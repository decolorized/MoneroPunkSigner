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

// QWizard решает, показывать Next/Commit или Finish, по nextId() — но
// вызывает nextId() только при смене страницы (и в setCommitPage/
// setFinalPage). completeChanged() пересчитывает лишь enabled, а не какая
// кнопка видна. Страница, у которой nextId() зависит от выбранного способа
// обмена (QR / файл / HID), после смены способа должна вызвать эту функцию,
// иначе остаётся кнопка от прежнего способа: Next, ведущий в никуда (HID),
// или Finish, закрывающий мастер вместо перехода к сканированию QR.
inline void otsRefreshWizardButtons(QWizardPage *page)
{
    // setCommitPage() с тем же значением ничего не меняет, кроме того, что
    // QWizard заново спрашивает nextId()/isFinalPage() и обновляет кнопки
    // и их тексты. Для не текущей страницы это no-op.
    if (page)
        page->setCommitPage(page->isCommitPage());
}

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