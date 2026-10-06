// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_ExportSignedTx.h"
#include "ui_PageOTS_Export.h"

#include <QFileDialog>
#include <QSignalBlocker>

#include "OfflineTxSigningWizard.h"
#include "dialog/TxConfDialog.h"
#include "dialog/TxConfAdvDialog.h"
#include "utils/config.h"
#include "utils/Utils.h"

PageOTS_ExportSignedTx::PageOTS_ExportSignedTx(QWidget *parent, Wallet *wallet, TxWizardFields *wizardFields)
        : QWizardPage(parent)
        , ui(new Ui::PageOTS_Export)
        , m_wallet(wallet)
        , m_wizardFields(wizardFields)
{
    ui->setupUi(this);

    this->setTitle("4. Export signed transaction");

    // HID не поддерживается на этой странице — убираем третий пункт.
    if (ui->combo_method->count() > 2)
        ui->combo_method->removeItem(2);

    ui->label_step->hide();
    ui->label_instructions->setText("Scan this animated QR code with your view-only wallet.");

    connect(ui->btn_export, &QPushButton::clicked, this, &PageOTS_ExportSignedTx::exportSignedTx);
    connect(ui->combo_method, &QComboBox::currentIndexChanged, this, [this](int index){
        if (index < 0 || index > 1)
            return;
        conf()->set(Config::offlineTxSigningMethod, index);
        ui->stackedWidget->setCurrentIndex(index);
        // QR готовим при переключении, иначе после File → QR виджет пуст.
        if (index == 0 && m_wizardFields->utx) {
            m_wizardFields->utx->signToStr(m_wizardFields->signedTx);
            ui->widget_UR->setData("xmr-txsigned", m_wizardFields->signedTx);
        }
    });
}

void PageOTS_ExportSignedTx::exportSignedTx() {
    QString defaultName = QString("%1_signed_monero_tx").arg(QString::number(QDateTime::currentSecsSinceEpoch()));
    QString fn = Utils::getSaveFileName(this, "Save signed transaction to file", defaultName, "Transaction (*signed_monero_tx)");
    if (fn.isEmpty()) {
        return;
    }

    bool r = m_wizardFields->utx->sign(fn);

    if (!r) {
        Utils::showError(this, "Failed to save transaction to file");
        return;
    }

    QFileInfo fileInfo(fn);
    Utils::openDir(this, "Transaction saved successfully", fileInfo.absolutePath());
}

void PageOTS_ExportSignedTx::initializePage() {
    if (!m_wizardFields->utx) {
        Utils::showError(this, "Unknown error");
        this->close();
        return;
    }

    // Если пришли из HID-режима, conf может содержать 2 — нормализуем.
    int method = conf()->get(Config::offlineTxSigningMethod).toInt();
    if (method > 1)
        method = 1;

    {
        QSignalBlocker blocker(ui->combo_method);
        ui->combo_method->setCurrentIndex(method);
    }
    ui->stackedWidget->setCurrentIndex(method);

    // QR-режим: подготовить blob для виджета. Files: подпись делается
    // в exportSignedTx(). HID: эта страница недоступна.
    if (method == 0) {
        m_wizardFields->utx->signToStr(m_wizardFields->signedTx);
        ui->widget_UR->setData("xmr-txsigned", m_wizardFields->signedTx);
    }
}

int PageOTS_ExportSignedTx::nextId() const {
    return -1;
}