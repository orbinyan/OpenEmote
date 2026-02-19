// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/LoginDialog.hpp"

#include "Application.hpp"
#include "common/Common.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "util/Clipboard.hpp"
#include "util/Helpers.hpp"

#ifdef USEWINSDK
#    include <Windows.h>
#endif

#include <pajlada/settings/setting.hpp>
#include <QClipboard>
#include <QDebug>
#include <QDesktopServices>
#include <QJsonObject>
#include <QMessageBox>
#include <QUrl>

namespace chatterino {

namespace {

QString resolveLoginLink()
{
    // OpenEmote override (preferred), then generic override.
    auto openEmoteLink =
        qEnvironmentVariable("CHATTERINO_OPENEMOTE_LOGIN_URL");
    if (!openEmoteLink.isEmpty())
    {
        return openEmoteLink;
    }

    auto loginLink = qEnvironmentVariable("CHATTERINO_LOGIN_URL");
    if (!loginLink.isEmpty())
    {
        return loginLink;
    }

    return "https://chatterino.com/client_login";
}

QString resolveOpenEmoteOauthBridgeUrl()
{
    auto envBridge =
        qEnvironmentVariable("CHATTERINO_OPENEMOTE_OAUTH_BRIDGE_URL");
    if (!envBridge.isEmpty())
    {
        return envBridge;
    }
    return getSettings()->openEmoteOauthBridgeUrl.getValue().trimmed();
}

bool tryReadCredentialField(const QJsonObject &object,
                            const QStringList &keys, QString &out)
{
    for (const auto &key : keys)
    {
        auto it = object.find(key);
        if (it != object.end() && it->isString())
        {
            const auto value = it->toString().trimmed();
            if (!value.isEmpty())
            {
                out = value;
                return true;
            }
        }
    }
    return false;
}

bool extractCredentialsFromJson(const QJsonObject &root, QString &userID,
                                QString &username, QString &clientID,
                                QString &oauthToken)
{
    const QJsonObject payload =
        root.contains("credentials") && root.value("credentials").isObject()
            ? root.value("credentials").toObject()
        : root.contains("data") && root.value("data").isObject()
            ? root.value("data").toObject()
            : root;

    tryReadCredentialField(payload, {"user_id", "userID", "uid"}, userID);
    tryReadCredentialField(payload, {"username", "login", "user_name"},
                           username);
    tryReadCredentialField(payload, {"client_id", "clientID"}, clientID);
    tryReadCredentialField(payload,
                           {"oauth_token", "oauthToken", "access_token",
                            "token"},
                           oauthToken);

    return !(userID.isEmpty() || username.isEmpty() || clientID.isEmpty() ||
             oauthToken.isEmpty());
}

bool logInWithCredentials(QWidget *parent, const QString &userID,
                          const QString &username, const QString &clientID,
                          const QString &oauthToken)
{
    QStringList errors;

    if (userID.isEmpty())
    {
        errors.append("Missing user ID");
    }
    if (username.isEmpty())
    {
        errors.append("Missing username");
    }
    if (clientID.isEmpty())
    {
        errors.append("Missing Client ID");
    }
    if (oauthToken.isEmpty())
    {
        errors.append("Missing OAuth Token");
    }

    if (errors.length() > 0)
    {
        QMessageBox messageBox(parent);
        messageBox.setWindowTitle("Invalid account credentials");
        messageBox.setIcon(QMessageBox::Critical);
        messageBox.setText(errors.join("<br>"));
        messageBox.exec();
        return false;
    }

    std::string basePath = "/accounts/uid" + userID.toStdString();
    pajlada::Settings::Setting<QString>::set(basePath + "/username", username);
    pajlada::Settings::Setting<QString>::set(basePath + "/userID", userID);
    pajlada::Settings::Setting<QString>::set(basePath + "/clientID", clientID);
    pajlada::Settings::Setting<QString>::set(basePath + "/oauthToken",
                                             oauthToken);

    getApp()->getAccounts()->twitch.reloadUsers();
    getApp()->getAccounts()->twitch.currentUsername = username;
    getSettings()->requestSave();
    return true;
}

}  // namespace

BasicLoginWidget::BasicLoginWidget()
{
    const QString logInLink = resolveLoginLink();
    this->setLayout(&this->ui_.layout);

    this->ui_.loginButton.setText("Log in with Twitch (Opens in browser)");
    this->ui_.secureHandoffButton.setText("Connect from OpenEmote (No paste)");
    this->ui_.pasteCodeButton.setText("Paste login info");
    this->ui_.unableToOpenBrowserHelper.setWindowTitle(
        "Chatterino - unable to open in browser");
    this->ui_.unableToOpenBrowserHelper.setWordWrap(true);
    this->ui_.unableToOpenBrowserHelper.hide();
    this->ui_.unableToOpenBrowserHelper.setText(
        QString("An error occurred while attempting to open <a href=\"%1\">the "
                "log in link (%1)</a> - open it manually in your browser and "
                "proceed from there.")
            .arg(logInLink));
    this->ui_.unableToOpenBrowserHelper.setOpenExternalLinks(true);

    this->ui_.horizontalLayout.addWidget(&this->ui_.loginButton);
    this->ui_.horizontalLayout.addWidget(&this->ui_.secureHandoffButton);
    this->ui_.horizontalLayout.addWidget(&this->ui_.pasteCodeButton);

    this->ui_.layout.addLayout(&this->ui_.horizontalLayout);
    this->ui_.secureHandoffHelper.setWordWrap(true);
    this->ui_.secureHandoffHelper.setText(
        "Recommended for stream safety: complete OAuth in browser, then use "
        "\"Connect from OpenEmote\" so no token copy/paste is shown.");
    this->ui_.layout.addWidget(&this->ui_.secureHandoffHelper);
    this->ui_.layout.addWidget(&this->ui_.unableToOpenBrowserHelper);

    this->ui_.secureHandoffButton.setToolTip(
        "Fetch pending OAuth credentials from local OpenEmote handoff bridge.");

    connect(&this->ui_.loginButton, &QPushButton::clicked, [this, logInLink]() {
        qCDebug(chatterinoWidget) << "open login in browser";
        if (!QDesktopServices::openUrl(QUrl(logInLink)))
        {
            qCWarning(chatterinoWidget) << "open login in browser failed";
            this->ui_.unableToOpenBrowserHelper.show();
        }
    });

    connect(&this->ui_.pasteCodeButton, &QPushButton::clicked, [this]() {
        QStringList parameters = getClipboardText().split(";");
        QString oauthToken, clientID, username, userID;

        // Removing clipboard content to prevent accidental paste of credentials into somewhere
        crossPlatformCopy("");

        for (const auto &param : parameters)
        {
            QStringList kvParameters = param.split('=');
            if (kvParameters.size() != 2)
            {
                continue;
            }
            QString key = kvParameters[0];
            QString value = kvParameters[1];

            if (key == "oauth_token")
            {
                oauthToken = value;
            }
            else if (key == "client_id")
            {
                clientID = value;
            }
            else if (key == "username")
            {
                username = value;
            }
            else if (key == "user_id")
            {
                userID = value;
            }
            else
            {
                qCWarning(chatterinoWidget) << "Unknown key in code: " << key;
            }
        }

        if (logInWithCredentials(this, userID, username, clientID, oauthToken))
        {
            this->window()->close();
        }
    });

    connect(&this->ui_.secureHandoffButton, &QPushButton::clicked, [this]() {
        const auto bridgeUrl = resolveOpenEmoteOauthBridgeUrl();
        if (bridgeUrl.isEmpty())
        {
            QMessageBox::warning(
                this, "OpenEmote handoff not configured",
                "No OAuth handoff bridge URL is configured.");
            return;
        }

        this->ui_.secureHandoffButton.setEnabled(false);
        this->ui_.secureHandoffButton.setText("Connecting...");

        NetworkRequest(QUrl(bridgeUrl), NetworkRequestType::Get)
            .onSuccess([this](const NetworkResult &result) {
                this->ui_.secureHandoffButton.setEnabled(true);
                this->ui_.secureHandoffButton.setText(
                    "Connect from OpenEmote (No paste)");

                const auto payload = result.parseJson();
                QString oauthToken, clientID, username, userID;
                if (!extractCredentialsFromJson(payload, userID, username,
                                                clientID, oauthToken))
                {
                    QMessageBox::information(
                        this, "No pending OAuth handoff",
                        "No complete credentials were found in the handoff "
                        "response. Finish login in browser and try again.");
                    return;
                }

                if (logInWithCredentials(this, userID, username, clientID,
                                         oauthToken))
                {
                    this->window()->close();
                }
            })
            .onError([this](const NetworkResult &result) {
                this->ui_.secureHandoffButton.setEnabled(true);
                this->ui_.secureHandoffButton.setText(
                    "Connect from OpenEmote (No paste)");
                QMessageBox::warning(
                    this, "OpenEmote handoff failed",
                    QString("Unable to fetch OAuth handoff credentials: %1")
                        .arg(result.formatError()));
                return true;
            })
            .execute();
    });

    if (getApp()->getStreamerMode()->isEnabled() &&
        getSettings()->openEmoteHideManualOauthInStreamerMode)
    {
        this->ui_.pasteCodeButton.hide();
    }
}

AdvancedLoginWidget::AdvancedLoginWidget()
{
    this->setLayout(&this->ui_.layout);

    this->ui_.instructionsLabel.setText("1. Fill in your username"
                                        "\n2. Fill in your user ID"
                                        "\n3. Fill in your client ID"
                                        "\n4. Fill in your OAuth token"
                                        "\n5. Press Add user");
    this->ui_.instructionsLabel.setWordWrap(true);

    this->ui_.layout.addWidget(&this->ui_.instructionsLabel);
    this->ui_.layout.addLayout(&this->ui_.formLayout);
    this->ui_.layout.addLayout(&this->ui_.buttonUpperRow.layout);

    this->refreshButtons();

    /// Form
    this->ui_.formLayout.addRow("Username", &this->ui_.usernameInput);
    this->ui_.formLayout.addRow("User ID", &this->ui_.userIDInput);
    this->ui_.formLayout.addRow("Client ID", &this->ui_.clientIDInput);
    this->ui_.formLayout.addRow("OAuth token", &this->ui_.oauthTokenInput);

    this->ui_.oauthTokenInput.setEchoMode(QLineEdit::Password);

    connect(&this->ui_.userIDInput, &QLineEdit::textChanged, [this]() {
        this->refreshButtons();
    });
    connect(&this->ui_.usernameInput, &QLineEdit::textChanged, [this]() {
        this->refreshButtons();
    });
    connect(&this->ui_.clientIDInput, &QLineEdit::textChanged, [this]() {
        this->refreshButtons();
    });
    connect(&this->ui_.oauthTokenInput, &QLineEdit::textChanged, [this]() {
        this->refreshButtons();
    });

    /// Upper button row

    this->ui_.buttonUpperRow.addUserButton.setText("Add user");
    this->ui_.buttonUpperRow.clearFieldsButton.setText("Clear fields");

    this->ui_.buttonUpperRow.layout.addWidget(
        &this->ui_.buttonUpperRow.addUserButton);
    this->ui_.buttonUpperRow.layout.addWidget(
        &this->ui_.buttonUpperRow.clearFieldsButton);

    connect(&this->ui_.buttonUpperRow.clearFieldsButton, &QPushButton::clicked,
            [this]() {
                this->ui_.userIDInput.clear();
                this->ui_.usernameInput.clear();
                this->ui_.clientIDInput.clear();
                this->ui_.oauthTokenInput.clear();
            });

    connect(&this->ui_.buttonUpperRow.addUserButton, &QPushButton::clicked,
            [this]() {
                QString userID = this->ui_.userIDInput.text();
                QString username = this->ui_.usernameInput.text();
                QString clientID = this->ui_.clientIDInput.text();
                QString oauthToken = this->ui_.oauthTokenInput.text();

                logInWithCredentials(this, userID, username, clientID,
                                     oauthToken);
            });
}

void AdvancedLoginWidget::refreshButtons()
{
    if (this->ui_.userIDInput.text().isEmpty() ||
        this->ui_.usernameInput.text().isEmpty() ||
        this->ui_.clientIDInput.text().isEmpty() ||
        this->ui_.oauthTokenInput.text().isEmpty())
    {
        this->ui_.buttonUpperRow.addUserButton.setEnabled(false);
    }
    else
    {
        this->ui_.buttonUpperRow.addUserButton.setEnabled(true);
    }
}

LoginDialog::LoginDialog(QWidget *parent)
    : QDialog(parent)
{
    this->setMinimumWidth(300);
    this->setWindowFlags(
        (this->windowFlags() & ~(Qt::WindowContextHelpButtonHint)) |
        Qt::Dialog | Qt::MSWindowsFixedSizeDialogHint);

    this->setWindowTitle("Add new account");

    this->setLayout(&this->ui_.mainLayout);
    this->ui_.mainLayout.addWidget(&this->ui_.tabWidget);

    this->ui_.tabWidget.addTab(&this->ui_.basic, "Basic");
    this->ui_.tabWidget.addTab(&this->ui_.advanced, "Advanced");

    if (getApp()->getStreamerMode()->isEnabled() &&
        getSettings()->openEmoteHideManualOauthInStreamerMode)
    {
        const auto advancedIndex =
            this->ui_.tabWidget.indexOf(&this->ui_.advanced);
        if (advancedIndex >= 0)
        {
            this->ui_.tabWidget.removeTab(advancedIndex);
        }
    }

    this->ui_.buttonBox.setStandardButtons(QDialogButtonBox::Close);

    QObject::connect(&this->ui_.buttonBox, &QDialogButtonBox::rejected,
                     [this]() {
                         this->close();
                     });

    this->ui_.mainLayout.addWidget(&this->ui_.buttonBox);
}

}  // namespace chatterino
