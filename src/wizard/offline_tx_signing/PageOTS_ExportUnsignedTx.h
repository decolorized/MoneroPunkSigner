// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_PAGEOTS_EXPORTUNSIGNEDTX_H
#define FEATHER_PAGEOTS_EXPORTUNSIGNEDTX_H

#include <QWizardPage>
#include "Wallet.h"
#include "PendingTransaction.h"

#ifdef FEATHER_HAVE_HID
#include "HidOperation.h"
#include "MwWallet.h"
#endif

namespace Ui {
    class PageOTS_Export;
}

class PageOTS_ExportUnsignedTx : public QWizardPage
{
    Q_OBJECT

public:
    explicit PageOTS_ExportUnsignedTx(QWidget *parent, Wallet *wallet, PendingTransaction *tx = nullptr);
    void initializePage() override;
    [[nodiscard]] int nextId() const override;
    [[nodiscard]] bool isComplete() const override;
    bool validatePage() override;

private slots:
    void exportUnsignedTx();
    void signOnHid();

#ifdef FEATHER_HAVE_HID
    void onHidStep(int index, const QString &description);
    void onHidFinished(bool ok, const QString &error);
    void onHidLog(quint8 level, const QString &text);
#endif

private:
    void onMethodChanged(int index);

#ifdef FEATHER_HAVE_HID
    void setHidState(bool busy,
                     const QString &note = {},
                     bool success = false);
    bool isHidMode() const;
#endif

    Ui::PageOTS_Export *ui;
    Wallet *m_wallet;
    PendingTransaction *m_tx;

#ifdef FEATHER_HAVE_HID
    MwLink::HidOperation *m_op = nullptr;
    bool                  m_hidBusy = false;
    bool                  m_hidDone = false;
    // Подписанная транзакция принята от устройства: следующий экран
    // («Signed transaction received») не нужен, Finish завершает мастер.
    bool                  m_signedRxViaHid = false;
    QString               m_hidError;
#endif
};

#endif // FEATHER_PAGEOTS_EXPORTUNSIGNEDTX_H
