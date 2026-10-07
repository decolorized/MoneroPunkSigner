// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_ImportSignedTx.h"
#include "ui_PageOTS_Import.h"
#include "OfflineTxSigningWizard.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

#include "dialog/TxConfDialog.h"
#include "dialog/TxConfAdvDialog.h"
#include "utils/Icons.h"
#include "utils/config.h"
#include "utils/Utils.h"

static constexpr int kMethodQr   = 0;
static constexpr int kMethodFile = 1;
static constexpr int kMethodHid  = 2;

PageOTS_ImportSignedTx::PageOTS_ImportSignedTx(QWidget *parent, Wallet *wallet, TxWizardFields *wizardFields)
        : PageOTS_Import(parent, wallet, wizardFields, 4, "signed transaction", "Transaction (*signed_monero_tx)", "Send..")
{
}

bool PageOTS_ImportSignedTx::isHidMode() const
{
    auto *wiz = qobject_cast<OfflineTxSigningWizard*>(wizard());
    return wiz && wiz->fields().viaHid;
}

void PageOTS_ImportSignedTx::initializePage()
{
    // --- HID-режим: signed tx уже загружена на предыдущей странице ---
    if (isHidMode()) {
        this->setTitle("4. Signed transaction received");

        if (ui->stackedWidget)
            ui->stackedWidget->setCurrentIndex(kMethodHid);

        if (ui->combo_method) ui->combo_method->hide();
        if (ui->btn_import)   ui->btn_import->hide();
        if (ui->label_step)   ui->label_step->hide();

        auto *wiz = qobject_cast<OfflineTxSigningWizard*>(wizard());
        if (!wiz) return;
        const auto &f = wiz->fields();

        if (ui->label_instructions) {
            ui->label_instructions->setText(
                QString("Signed transaction received from the HID device.\n\n"
                        "Device:     %1\n"
                        "Size:       %2 bytes\n"
                        "Status:     ready to broadcast\n\n"
                        "Press Finish to review and send it to the network.")
                    .arg(f.hidDeviceName)
                    .arg(f.signedTxSize));
        }

        if (ui->frame_status) {
            ui->frame_status->show();
            ui->frame_status->setInfo(icons()->icon("confirmed.svg"),
                QString("Signed transaction received (%1 bytes)")
                    .arg(f.signedTxSize));
        }

#ifdef FEATHER_HAVE_SCANNER
        if (m_scanWidget) {
            m_scanWidget->reset();
            m_scanWidget->hide();
        }
#endif

        m_success = true;
        this->setButtonText(QWizard::FinishButton, "Review and send");

        emit completeChanged();
        return;
    }

    // --- QR / Files: старое поведение ---
    int method = conf()->get(Config::offlineTxSigningMethod).toInt();
    if (method > 1) method = 1;

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

#ifdef FEATHER_HAVE_SCANNER
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
#endif
}

void PageOTS_ImportSignedTx::importFromStr(const std::string &data) {
    PendingTransaction *tx = m_wallet->loadSignedTxFromStr(data);
    if (!tx || tx->status() != PendingTransaction::Status_Ok) {
#ifdef FEATHER_HAVE_SCANNER
        m_scanWidget->pause();
#endif
        Utils::showError(this, "Failed to import signed transaction", m_wallet->errorString());
#ifdef FEATHER_HAVE_SCANNER
        m_scanWidget->reset();
#endif
        return;
    }

    m_wizardFields->tx = tx;
    m_wizardFields->readyToCommit = true;
    PageOTS_Import::onSuccess();
}

int PageOTS_ImportSignedTx::nextId() const {
    return -1;
}

bool PageOTS_ImportSignedTx::validatePage() {
    // --- HID-режим: tx уже в fields, отправляем в сеть ---
    if (isHidMode()) {
        auto *wiz = qobject_cast<OfflineTxSigningWizard*>(wizard());
        if (!wiz || !wiz->fields().tx) {
            Utils::showError(this, "No signed transaction",
                "The device did not return a signed transaction.");
            return false;
        }

        PendingTransaction *tx = wiz->fields().tx;

        if (tx->status() != PendingTransaction::Status_Ok) {
            Utils::showError(this, "Transaction is not ready",
                "The signed transaction has an invalid status.");
            return false;
        }

        // Транзакцию НЕ рассылаем здесь: как и в QR/Files-режиме, финал
        // (подтверждение, заметка, история) остаётся за вызывающим кодом,
        // который получит signedTx() и readyToCommit().
        m_wizardFields->readyToCommit = true;
        return true;
    }

    // --- QR / Files: старое поведение ---
#ifdef FEATHER_HAVE_SCANNER
    if (m_scanWidget) {
        m_scanWidget->disconnect();
    }
#endif
    m_wizardFields->readyToCommit = true;
    return true;
}
