// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Monero Project

#include "PageWalletRestoreKeys.h"
#include "ui_PageWalletRestoreKeys.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QPlainTextEdit>
#include <QPushButton>

#include "WalletWizard.h"
#include "constants.h"
#include "libwalletqt/WalletManager.h"

#ifdef WITH_SCANNER
#include "scanner/QrCodeScanDialog.h"
#endif

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <memory>

#include "utils/Utils.h"

#ifdef FEATHER_HAVE_HID
#include "HidOperation.h"
#include "MwWallet.h"
#endif

PageWalletRestoreKeys::PageWalletRestoreKeys(WizardFields *fields, QWidget *parent)
    : QWizardPage(parent)
    , ui(new Ui::PageWalletRestoreKeys)
    , m_fields(fields)
{
    ui->setupUi(this);
    this->setTitle("Restore wallet from keys");
    ui->label_errorString->hide();

#ifndef QT_NO_CURSOR
    QGuiApplication::setOverrideCursor(QCursor(Qt::WaitCursor));
    QGuiApplication::restoreOverrideCursor();
#endif

    if (constants::networkType == NetworkType::Type::MAINNET) {
        ui->line_address->setPlaceholderText("4...");
    } else if (constants::networkType == NetworkType::Type::STAGENET) {
        ui->line_address->setPlaceholderText("5...");
    }

    QRegularExpression keyRe(R"([0-9a-fA-F]{64})");
    QValidator *keyValidator = new QRegularExpressionValidator(keyRe, this);

    ui->line_viewkey->setValidator(keyValidator);
    ui->line_spendkey->setValidator(keyValidator);

    connect(ui->btnOptions, &QPushButton::clicked, this, &PageWalletRestoreKeys::onOptionsClicked);
    connect(ui->combo_walletType, &QComboBox::currentTextChanged, this, &PageWalletRestoreKeys::showInputLines);

#ifdef WITH_SCANNER
    connect(ui->btn_scanUR, &QPushButton::clicked, [this] {
        QrCodeScanDialog dialog{this, false};
        dialog.exec();

        QString json = dialog.decodedString();
        if (json.isEmpty()) {
            return;
        }

        QJsonParseError error;
        auto doc = QJsonDocument::fromJson(json.toUtf8(), &error);
        if (error.error != QJsonParseError::NoError) {
            Utils::showError(this, "Unable to load view-only details", QString("Can't parse JSON: %1").arg(error.errorString()));
            return;
        }

        ui->line_address->setText(doc["primaryAddress"].toString());
        ui->line_address->setCursorPosition(0);
        ui->line_viewkey->setText(doc["privateViewKey"].toString());
        ui->line_viewkey->setCursorPosition(0);
        m_fields->restoreHeight = doc["restoreHeight"].toInt();
        m_fields->walletName = doc["walletName"].toString() + "_view_only";
    });
#else
    ui->btn_scanUR->setEnabled(false);
    ui->btn_scanUR->setToolTip("Can't scan QR code: Feather was built without webcam scanner support.");
#endif

    // [ColdPunk] right next to [Scan QR].
    m_btnColdPunk = new QPushButton("ColdPunk", this);
    m_btnColdPunk->setToolTip("Get the primary address and the private view key from the "
                              "ColdPunk device over USB (confirm on the device)");
    ui->horizontalLayout_2->insertWidget(ui->horizontalLayout_2->indexOf(ui->btn_scanUR) + 1,
                                         m_btnColdPunk);
#ifdef FEATHER_HAVE_HID
    connect(m_btnColdPunk, &QPushButton::clicked, this, &PageWalletRestoreKeys::requestFromColdPunk);
#else
    m_btnColdPunk->setEnabled(false);
    m_btnColdPunk->setToolTip("Feather was built without HID support.");
#endif
}

void PageWalletRestoreKeys::setColdPunkBusy(bool busy) {
    m_btnColdPunk->setEnabled(!busy);
    m_btnColdPunk->setText(busy ? "Confirm on device..." : "ColdPunk");
#ifdef WITH_SCANNER
    ui->btn_scanUR->setEnabled(!busy);
#endif
    ui->combo_walletType->setEnabled(!busy);
    if (auto *w = wizard()) {
        if (auto *b = w->button(QWizard::NextButton)) b->setEnabled(!busy);
        if (auto *b = w->button(QWizard::BackButton)) b->setEnabled(!busy);
    }
}

void PageWalletRestoreKeys::requestFromColdPunk() {
#ifdef FEATHER_HAVE_HID
    if (m_hidOp) return;

    // The device sends exactly what a view-only wallet needs.
    if (ui->combo_walletType->currentIndex() != walletType::ViewOnly)
        ui->combo_walletType->setCurrentIndex(walletType::ViewOnly);   // clears the fields

    auto result = std::make_shared<QByteArray>();
    m_hidOp = new MwLink::HidOperation(this);

    // A stale export from an earlier request must not be taken for this one.
    m_hidOp->addStep(QStringLiteral("Preparing device..."),
        [](MwLink::Wallet &dev) { return dev.clear(MwLink::KExport); }, /*critical=*/true);
    m_hidOp->addStep(QStringLiteral("Asking the device..."),
        [](MwLink::Wallet &dev) { return dev.request(MwLink::ReqViewOnly); });
    m_hidOp->addStep(QStringLiteral("Confirm on the device..."),
        [result](MwLink::Wallet &dev) { return dev.waitForRequest(result.get(), 300000); });
    // Whatever happened, the view key must not stay in the device outbox.
    m_hidOp->addCleanup(QStringLiteral("Clearing device state..."),
        [](MwLink::Wallet &dev) { return dev.clear(MwLink::KExport); });

    connect(m_hidOp, &MwLink::HidOperation::finished, this,
            [this, result](bool ok, const QString &err) {
        if (m_hidOp) {
            m_hidOp->deleteLater();
            m_hidOp = nullptr;
        }
        setColdPunkBusy(false);

        QByteArray json = *result;
        result->fill('\0');
        result->clear();
        if (!ok) {
            Utils::showError(this, "Unable to get view-only details from ColdPunk",
                             err.isEmpty() ? QStringLiteral("unknown error") : err);
            return;
        }

        QJsonParseError perr;
        const QJsonDocument doc = QJsonDocument::fromJson(json, &perr);
        json.fill('\0');
        if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
            Utils::showError(this, "Unable to load view-only details",
                             QString("Can't parse the device answer: %1").arg(perr.errorString()));
            return;
        }
        const QJsonObject o = doc.object();
        const QString address = o.value("address").toString();
        const QString viewKey = o.value("view_key").toString();
        if (address.isEmpty() || viewKey.isEmpty()) {
            Utils::showError(this, "Unable to load view-only details",
                             "The device did not send the address and the view key.");
            return;
        }

        // The device wallet must be on the network Feather runs on.
        const QString net = o.value("network").toString();
        QString want = "mainnet";
        if (constants::networkType == NetworkType::Type::STAGENET) want = "stagenet";
        else if (constants::networkType == NetworkType::Type::TESTNET) want = "testnet";
        if (!net.isEmpty() && net != want) {
            Utils::showError(this, "Wrong network",
                             QString("The device wallet is on %1, Feather runs on %2.").arg(net, want));
            return;
        }

        ui->line_address->setText(address);
        ui->line_address->setCursorPosition(0);
        ui->line_viewkey->setText(viewKey);
        ui->line_viewkey->setCursorPosition(0);
        ui->line_address->setStyleSheet("");
        ui->line_viewkey->setStyleSheet("");
        ui->label_errorString->hide();

        const qint64 height = o.value("restore_height").toVariant().toLongLong();
        if (height > 0) m_fields->restoreHeight = int(height);
        const QString name = o.value("wallet").toString();
        if (!name.isEmpty()) m_fields->walletName = name + "_view_only";
    });

    setColdPunkBusy(true);
    if (!m_hidOp->start()) {
        const QString err = m_hidOp->lastError();
        m_hidOp->deleteLater();
        m_hidOp = nullptr;
        setColdPunkBusy(false);
        Utils::showError(this, "Unable to start the HID exchange",
                         err.isEmpty() ? QStringLiteral("unknown error") : err);
    }
#endif
}

void PageWalletRestoreKeys::initializePage() {
    this->showInputLines();
}

int PageWalletRestoreKeys::nextId() const {
    return WalletWizard::Page_SetRestoreHeight;
}

void PageWalletRestoreKeys::showInputLines() {
    ui->label_errorString->hide();

    if (ui->combo_walletType->currentIndex() == walletType::ViewOnly) {
        ui->frame_address->show();
        ui->frame_viewKey->show();
        ui->frame_spendKey->hide();
    }
    else if (ui->combo_walletType->currentIndex() == walletType::Spendable) {
        ui->frame_address->hide();
        ui->frame_viewKey->hide();
        ui->frame_spendKey->show();
    }
    else {
        ui->frame_address->show();
        ui->frame_viewKey->show();
        ui->frame_spendKey->show();
    }

    ui->line_address->setText("");
    ui->line_viewkey->setText("");
    ui->line_spendkey->setText("");
}

bool PageWalletRestoreKeys::validatePage() {
    auto errStyle = "QLineEdit{border: 1px solid red;}";

    ui->line_address->setStyleSheet("");
    ui->line_viewkey->setStyleSheet("");
    ui->label_errorString->hide();

    const QString address = ui->line_address->text().trimmed();
    const QString viewkey = ui->line_viewkey->text().trimmed();
    const QString spendkey = ui->line_spendkey->text().trimmed();

    QStringList errors = {};
    bool hasInvalidInput = false;
    if (walletType() == walletType::Spendable || walletType() == walletType::Spendable_Nondeterministic) {
        if (!ui->line_spendkey->hasAcceptableInput()) {
            hasInvalidInput = true;
            errors.append("invalid spend key");
            ui->line_spendkey->setStyleSheet(errStyle);
        }
    }

    if (walletType() == walletType::ViewOnly || walletType() == walletType::Spendable_Nondeterministic) {
        if (!ui->line_viewkey->hasAcceptableInput()) {
            hasInvalidInput = true;
            errors.append("invalid view key");
            ui->line_viewkey->setStyleSheet(errStyle);
        }

        if (!WalletManager::addressValid(address, constants::networkType)) {
            hasInvalidInput = true;
            errors.append("invalid address");
            ui->line_address->setStyleSheet(errStyle);
        }
    }

    const QString errorString = "Error: " + errors.join(", ");
    if (hasInvalidInput) {
        ui->label_errorString->show();
        ui->label_errorString->setText(errorString);
        return false;
    }

    if (walletType() == walletType::Spendable_Nondeterministic) {
        if (!WalletManager::keyValid(spendkey, address, false, constants::networkType)) {
            Utils::showError(this, "Primary address does not correspond to private spend key.", "Double-check both values.");
            return false;
        }
    }

    if (walletType() == walletType::ViewOnly || walletType() == walletType::Spendable_Nondeterministic) {
        if (!WalletManager::keyValid(viewkey, address, true, constants::networkType)) {
            Utils::showError(this, "Primary address does not correspond to private view key.", "Double-check both values.");
            return false;
        }
    }

    m_fields->address = address;
    m_fields->secretViewKey = viewkey;
    m_fields->secretSpendKey = spendkey;
    return true;
}

void PageWalletRestoreKeys::onOptionsClicked() {
    QDialog dialog(this);
    dialog.setWindowTitle("Options");

    QVBoxLayout layout;
    QCheckBox check_subaddressLookahead("Set subaddress lookahead");
    check_subaddressLookahead.setChecked(m_fields->showSetSubaddressLookaheadPage);

    layout.addWidget(&check_subaddressLookahead);
    QDialogButtonBox buttons(QDialogButtonBox::Ok);
    layout.addWidget(&buttons);
    dialog.setLayout(&layout);
    connect(&buttons, &QDialogButtonBox::accepted, [&dialog]{
        dialog.close();
    });
    dialog.exec();

    m_fields->showSetSubaddressLookaheadPage = check_subaddressLookahead.isChecked();
}

int PageWalletRestoreKeys::walletType() {
    return ui->combo_walletType->currentIndex();
}