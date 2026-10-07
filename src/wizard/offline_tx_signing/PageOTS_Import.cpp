// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_Import.h"
#include "ui_PageOTS_Import.h"
#include "OfflineTxSigningWizard.h"

#include <QFileDialog>

#include "utils/config.h"
#include "utils/Icons.h"
#include "utils/Utils.h"

// Индексы combo_method / stackedWidget
//   PageOTS_Import.ui содержит три страницы:
//     0 — Animated QR codes
//     1 — File transfer
//     2 — HID device (используется подклассами, базовая страница туда
//         не переключается)
static constexpr int kMethodQr   = 0;
static constexpr int kMethodFile = 1;
static constexpr int kMethodHid  = 2;

PageOTS_Import::PageOTS_Import(QWidget *parent,
                               Wallet *wallet,
                               TxWizardFields *wizardFields,
                               int step,
                               const QString &type,
                               const QString &fileType,
                               const QString &successButtonText)
        : QWizardPage(parent)
        , m_wallet(wallet)
        , m_wizardFields(wizardFields)
#ifdef FEATHER_HAVE_SCANNER
        , m_scanWidget(wizardFields ? wizardFields->scanWidget : nullptr)
#endif
        , m_type(type)
        , m_fileType(fileType)
        , m_successButtonText(successButtonText)
        , ui(new Ui::PageOTS_Import)
{
    ui->setupUi(this);

    this->setTitle(QString("%1. Import %2").arg(QString::number(step), m_type));
    this->setCommitPage(true);
    this->setButtonText(QWizard::CommitButton, "Next");
    this->setButtonText(QWizard::FinishButton, "Next");

    ui->label_step->hide();
    ui->frame_status->hide();

    // Файловая ветка
    connect(ui->btn_import, &QPushButton::clicked,
            this, &PageOTS_Import::importFromFile);

    // Переключение метода: QR / File / HID.
    // Базовая страница не поддерживает HID-страницу (её обрабатывают
    // подклассы PageOTS_ImportKeyImages / PageOTS_ImportSignedTx).
    // Если пользователь выберет HID на базовой странице — принудительно
    // возвращаемся на Files, чтобы не показывать пустую страницу.
    connect(ui->combo_method, &QComboBox::currentIndexChanged,
            this, [this](int index) {
        if (index == kMethodHid) {
            // Базовая страница не умеет HID — откатываемся на Files.
            ui->combo_method->setCurrentIndex(kMethodFile);
            return;
        }
        if (index < 0 || index > kMethodFile)
            return;
        conf()->set(Config::offlineTxSigningMethod, index);
        ui->stackedWidget->setCurrentIndex(index);
    });
}

// ---------------------------------------------------------------------------
// Сканирование QR
// ---------------------------------------------------------------------------
//
// Только при наличии камеры (FEATHER_HAVE_SCANNER). Без сканера визард
// работает через файлы и HID, поэтому слот и его подключение отсутствуют.

#ifdef FEATHER_HAVE_SCANNER

void PageOTS_Import::onScanFinished(bool success)
{
    if (!success) {
        m_scanWidget->pause();
        Utils::showError(this, "Failed to scan QR code", m_scanWidget->getURError());
        m_scanWidget->reset();
        return;
    }

    std::string data = m_scanWidget->getURData();
    importFromStr(data);
}

#endif // FEATHER_HAVE_SCANNER

// ---------------------------------------------------------------------------
// Успех
// ---------------------------------------------------------------------------

void PageOTS_Import::onSuccess()
{
    m_success = true;
    emit completeChanged();

    // Если Finish виден — нажимаем его; иначе Commit.
    if (auto *b = this->wizard()->button(QWizard::FinishButton); b && b->isVisible()) {
        b->click();
    } else if (auto *b = this->wizard()->button(QWizard::CommitButton)) {
        b->click();
    }

    ui->frame_status->show();
    ui->frame_status->setInfo(icons()->icon("confirmed.svg"),
        QString("%1 imported successfully").arg(m_type));
    this->setButtonText(QWizard::FinishButton, m_successButtonText);
}

// ---------------------------------------------------------------------------
// Файловый импорт
// ---------------------------------------------------------------------------

void PageOTS_Import::importFromFile()
{
    QString fn = Utils::getOpenFileName(this,
        QString("Import %1 file").arg(m_type),
        QString("%1;;All Files (*)").arg(m_fileType));
    if (fn.isEmpty()) {
        return;
    }

    QFile file(fn);
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }

    QByteArray qdata = file.readAll();
    std::string data = qdata.toStdString();
    file.close();

    importFromStr(data);
}

// ---------------------------------------------------------------------------
// initializePage
// ---------------------------------------------------------------------------
//
// Базовая страница используется подклассами:
//   • PageOTS_ImportKeyImages
//   • PageOTS_ImportSignedTx
//   • PageOTS_ImportOffline
//   • PageOTS_ImportUnsignedTx
//
// Они могут переопределять initializePage() (например, чтобы показать
// итоги в HID-режиме вместо сканера). Если не переопределяют — этот
// метод запускает QR-сканер по старой логике.
//
// Индекс метода нормализуется: если в conf() сохранён HID (2),
// а базовый UI поддерживает только QR (0) и Files (1), берём Files.

void PageOTS_Import::initializePage()
{
    int method = conf()->get(Config::offlineTxSigningMethod).toInt();
    if (method > kMethodFile)
        method = kMethodFile;

    if (ui->combo_method) {
        ui->combo_method->setCurrentIndex(method);
    }
    if (ui->stackedWidget) {
        ui->stackedWidget->setCurrentIndex(method);
    }

#ifdef FEATHER_HAVE_SCANNER
    if (m_scanWidget) {
        m_scanWidget->reset();
        connect(m_scanWidget, &QrCodeScanWidget::finished,
                this, &PageOTS_Import::onScanFinished, Qt::UniqueConnection);
        if (ui->layout_scanner) {
            ui->layout_scanner->addWidget(m_scanWidget);
        }
        m_scanWidget->startCapture(true);
    }
#endif
}

// ---------------------------------------------------------------------------
// isComplete / validatePage
// ---------------------------------------------------------------------------

bool PageOTS_Import::isComplete() const
{
    return m_success;
}

bool PageOTS_Import::validatePage()
{
#ifdef FEATHER_HAVE_SCANNER
    if (m_scanWidget) {
        m_scanWidget->disconnect();
        m_scanWidget->pause();
    }
#endif
    return true;
}
