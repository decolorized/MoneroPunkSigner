// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_ExportOutputs.h"
#include "ui_PageOTS_Export.h"
#include "OfflineTxSigningWizard.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFileDialog>
#include <QFileInfo>
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

PageOTS_ExportOutputs::PageOTS_ExportOutputs(QWidget *parent, Wallet *wallet)
        : QWizardPage(parent)
        , ui(new Ui::PageOTS_Export)
        , m_wallet(wallet)
        , m_check_exportAll(new QCheckBox(this))
{
    ui->setupUi(this);
    this->setTitle("1. Export outputs");

    ui->label_step->hide();
    ui->label_instructions->setText("Scan this animated QR code with your offline wallet (Tools → Offline Transaction Signing).");

#ifndef FEATHER_HAVE_HID
    // Сборка без HID: пункт не должен быть доступен.
    if (ui->combo_method->count() > kMethodHid)
        ui->combo_method->removeItem(kMethodHid);
#endif

    m_check_exportAll->setText("Export all outputs");
    ui->layout_extra->addWidget(m_check_exportAll);
    connect(m_check_exportAll, &QCheckBox::toggled,
            this, &PageOTS_ExportOutputs::setupUR);

    connect(ui->btn_export, &QPushButton::clicked, this, [this]{
        if (ui->combo_method->currentIndex() == kMethodHid) {
            sendOutputsToHid();
        } else {
            exportOutputs();
        }
    });

    connect(ui->combo_method, &QComboBox::currentIndexChanged,
            this, &PageOTS_ExportOutputs::onMethodChanged);
}

void PageOTS_ExportOutputs::onMethodChanged(int index)
{
    conf()->set(Config::offlineTxSigningMethod, index);

    switch (index) {
        case kMethodHid:
            ui->stackedWidget->setCurrentIndex(kStackHid);
            ui->btn_export->setText("Send via HID");
            ui->btn_export->setVisible(true);
            m_check_exportAll->setVisible(true);
            break;
        case kMethodFile:
            ui->stackedWidget->setCurrentIndex(kStackFile);
            ui->btn_export->setText("Export to file");
            ui->btn_export->setVisible(true);
            m_check_exportAll->setVisible(true);
            break;
        case kMethodQr:
        default:
            ui->stackedWidget->setCurrentIndex(kStackQr);
            ui->btn_export->setVisible(false);
            m_check_exportAll->setVisible(true);
            setupUR(m_check_exportAll->isChecked());
            break;
    }

    emit completeChanged();
    // nextId() зависит от способа (HID -> -1, страница финальная): заставляем
    // QWizard заново выбрать Next или Finish и их действие.
    otsRefreshWizardButtons(this);
}

void PageOTS_ExportOutputs::exportOutputs() {
    QString defaultName = QString("%1_%2")
        .arg(m_wallet->walletName(),
             QString::number(QDateTime::currentSecsSinceEpoch()));

    QString fn = Utils::getSaveFileName(this, "Save outputs to file",
                                        defaultName, "Outputs (*_outputs)");
    if (fn.isEmpty()) return;
    if (!fn.endsWith("_outputs")) fn += "_outputs";

    bool r = m_wallet->exportOutputs(fn, m_check_exportAll->isChecked());
    if (!r) {
        Utils::showError(this, "Failed to export outputs", m_wallet->errorString());
        return;
    }

    QFileInfo fileInfo(fn);
    Utils::openDir(this, "Successfully exported outputs", fileInfo.absolutePath());
}

void PageOTS_ExportOutputs::setupUR(bool all) {
    std::string output_export;
    m_wallet->exportOutputsToStr(output_export, all);
    ui->widget_UR->setData("xmr-output", output_export);
}

// ---------------------------------------------------------------------------
// HID
// ---------------------------------------------------------------------------

#ifdef FEATHER_HAVE_HID

// Та же логика, что PageOTS_ImportKeyImages::proceed(): импорт key images
// раскрывает ноде набор ваших выходов. Для HID-пути спрашиваем ДО отправки.
static bool confirmKeyImageImport(QWidget *parent)
{
    if (!conf()->get(Config::warnOnKiImport).toBool())
        return true;

    QMessageBox warning{parent};
    warning.setWindowTitle("Warning");
    warning.setText("Key image import reveals which outputs you own to the node. "
                    "Make sure you are connected to a trusted node.\n\n"
                    "Do you want to proceed?");
    warning.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

    if (warning.exec() == QMessageBox::No)
        return false;

    conf()->set(Config::warnOnKiImport, false);
    return true;
}

bool PageOTS_ExportOutputs::isHidMode() const
{
    return ui->combo_method->currentIndex() == kMethodHid;
}

bool PageOTS_ExportOutputs::isComplete() const
{
    // Сравниваем индекс напрямую: isHidMode() существует только при
    // FEATHER_HAVE_HID, а этот метод компилируется всегда.
    if (ui->combo_method->currentIndex() == kMethodHid)
        return m_hidDone && m_hidError.isEmpty() && !m_hidBusy;
    return true;
}

void PageOTS_ExportOutputs::setHidState(bool busy,
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

    // Сначала пересчёт QWizard, потом Back — иначе QWizard перезапишет состояние.
    emit completeChanged();
    if (auto *w = wizard()) {
        if (auto *b = w->button(QWizard::BackButton))
            b->setEnabled(!busy);
        // Cancel оставляем доступным: закрытие мастера безопасно —
        // ~HidOperation отменяет операцию и дожидается потока.
    }
}

// Мастер открыт только ради синхронизации key images (без PendingTransaction):
// после успешного обмена следующий экран не нужен.
bool PageOTS_ExportOutputs::isSyncOnlyRun() const
{
    auto *w = qobject_cast<OfflineTxSigningWizard*>(wizard());
    return w && w->fields().isSyncOnly();
}

void PageOTS_ExportOutputs::sendOutputsToHid()
{
    if (m_hidBusy)
        return;

    if (!confirmKeyImageImport(this))
        return;

    const bool all = m_check_exportAll->isChecked();
    m_outputsBlob.clear();
    if (!m_wallet->exportOutputsToStr(m_outputsBlob, all)) {
        Utils::showError(this, "Export failed", m_wallet->errorString());
        return;
    }
    if (m_outputsBlob.empty()) {
        Utils::showError(this, "Export failed", "Empty outputs blob");
        return;
    }
    const QByteArray blob(m_outputsBlob.data(), int(m_outputsBlob.size()));

    if (quint32(blob.size()) > MwLink::kMaxPayload) {
        m_outputsBlob.clear();
        Utils::showError(this, "Outputs too large for HID",
            QString("Outputs are %1 KiB, the limit is %2 KiB.\n"
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

    // Шаги работают в фоновом потоке: только обмен с устройством.
    // Результат складываем в буфер, импорт в кошелёк — в слоте finished (UI-поток).
    auto keyImages = std::make_shared<QByteArray>();

    m_op = new HidOperation(this);
    m_op->setPayloadSize(quint32(blob.size()));

    // CLEAR чистит только outbox: убираем возможный устаревший результат до PUT,
    // иначе waitForResult мог бы забрать key images прошлой операции.
    //
    // ВАЖНО: шаг КРИТИЧЕСКИЙ. Если очистка не удалась, продолжать нельзя:
    // waitForResult может забрать из outbox старые key images, не относящиеся
    // к текущей операции.
    m_op->addStep(QStringLiteral("Preparing device…"),
        [](MwLink::Wallet &dev) -> bool {
            return dev.clear(MwLink::KKeyImages);
        },
        /*critical=*/true);

    m_op->addStep(QStringLiteral("Sending outputs to device…"),
        [blob](MwLink::Wallet &dev) -> bool {
            MwLink::Kind stored;
            return dev.put(MwLink::KOutputs, blob, &stored);
        });

    // GET не ждёт подтверждения: waitForResult опрашивает STATUS и забирает файл.
    m_op->addStep(QStringLiteral("Waiting for key images (confirm on device)…"),
        [keyImages](MwLink::Wallet &dev) -> bool {
            return dev.waitForResult(MwLink::KKeyImages, MwLink::KOutputs,
                                     keyImages.get(), 600000);
        });

    // При любом исходе (успех, отказ, таймаут, отмена) убираем результат с устройства.
    m_op->addCleanup(QStringLiteral("Clearing device state…"),
        [](MwLink::Wallet &dev) -> bool {
            return dev.clear(MwLink::KKeyImages);
        });

    connect(m_op, &HidOperation::stepStarted,
            this, &PageOTS_ExportOutputs::onHidStep);
    connect(m_op, &HidOperation::logReceived,
            this, &PageOTS_ExportOutputs::onHidLog);

    connect(m_op, &HidOperation::finished, this,
            [this, keyImages](bool ok, const QString &err) {
        QString error = err;
        const QString board = m_op ? m_op->deviceBoard() : QString();
        m_outputsBlob.clear();

        if (ok) {
            const auto kiSize = keyImages->size();
            const std::string ki(keyImages->constData(), size_t(kiSize));

            if (ki.empty()) {
                ok = false;
                error = QStringLiteral("Device returned empty key images");
            } else if (!m_wallet->importKeyImagesFromStr(ki)) {
                ok = false;
                error = m_wallet->errorString();
            } else if (auto *w = qobject_cast<OfflineTxSigningWizard*>(wizard())) {
                auto &f = w->fields();
                f.viaHid        = true;
                f.keyImages     = ki;
                f.keyImagesSize = kiSize;
                f.hidDeviceName = board.isEmpty() ? QStringLiteral("HID device") : board;
            }
        }
        keyImages->clear();

        onHidFinished(ok, error);
    });

    m_hidDone = false;
    m_hidError.clear();
    setHidState(true, QStringLiteral("Starting…"));

    m_op->start();
}

void PageOTS_ExportOutputs::onHidStep(int index, const QString &description)
{
    Q_UNUSED(index);
    if (ui->label_hid_status) {
        ui->label_hid_status->setTextFormat(Qt::PlainText);
        ui->label_hid_status->setText(description);
    }
}

void PageOTS_ExportOutputs::onHidLog(quint8 level, const QString &text)
{
    if (level == MwLink::LogProgress || level == MwLink::LogInfo) {
        if (ui->label_hid_status) {
            ui->label_hid_status->setTextFormat(Qt::PlainText);
            ui->label_hid_status->setText(text);
        }
    }
}

void PageOTS_ExportOutputs::onHidFinished(bool ok, const QString &error)
{
    if (m_op) {
        m_op->deleteLater();
        m_op = nullptr;
    }

    if (!ok) {
        m_hidDone  = false;
        m_hidError = error.isEmpty() ? QStringLiteral("unknown error") : error;
        setHidState(false,
            QStringLiteral("❌ Failed: %1\nPress «Send via HID» to retry.")
                .arg(m_hidError));
        return;
    }

    m_hidDone = true;
    m_hidError.clear();

    // HID-синхронизация завершена. Страница уже «финальная» за счёт
    // nextId() == -1 (см. nextId()): QWizard показывает Finish, а Finish
    // доступен ровно тогда, когда isComplete() == true.
    setButtonText(QWizard::FinishButton, QStringLiteral("Finish"));

    setHidState(false,
        isSyncOnlyRun()
            ? QStringLiteral("✅ Key images synchronized and imported.\n"
                             "Press Finish to close the wizard.")
            : QStringLiteral("✅ Key images imported successfully.\n"
                             "Press «View transaction» to continue."),
        true);
}

#else  // !FEATHER_HAVE_HID

bool PageOTS_ExportOutputs::isComplete() const
{
    if (ui->combo_method->currentIndex() == kMethodHid)
        return false;
    return true;
}

void PageOTS_ExportOutputs::sendOutputsToHid()
{
    Utils::showError(this, "HID not available",
        "This build of Feather was compiled without HID support.\n"
        "Use QR codes or file transfer instead.");
}

#endif // FEATHER_HAVE_HID

void PageOTS_ExportOutputs::initializePage() {
#ifdef FEATHER_HAVE_HID
    const int maxMethod = kMethodHid;
#else
    const int maxMethod = kMethodFile;
#endif
    int method = conf()->get(Config::offlineTxSigningMethod).toInt();
    method = qBound(int(kMethodQr), method, qMin(maxMethod, ui->combo_method->count() - 1));

#ifdef FEATHER_HAVE_HID
    m_hidDone = false;
    m_hidError.clear();
    m_hidBusy = false;
    if (m_op) {
        m_op->disconnect(this);   // чтобы старая операция не трогала новое состояние
        m_op->cancel();
        m_op->deleteLater();
        m_op = nullptr;
    }
    if (ui->label_hid_status)
        ui->label_hid_status->clear();

    // Текст кнопки Finish на входе. Финальность НЕ трогаем: её определяет
    // nextId() (-1 в HID-режиме), setFinalPage() не используем.
    setButtonText(QWizard::FinishButton, "Finish");

    // viaHid описывает только эту страницу: сбрасываем при каждом входе.
    if (auto *w = qobject_cast<OfflineTxSigningWizard*>(wizard()))
        w->fields().viaHid = false;
#endif
    ui->combo_method->setEnabled(true);
    ui->btn_export->setEnabled(true);

    // Одно явное применение метода вместо двойного вызова onMethodChanged.
    {
        QSignalBlocker blocker(ui->combo_method);
        ui->combo_method->setCurrentIndex(method);
    }
    onMethodChanged(method);
}

void PageOTS_ExportOutputs::cleanupPage() {
#ifdef FEATHER_HAVE_HID
    // Уходя со страницы (Back), возвращаем штатные кнопки.
    m_hidDone = false;
    setButtonText(QWizard::FinishButton, "Finish");
#endif
}

int PageOTS_ExportOutputs::nextId() const {
#ifdef FEATHER_HAVE_HID
    // HID-режим: key images уже импортированы в кошелёк прямо на этой
    // странице, страница Import не нужна. -1 делает страницу финальной:
    // QWizard скрывает Next и показывает Finish (доступен, когда isComplete()).
    // Нельзя «блокировать» Next через validatePage() == false — это же
    // validatePage() вызывается и на Finish, и мастер перестаёт закрываться.
    if (isHidMode())
        return -1;
#endif
    return OfflineTxSigningWizard::Page_ImportKeyImages;
}
