// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_ImportKeyImages.h"
#include "ui_PageOTS_Import.h"
#include "OfflineTxSigningWizard.h"

#include <QCheckBox>
#include <QMessageBox>
#include <QPushButton>

#include "utils/config.h"
#include "utils/Icons.h"
#include "utils/Utils.h"

static constexpr int kMethodQr   = 0;
static constexpr int kMethodFile = 1;
static constexpr int kMethodHid  = 2;

PageOTS_ImportKeyImages::PageOTS_ImportKeyImages(QWidget *parent, Wallet *wallet, TxWizardFields *wizardFields)
        : PageOTS_Import(parent, wallet, wizardFields, 2, "key images", "Key Images (*_keyImages)", "Create transaction")
{
}

bool PageOTS_ImportKeyImages::isHidMode() const
{
    auto *wiz = qobject_cast<OfflineTxSigningWizard*>(wizard());
    return wiz && wiz->fields().viaHid;
}

void PageOTS_ImportKeyImages::initializePage()
{
    // --- HID-режим: key images уже импортированы на предыдущей странице ---
    if (isHidMode()) {
        this->setTitle("2. Key images received");

        // Переключаем на страницу HID (индекс 2).
        if (ui->stackedWidget)
            ui->stackedWidget->setCurrentIndex(kMethodHid);

        // Скрываем комбобокс и кнопку импорта.
        if (ui->combo_method) ui->combo_method->hide();
        if (ui->btn_import)   ui->btn_import->hide();
        if (ui->label_step)   ui->label_step->hide();

        auto *wiz = qobject_cast<OfflineTxSigningWizard*>(wizard());
        if (!wiz) return;
        const auto &f = wiz->fields();

        if (ui->label_instructions) {
            ui->label_instructions->setText(
                QString("Key images received from the HID device.\n\n"
                        "Device:     %1\n"
                        "Size:       %2 bytes\n"
                        "Imported:   yes\n\n"
                        "You can now create the unsigned transaction.")
                    .arg(f.hidDeviceName)
                    .arg(f.keyImagesSize));
        }

        if (ui->frame_status) {
            ui->frame_status->show();
            ui->frame_status->setInfo(icons()->icon("confirmed.svg"),
                QString("Key images imported (%1 bytes)").arg(f.keyImagesSize));
        }

        if (m_scanWidget) {
            m_scanWidget->reset();
            m_scanWidget->hide();
        }

        m_success = true;
        this->setButtonText(QWizard::CommitButton, "Create transaction");
        this->setButtonText(QWizard::FinishButton, "Create transaction");

        emit completeChanged();
        return;
    }

    // --- QR / Files: старое поведение ---
    int method = conf()->get(Config::offlineTxSigningMethod).toInt();
    if (method > 1) method = 1;   // нормализация

    if (ui->combo_method) {
        ui->combo_method->setCurrentIndex(method);
        ui->combo_method->show();
    }
    if (ui->stackedWidget) {
        ui->stackedWidget->setCurrentIndex(method);
    }
    if (ui->btn_import) ui->btn_import->show();
    if (ui->frame_status) ui->frame_status->hide();
    if (ui->label_instructions) {
        ui->label_instructions->setText(
            "Scan the animated QR code shown on the offline wallet.");
    }

    if (m_scanWidget) {
        m_scanWidget->reset();
        m_scanWidget->show();
        connect(m_scanWidget, &QrCodeScanWidget::finished,
                this, &PageOTS_Import::onScanFinished, Qt::UniqueConnection);
        if (ui->layout_scanner) {
            ui->layout_scanner->addWidget(m_scanWidget);
        }
        m_scanWidget->startCapture(true);
    }
}

void PageOTS_ImportKeyImages::importFromStr(const std::string &data) {
    if (!proceed()) {
        m_scanWidget->reset();
        return;
    }

    bool r = m_wallet->importKeyImagesFromStr(data);
    if (!r) {
        m_scanWidget->pause();
        Utils::showError(this, "Failed to import key images", m_wallet->errorString());
        m_scanWidget->reset();
        return;
    }

    PageOTS_Import::onSuccess();
}

bool PageOTS_ImportKeyImages::proceed() {
    if (!conf()->get(Config::warnOnKiImport).toBool()) {
        return true;
    }

    QMessageBox warning{this};
    warning.setWindowTitle("Warning");
    warning.setText("Key image import reveals which outputs you own to the node. "
                    "Make sure you are connected to a trusted node.\n\n"
                    "Do you want to proceed?");
    warning.setStandardButtons(QMessageBox::Yes | QMessageBox::No);

    switch(warning.exec()) {
        case QMessageBox::No:
            return false;
        default:
            conf()->set(Config::warnOnKiImport, false);
            return true;
    }
}

int PageOTS_ImportKeyImages::nextId() const {
    return -1;
}