// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_OFFLINETXSIGNINGWIZARD_H
#define FEATHER_OFFLINETXSIGNINGWIZARD_H

#include <QWizard>
#include <QFileDialog>

#include "Wallet.h"

#ifdef FEATHER_HAVE_SCANNER
#include "qrcode/scanner/QrCodeScanWidget.h"
#endif

// Зачем открыт мастер. От этого зависит, что делать после синхронизации
// key images по HID: закрыть мастер (SyncOnly) или продолжить подпись
// транзакции (SignAndSend).
enum class OtsMode {
    SyncOnly,      // мастер открыт без PendingTransaction: только синхронизация
    SignAndSend,   // мастер открыт из onTransactionCreated: синхронизация + отправка
};

struct TxWizardFields {
    UnsignedTransaction *utx = nullptr;
    PendingTransaction  *tx = nullptr;
    std::string          signedTx;
#ifdef FEATHER_HAVE_SCANNER
    QrCodeScanWidget    *scanWidget = nullptr;
#endif
    bool                 readyToCommit = false;
    bool                 readyToSign = false;
    std::string          keyImages;

    // Режим мастера, выставляется в конструкторе (tx == nullptr → SyncOnly).
    OtsMode              mode = OtsMode::SyncOnly;

    bool    viaHid        = false;
    qint64  keyImagesSize = 0;
    qint64  signedTxSize  = 0;
    QString hidDeviceName;

    bool isSyncOnly() const { return mode == OtsMode::SyncOnly; }
};

class OfflineTxSigningWizard : public QWizard
{
    Q_OBJECT

public:
    enum Page {
        Page_ExportOutputs = 0,
        Page_ExportKeyImages,
        Page_ImportKeyImages,
        Page_ExportUnsignedTx,
        Page_ImportUnsignedTx,
        Page_SignTx,
        Page_ExportSignedTx,
        Page_ImportSignedTx,
        Page_ImportOffline
    };

    explicit OfflineTxSigningWizard(QWidget *parent, Wallet *wallet, PendingTransaction *tx = nullptr);
    ~OfflineTxSigningWizard() override;

    bool readyToCommit();
    bool readyToSign();
    UnsignedTransaction* unsignedTransaction();
    PendingTransaction* signedTx();

    TxWizardFields& fields() { return m_wizardFields; }
    const TxWizardFields& fields() const { return m_wizardFields; }

    // Отмена: сбрасываем флаги готовности, чтобы закрытие мастера
    // не привело к диалогу отправки после успешной подписи.
    void reject() override;

private:
    Wallet *m_wallet;
    TxWizardFields m_wizardFields;
};

#endif // FEATHER_OFFLINETXSIGNINGWIZARD_H