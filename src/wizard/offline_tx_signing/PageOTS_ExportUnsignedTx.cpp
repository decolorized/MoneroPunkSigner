// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_ExportUnsignedTx.h"
#include "ui_PageOTS_Export.h"
#include "OfflineTxSigningWizard.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>

#include <memory>

#include "utils/Utils.h"
#include "utils/config.h"

#ifdef FEATHER_HAVE_HID
#include "MwWallet.h"
#include "HidOperation.h"
using MwLink::HidOperation;
#endif

static constexpr int kMethodQr   = 0;
static constexpr int kMethodFile = 1;
static constexpr int kMethodHid  = 2;

static constexpr int kStackQr   = 0;
static constexpr int kStackFile = 1;
static constexpr int kStackHid  = 2;

PageOTS_ExportUnsignedTx::PageOTS_ExportUnsignedTx(QWidget *parent, Wallet *wallet, PendingTransaction *tx)
        : QWizardPage(parent)
        , ui(new Ui::PageOTS_Export)
        , m_wallet(wallet)
        , m_tx(tx)
{
    ui->setupUi(this);
    this->setTitle("3. Export unsigned transaction");

    ui->label_step->hide();
    ui->label_instructions->setText("Scan this animated QR code with the offline wallet.");

#ifndef FEATHER_HAVE_HID
    if (ui->combo_method->count() > kMethodHid)
        ui->combo_method->removeItem(kMethodHid);
#endif

    connect(ui->btn_export, &QPushButton::clicked, this, [this]{
        if (ui->combo_method->currentIndex() == kMethodHid) {
            signOnHid();
        } else {
            exportUnsignedTx();
        }
    });

    connect(ui->combo_method, &QComboBox::currentIndexChanged,
            this, &PageOTS_ExportUnsignedTx::onMethodChanged);
}

void PageOTS_ExportUnsignedTx::onMethodChanged(int index)
{
    conf()->set(Config::offlineTxSigningMethod, index);

    switch (index) {
        case kMethodHid:
            ui->stackedWidget->setCurrentIndex(kStackHid);
            ui->btn_export->setText("Sign on HID");
            ui->btn_export->setVisible(true);
            break;
        case kMethodFile:
            ui->stackedWidget->setCurrentIndex(kStackFile);
            ui->btn_export->setText("Export to file");
            ui->btn_export->setVisible(true);
            break;
        case kMethodQr:
        default:
            ui->stackedWidget->setCurrentIndex(kStackQr);
            ui->btn_export->setVisible(false);
            // QR готовим здесь, а не только в initializePage: иначе при
            // переключении File → QR виджет остаётся пустым.
            if (m_tx)
                ui->widget_UR->setData("xmr-txunsigned", m_tx->unsignedTxToBin());
            break;
    }

    emit completeChanged();
    // nextId() зависит от способа (HID -> -1, страница финальная): заставляем
    // QWizard заново выбрать Next или Finish и их действие.
    otsRefreshWizardButtons(this);
}

void PageOTS_ExportUnsignedTx::exportUnsignedTx() {
    QString defaultName = QString("%1_unsigned_monero_tx").arg(QString::number(QDateTime::currentSecsSinceEpoch()));
    QString fn = Utils::getSaveFileName(this, "Save transaction to file", defaultName, "Transaction (*unsigned_monero_tx)");
    if (fn.isEmpty()) {
        return;
    }

    bool r = m_tx->saveToFile(fn);
    if (!r) {
        Utils::showError(this, "Failed to export unsigned transaction", m_wallet->errorString());
        return;
    }

    QFileInfo fileInfo(fn);
    Utils::openDir(this, "Successfully exported unsigned transaction", fileInfo.absolutePath());
}

#ifdef FEATHER_HAVE_HID

bool PageOTS_ExportUnsignedTx::isHidMode() const
{
    return ui->combo_method->currentIndex() == kMethodHid;
}

bool PageOTS_ExportUnsignedTx::isComplete() const
{
    if (isHidMode())
        return m_hidDone && m_hidError.isEmpty() && !m_hidBusy;
    return true;
}

void PageOTS_ExportUnsignedTx::setHidState(bool busy,
                                           const QString &note,
                                           bool success)
{
    Q_UNUSED(success);   // Next управляется только через isComplete()

    m_hidBusy = busy;
    ui->btn_export->setEnabled(!busy);
    ui->combo_method->setEnabled(!busy);

    if (ui->label_hid_status) {
        // Логи устройства — простой текст, не rich text.
        ui->label_hid_status->setTextFormat(Qt::PlainText);
        ui->label_hid_status->setText(note);
    }

    emit completeChanged();
    if (auto *w = wizard()) {
        if (auto *b = w->button(QWizard::BackButton))
            b->setEnabled(!busy);
    }
}

void PageOTS_ExportUnsignedTx::signOnHid()
{
    if (m_hidBusy)
        return;

    if (!m_tx) {
        Utils::showError(this, "No pending transaction",
            "Cannot sign: no unsigned transaction to send.");
        return;
    }

    const std::string utxBin = m_tx->unsignedTxToBin();
    if (utxBin.empty()) {
        Utils::showError(this, "Export failed", "Empty unsigned tx blob");
        return;
    }
    const QByteArray blob(utxBin.data(), int(utxBin.size()));

    if (quint32(blob.size()) > MwLink::kMaxPayload) {
        Utils::showError(this, "Transaction too large for HID",
            QString("Unsigned transaction is %1 KiB, the limit is %2 KiB.\n"
                    "Use QR codes or file transfer instead.")
                .arg((blob.size() + 1023) / 1024)
                .arg(MwLink::kMaxPayload / 1024));
        return;
    }

    if (m_op) {
        m_op->disconnect(this);
        m_op->cancel();
        m_op->deleteLater();
        m_op = nullptr;
    }

    // Фоновые шаги — только обмен с устройством. Загрузка подписанной
    // транзакции в Wallet — в слоте finished (UI-поток).
    auto signedBin = std::make_shared<QByteArray>();

    m_op = new HidOperation(this);
    m_op->setPayloadSize(quint32(blob.size()));

    // CLEAR чистит только outbox: убираем возможную устаревшую подписанную
    // транзакцию до PUT, чтобы не забрать результат прошлой операции.
    //
    // ВАЖНО: шаг КРИТИЧЕСКИЙ. Если очистка не удалась, продолжать нельзя:
    // waitForResult может забрать из outbox старую подписанную транзакцию
    // (сумма/комиссия от прошлой операции, адрес — из текущего диалога).
    m_op->addStep(QStringLiteral("Preparing device…"),
        [](MwLink::Wallet &dev) -> bool {
            return dev.clear(MwLink::KSignedTx);
        },
        /*critical=*/true);

    m_op->addStep(QStringLiteral("Sending unsigned tx…"),
        [blob](MwLink::Wallet &dev) -> bool {
            MwLink::Kind stored;
            return dev.put(MwLink::KUnsignedTx, blob, &stored);
        });

    // GET не ждёт подтверждения: waitForResult опрашивает STATUS и забирает файл.
    m_op->addStep(QStringLiteral("Waiting for signed tx (confirm on device)…"),
        [signedBin](MwLink::Wallet &dev) -> bool {
            return dev.waitForResult(MwLink::KSignedTx, MwLink::KUnsignedTx,
                                     signedBin.get(), /*timeoutMs=*/600000);
        });

    // При любом исходе (успех, отказ, таймаут, отмена) убираем результат с устройства.
    m_op->addCleanup(QStringLiteral("Clearing device state…"),
        [](MwLink::Wallet &dev) -> bool {
            return dev.clear(MwLink::KSignedTx);
        });

    connect(m_op, &HidOperation::stepStarted,
            this, &PageOTS_ExportUnsignedTx::onHidStep);
    connect(m_op, &HidOperation::logReceived,
            this, &PageOTS_ExportUnsignedTx::onHidLog);

    connect(m_op, &HidOperation::finished, this,
            [this, signedBin](bool ok, const QString &err) {
        QString error = err;
        const QString board = m_op ? m_op->deviceBoard() : QString();

        if (ok) {
            const auto size = signedBin->size();
            const std::string raw(signedBin->constData(), size_t(size));

            PendingTransaction *tx = raw.empty() ? nullptr
                                                 : m_wallet->loadSignedTxFromStr(raw);
            if (!tx || tx->status() != PendingTransaction::Status_Ok) {
                if (tx)
                    m_wallet->disposeTransaction(tx);
                ok = false;
                error = raw.empty()
                    ? QStringLiteral("Device returned an empty signed transaction")
                    : m_wallet->errorString();
                if (error.isEmpty())
                    error = QStringLiteral("Device returned an invalid signed transaction");
            }
            // Защита от подхвата старой подписанной транзакции из outbox устройства:
            // сверяем сумму и комиссию с исходной unsigned-транзакцией.
            else if (!m_tx
                     || tx->amount() != m_tx->amount()
                     || tx->fee()    != m_tx->fee()) {
                m_wallet->disposeTransaction(tx);
                ok = false;
                error = QStringLiteral(
                    "The signed transaction returned by the device does not match "
                    "the unsigned transaction (amount or fee mismatch). "
                    "Refusing to continue — possible stale outbox result.");
            }
            else if (auto *w = qobject_cast<OfflineTxSigningWizard*>(wizard())) {
                auto &f = w->fields();
                f.viaHid        = true;
                f.tx            = tx;
                f.signedTx      = raw;
                f.signedTxSize  = size;
                f.readyToCommit = true;
                f.hidDeviceName = board.isEmpty() ? QStringLiteral("HID device") : board;
            }
        }
        signedBin->clear();

        onHidFinished(ok, error);
    });

    m_hidDone = false;
    m_hidError.clear();
    setHidState(true, QStringLiteral("Starting…"));

    m_op->start();
}

void PageOTS_ExportUnsignedTx::onHidStep(int index, const QString &description)
{
    Q_UNUSED(index);
    if (ui->label_hid_status) {
        ui->label_hid_status->setTextFormat(Qt::PlainText);
        ui->label_hid_status->setText(description);
    }
}

void PageOTS_ExportUnsignedTx::onHidLog(quint8 level, const QString &text)
{
    if (level == MwLink::LogProgress || level == MwLink::LogInfo) {
        if (ui->label_hid_status) {
            ui->label_hid_status->setTextFormat(Qt::PlainText);
            ui->label_hid_status->setText(text);
        }
    }
}

void PageOTS_ExportUnsignedTx::onHidFinished(bool ok, const QString &error)
{
    if (m_op) {
        m_op->deleteLater();
        m_op = nullptr;
    }

    if (!ok) {
        m_hidDone  = false;
        m_hidError = error.isEmpty() ? QStringLiteral("unknown error") : error;
        setHidState(false,
            QStringLiteral("❌ Failed: %1\nPress «Sign on HID» to retry.")
                .arg(m_hidError));
        return;
    }

    m_hidDone = true;
    m_hidError.clear();

    // Подпись получена. Страница финальная за счёт nextId() == -1 (HID-режим),
    // Finish закрывает мастер, вызывающий код покажет транзакцию и отправит.
    setButtonText(QWizard::FinishButton, "View transaction");

    setHidState(false,
        QStringLiteral("✅ Transaction signed on device.\n"
                       "Press «View transaction» to review and send."),
        /*success=*/true);
}

#else  // !FEATHER_HAVE_HID

bool PageOTS_ExportUnsignedTx::isComplete() const
{
    if (ui->combo_method->currentIndex() == kMethodHid)
        return false;
    return true;
}

void PageOTS_ExportUnsignedTx::signOnHid()
{
    Utils::showError(this, "HID not available",
        "This build of Feather was compiled without HID support.\n"
        "Use QR codes or file transfer instead.");
}

#endif // FEATHER_HAVE_HID

void PageOTS_ExportUnsignedTx::initializePage() {
#ifdef FEATHER_HAVE_HID
    const int maxMethod = kMethodHid;
#else
    const int maxMethod = kMethodFile;
#endif
    int method = conf()->get(Config::offlineTxSigningMethod).toInt();
    method = qBound(int(kMethodQr), method, qMin(maxMethod, ui->combo_method->count() - 1));

#ifdef FEATHER_HAVE_HID
    m_hidDone  = false;
    m_hidError.clear();
    m_hidBusy  = false;
    if (m_op) {
        m_op->disconnect(this);
        m_op->cancel();
        m_op->deleteLater();
        m_op = nullptr;
    }
    if (ui->label_hid_status)
        ui->label_hid_status->clear();

    // Текст кнопки Finish на входе; финальность определяет nextId().
    setButtonText(QWizard::FinishButton, "Finish");

    // Новый этап подписи: viaHid от предыдущего этапа (key images) не должен
    // заставлять ImportSignedTx ждать готовую HID-транзакцию.
    if (auto *w = qobject_cast<OfflineTxSigningWizard*>(wizard()))
        w->fields().viaHid = false;
#endif
    ui->combo_method->setEnabled(true);
    ui->btn_export->setEnabled(true);

    {
        QSignalBlocker blocker(ui->combo_method);
        ui->combo_method->setCurrentIndex(method);
    }
    onMethodChanged(method);   // для QR сам подготовит данные
}

void PageOTS_ExportUnsignedTx::cleanupPage() {
#ifdef FEATHER_HAVE_HID
    // Уходя со страницы (Back), возвращаем штатный текст кнопки.
    setButtonText(QWizard::FinishButton, "Finish");
#endif
}

int PageOTS_ExportUnsignedTx::nextId() const {
#ifdef FEATHER_HAVE_HID
    // HID-режим: подписанная транзакция принимается на этой же странице,
    // страница Import не нужна. -1 → страница финальная (Next скрыт, Finish
    // доступен по isComplete()). validatePage() здесь НЕ блокирует переход:
    // при false не закрывался бы и Finish.
    if (isHidMode())
        return -1;
#endif
    return OfflineTxSigningWizard::Page_ImportSignedTx;
}

bool PageOTS_ExportUnsignedTx::validatePage() {
    return true;
}
