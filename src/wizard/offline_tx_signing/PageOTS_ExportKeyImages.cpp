// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_ExportKeyImages.h"
#include "ui_PageOTS_Export.h"
#include "OfflineTxSigningWizard.h"

#include <QCheckBox>
#include <QSignalBlocker>

#include "utils/config.h"
#include "utils/Utils.h"

PageOTS_ExportKeyImages::PageOTS_ExportKeyImages(QWidget *parent, Wallet *wallet, TxWizardFields *wizardFields)
        : QWizardPage(parent)
        , ui(new Ui::PageOTS_Export)
        , m_wallet(wallet)
        , m_wizardFields(wizardFields)
{
    ui->setupUi(this);
    this->setTitle("2. Export key images");

    // HID не поддерживается на этой странице — убираем третий пункт.
    if (ui->combo_method->count() > 2)
        ui->combo_method->removeItem(2);

    ui->label_step->hide();
    ui->label_instructions->setText("Scan this animated QR code with the view-only wallet.");

    connect(ui->btn_export, &QPushButton::clicked, this, &PageOTS_ExportKeyImages::exportKeyImages);
    connect(ui->combo_method, &QComboBox::currentIndexChanged, this, [this](int index){
        // Остались только QR (0) и Files (1).
        if (index < 0 || index > 1)
            return;
        conf()->set(Config::offlineTxSigningMethod, index);
        ui->stackedWidget->setCurrentIndex(index);
        // QR готовим при переключении, иначе после File → QR виджет пуст.
        if (index == 0)
            setupUR(false);
    });
}

void PageOTS_ExportKeyImages::exportKeyImages() {
    QString defaultName = QString("%1_%2").arg(m_wallet->walletName(), QString::number(QDateTime::currentSecsSinceEpoch()));
    QString fn = Utils::getSaveFileName(this, "Save key images to file", defaultName, "Key Images (*_keyImages)");
    if (fn.isEmpty()) {
        return;
    }
    if (!fn.endsWith("_keyImages")) {
        fn += "_keyImages";
    }

    QFile file{fn};
    if (!file.open(QIODevice::WriteOnly)) {
      Utils::showError(this, "Failed to export key images", QString("Could not open file %1 for writing").arg(fn));
      return;
    }

    file.write(m_wizardFields->keyImages.data(), m_wizardFields->keyImages.size());
    file.close();

    QFileInfo fileInfo(fn);
    Utils::openDir(this, "Successfully exported key images", fileInfo.absolutePath());
}

void PageOTS_ExportKeyImages::setupUR(bool all) {
    Q_UNUSED(all);   // key images уже в m_wizardFields->keyImages
    ui->widget_UR->setData("xmr-keyimage", m_wizardFields->keyImages);
}

void PageOTS_ExportKeyImages::initializePage() {
    // Если пришли из HID-режима, conf может содержать 2 — нормализуем.
    int method = conf()->get(Config::offlineTxSigningMethod).toInt();
    if (method > 1)
        method = 1;

    {
        QSignalBlocker blocker(ui->combo_method);
        ui->combo_method->setCurrentIndex(method);
    }
    ui->stackedWidget->setCurrentIndex(method);

    if (method == 0)
        this->setupUR(false);
}

int PageOTS_ExportKeyImages::nextId() const {
    return OfflineTxSigningWizard::Page_ImportUnsignedTx;
}
