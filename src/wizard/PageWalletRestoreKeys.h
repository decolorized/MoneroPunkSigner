// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#ifndef FEATHER_WIZARDVIEWONLY_H
#define FEATHER_WIZARDVIEWONLY_H

#include <QWizardPage>

class WizardFields;
class QPushButton;
namespace MwLink { class HidOperation; }

namespace Ui {
    class PageWalletRestoreKeys;
}

class PageWalletRestoreKeys : public QWizardPage
{
    Q_OBJECT

    enum walletType {
        ViewOnly = 0,
        Spendable = 1,
        Spendable_Nondeterministic = 2
    };

public:
    explicit PageWalletRestoreKeys(WizardFields *fields, QWidget *parent = nullptr);
    void initializePage() override;
    bool validatePage() override;
    int nextId() const override;
    void showInputLines();

private:
    void onOptionsClicked();
    int walletType();

    // [ColdPunk]: asks the device (HID) for the primary address and the
    // private view key and fills both fields. The device shows a Yes / No
    // question first; the spend key never leaves it.
    void requestFromColdPunk();
    void setColdPunkBusy(bool busy);
    QPushButton *m_btnColdPunk = nullptr;
    MwLink::HidOperation *m_hidOp = nullptr;

    Ui::PageWalletRestoreKeys *ui;
    WizardFields *m_fields;
};

#endif
