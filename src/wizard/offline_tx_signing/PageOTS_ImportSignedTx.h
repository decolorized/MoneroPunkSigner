// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_PAGEOTS_IMPORTSIGNEDTX_H
#define FEATHER_PAGEOTS_IMPORTSIGNEDTX_H

#include <QWizardPage>
#include "Wallet.h"
#include "qrcode/scanner/QrCodeScanWidget.h"
#include "OfflineTxSigningWizard.h"
#include "PageOTS_Import.h"

namespace Ui {
    class PageOTS_Import;
}

class PageOTS_ImportSignedTx : public PageOTS_Import
{
    Q_OBJECT

public:
    explicit PageOTS_ImportSignedTx(QWidget *parent, Wallet *wallet, TxWizardFields *wizardFields);
    [[nodiscard]] int nextId() const override;

    void initializePage() override;

private slots:
    void importFromStr(const std::string &data) override;

private:
    bool validatePage() override;
    bool isHidMode() const;
};

#endif // FEATHER_PAGEOTS_IMPORTSIGNEDTX_H