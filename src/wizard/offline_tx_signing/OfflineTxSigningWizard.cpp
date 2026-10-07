// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "OfflineTxSigningWizard.h"

#include "PageOTS_ExportOutputs.h"
#include "PageOTS_ImportKeyImages.h"
#include "PageOTS_ExportUnsignedTx.h"
#include "PageOTS_ExportSignedTx.h"

#include "PageOTS_ImportOffline.h"
#include "PageOTS_ExportKeyImages.h"
#include "PageOTS_ImportUnsignedTx.h"
#include "PageOTS_SignTx.h"
#include "PageOTS_ImportSignedTx.h"

#include <QApplication>
#include <QLayout>
#include <QScreen>
#include <QPushButton>

#include "utils/config.h"

void OfflineTxSigningWizard::reject() {
    // После успешной подписи закрытие мастера не должно вести к диалогу отправки:
    // сбрасываем флаги готовности, чтобы вызывающий код не воспринял
    // закрытие как подтверждение.
    m_wizardFields.readyToCommit = false;
    m_wizardFields.readyToSign = false;
    QWizard::reject();
}

QPushButton *OfflineTxSigningWizard::actionButton(QWizardPage *page, QLayout *parentLayout)
{
    if (!page || !parentLayout)
        return nullptr;

    // Кнопка переживает повторные входы на страницу, поэтому создаём один раз.
    QPushButton *button = page->findChild<QPushButton *>(QStringLiteral("ots_action_button"));
    if (!button) {
        button = new QPushButton(page);
        button->setObjectName(QStringLiteral("ots_action_button"));
        button->setVisible(false);
        parentLayout->addWidget(button);

        // Действие подключает страница в updateActionButton(): только она
        // знает, принять мастер (accept) или идти дальше (next).
    }
    return button;
}

void OfflineTxSigningWizard::setActionButtonVisible(QPushButton *button, bool visible,
                                                    const QString &text)
{
    if (!button)
        return;

    if (visible)
        button->setText(text);

    button->setVisible(visible);
    button->setEnabled(visible);

    // Пока видна своя кнопка, штатные Next/Finish прячем — иначе внизу
    // окажутся две кнопки, ведущие себя по-разному.
    if (auto *b = this->button(QWizard::NextButton))
        b->setVisible(!visible);
    if (auto *b = this->button(QWizard::FinishButton))
        b->setVisible(!visible);

    // Текущая страница могла отключить кнопку в _q_updateButtonStates(),
    // поэтому при возврате штатных кнопок пересчитываем их состояние
    // (полезной нагрузки у сигнала нет, только пересчёт кнопок).
    if (!visible && currentPage())
        QMetaObject::invokeMethod(currentPage(), "completeChanged");
}

OfflineTxSigningWizard::OfflineTxSigningWizard(QWidget *parent, Wallet *wallet, PendingTransaction *tx)
    : QWizard(parent)
    , m_wallet(wallet)
{
    // Режим определяется точкой входа: onTransactionCreated() передаёт
    // PendingTransaction (синхронизация + отправка), showKeyImageSyncWizard()
    // и офлайн-кошелёк — нет (только синхронизация).
    m_wizardFields.mode = (tx == nullptr) ? OtsMode::SyncOnly : OtsMode::SignAndSend;

#ifdef FEATHER_HAVE_SCANNER
    m_wizardFields.scanWidget = new QrCodeScanWidget(nullptr);
#endif

    // View-only
    setPage(Page_ExportOutputs, new PageOTS_ExportOutputs(this, m_wallet));
    setPage(Page_ImportKeyImages, new PageOTS_ImportKeyImages(this, m_wallet, &m_wizardFields));
    setPage(Page_ExportUnsignedTx, new PageOTS_ExportUnsignedTx(this, m_wallet, tx));
    setPage(Page_ImportSignedTx, new PageOTS_ImportSignedTx(this, m_wallet, &m_wizardFields));

    // Offline
    setPage(Page_ImportOffline, new PageOTS_ImportOffline(this, m_wallet, &m_wizardFields));
    setPage(Page_ExportKeyImages, new PageOTS_ExportKeyImages(this, m_wallet, &m_wizardFields));
    setPage(Page_ImportUnsignedTx, new PageOTS_ImportUnsignedTx(this, m_wallet, &m_wizardFields));
    setPage(Page_SignTx, new PageOTS_SignTx(this));
    setPage(Page_ExportSignedTx, new PageOTS_ExportSignedTx(this, m_wallet, &m_wizardFields));

    if (tx) {
        setStartId(Page_ExportUnsignedTx);
    } else {
        setStartId(m_wallet->viewOnly() ? Page_ExportOutputs : Page_ImportOffline);
    }

    this->setWindowTitle("Offline transaction signing");

    QList<QWizard::WizardButton> layout;
    layout << QWizard::CancelButton;
    layout << QWizard::Stretch;
    layout << QWizard::BackButton;
    layout << QWizard::NextButton;
    layout << QWizard::FinishButton;
    layout << QWizard::CommitButton;
    this->setButtonLayout(layout);

    // setOption(QWizard::HaveCustomButton1, true);
    setOption(QWizard::NoBackButtonOnStartPage);
    setWizardStyle(WizardStyle::ModernStyle);

    bool geo = this->restoreGeometry(QByteArray::fromBase64(conf()->get(Config::geometryOTSWizard).toByteArray()));
    if (!geo) {
        QScreen *currentScreen = QApplication::screenAt(this->geometry().center());
        if (!currentScreen) {
            currentScreen = QApplication::primaryScreen();
        }
        int availableHeight = currentScreen->availableGeometry().height() - 100;

        this->resize(availableHeight, availableHeight);
    }

    // Anti-glare
    // QFile f(":qdarkstyle/style.qss");
    // if (!f.exists()) {
    //     printf("Unable to set stylesheet, file not found\n");
    //     f.close();
    // } else {
    //     f.open(QFile::ReadOnly | QFile::Text);
    //     QTextStream ts(&f);
    //     QString qdarkstyle = ts.readAll();
    //     this->setStyleSheet(qdarkstyle);
    // }
}

bool OfflineTxSigningWizard::readyToCommit() {
    return m_wizardFields.readyToCommit;
}

bool OfflineTxSigningWizard::readyToSign() {
    return m_wizardFields.readyToSign;
}

UnsignedTransaction* OfflineTxSigningWizard::unsignedTransaction() {
    return m_wizardFields.utx;
}

PendingTransaction* OfflineTxSigningWizard::signedTx() {
    return m_wizardFields.tx;
}

OfflineTxSigningWizard::~OfflineTxSigningWizard() {
    conf()->set(Config::geometryOTSWizard, QString(saveGeometry().toBase64()));
}
