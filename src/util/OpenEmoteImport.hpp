// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QStringList>

#include <optional>
#include <vector>

namespace chatterino::openemote {

struct LegacyTwitchAccount
{
    QString username;
    QString userID;
    QString clientID;
    QString oauthToken;
};

struct LegacyTwitchAccountsPayload
{
    QString currentUsername;
    std::vector<LegacyTwitchAccount> accounts;
};

QStringList findLegacySettingsDirectories(const QString &rootAppDataDirectory,
                                          const QString &currentSettingsDirectory);

int importLegacySettingsFiles(const QString &sourceDir,
                              const QString &targetDir);

LegacyTwitchAccountsPayload loadLegacyTwitchAccounts(const QString &sourceDir);

std::optional<QString> pickImportedCurrentUsername(
    const QString &legacyCurrentUsername, const QStringList &importedUsernames,
    const QString &existingCurrentUsername);

int countLegacyTwitchAccounts(const QString &sourceDir);

}  // namespace chatterino::openemote
