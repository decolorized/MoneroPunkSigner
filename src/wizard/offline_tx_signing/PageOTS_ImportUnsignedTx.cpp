// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageOTS_ImportUnsignedTx.h"
#include "ui_PageOTS_Import.h"
#include "OfflineTxSigningWizard.h"

#include <QFileDialog>

#include "dialog/TxConfAdvDialog.h"
#include "utils/config.h"
#include "utils/Icons.h"
#include "utils/Utils.h"

PageOTS_ImportUnsignedTx::PageOTS_ImportUnsignedTx(QWidget *parent, Wallet *wallet, TxWizardFields *wizardFields)
        : PageOTS_Import(parent, wallet, wizardFields, 3, "unsigned transaction", "Transaction (*unsigned_monero_tx)", "Review transaction")
{
}

void PageOTS_ImportUnsignedTx::importFromStr(const std::string &data) {
    UnsignedTransaction *utx = m_wallet->loadUnsignedTransactionFromStr(data);

    if (!utx || utx->status() != UnsignedTransaction::Status_Ok) {
#ifdef FEATHER_HAVE_SCANNER
        m_scanWidget->pause();
#endif
        Utils::showError(this, "Failed to import unsigned transaction", m_wallet->errorString());
#ifdef FEATHER_HAVE_SCANNER
        m_scanWidget->reset();
#endif
        return;
    }

    m_wizardFields->utx = utx;
    m_wizardFields->readyToSign = true;
    PageOTS_Import::onSuccess();
}

int PageOTS_ImportUnsignedTx::nextId() const {
    return -1;
}
