// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_PAGEOTS_EXPORTOUTPUTS_H
#define FEATHER_PAGEOTS_EXPORTOUTPUTS_H

#include <QWizardPage>
#include <QCheckBox>
#include "Wallet.h"

#ifdef FEATHER_HAVE_HID
#include "HidOperation.h"
#include "MwWallet.h"
#endif

namespace Ui {
    class PageOTS_Export;
}

class PageOTS_ExportOutputs : public QWizardPage
{
    Q_OBJECT

public:
    explicit PageOTS_ExportOutputs(QWidget *parent, Wallet *wallet);
    void initializePage() override;
    void cleanupPage() override;
    [[nodiscard]] int nextId() const override;
    [[nodiscard]] bool isComplete() const override;

private slots:
    void exportOutputs();
    void sendOutputsToHid();

#ifdef FEATHER_HAVE_HID
    void onHidStep(int index, const QString &description);
    void onHidFinished(bool ok, const QString &error);
    void onHidLog(quint8 level, const QString &text);
#endif

private:
    void setupUR(bool all);
    void onMethodChanged(int index);

#ifdef FEATHER_HAVE_HID
    void setHidState(bool busy,
                     const QString &note = {},
                     bool success = false);
    bool isHidMode() const;
    // Мастер открыт только ради синхронизации key images (без PendingTransaction).
    bool isSyncOnlyRun() const;
#endif

    Ui::PageOTS_Export *ui;
    QCheckBox *m_check_exportAll;
    Wallet *m_wallet;

#ifdef FEATHER_HAVE_HID
    MwLink::HidOperation *m_op = nullptr;
    bool                  m_hidBusy = false;
    bool                  m_hidDone = false;
    QString               m_hidError;
    std::string           m_outputsBlob;
#endif
};

#endif // FEATHER_PAGEOTS_EXPORTOUTPUTS_H