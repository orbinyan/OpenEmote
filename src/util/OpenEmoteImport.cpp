// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/OpenEmoteImport.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>

#include <utility>

namespace chatterino::openemote {

namespace {

constexpr qint64 MAX_LEGACY_SETTINGS_JSON_BYTES = 4 * 1024 * 1024;
constexpr qint64 MAX_IMPORTED_JSON_FILE_BYTES = 8 * 1024 * 1024;
constexpr int MAX_IMPORTED_SETTINGS_FILES = 256;

QJsonObject loadLegacySettingsObject(const QString &sourceDir)
{
    QFile sourceSettingsFile(QDir(sourceDir).filePath("settings.json"));
    if (!sourceSettingsFile.open(QIODevice::ReadOnly))
    {
        return {};
    }

    if (sourceSettingsFile.size() < 0 ||
        sourceSettingsFile.size() > MAX_LEGACY_SETTINGS_JSON_BYTES)
    {
        return {};
    }

    QJsonParseError parseError;
    const auto parsed = QJsonDocument::fromJson(sourceSettingsFile.readAll(),
                                                &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        return {};
    }

    if (!parsed.isObject())
    {
        return {};
    }

    return parsed.object();
}

}  // namespace

QStringList findLegacySettingsDirectories(const QString &rootAppDataDirectory,
                                          const QString &currentSettingsDirectory)
{
    if (rootAppDataDirectory.trimmed().isEmpty() ||
        currentSettingsDirectory.trimmed().isEmpty())
    {
        return {};
    }

    QStringList candidates;
    QSet<QString> seenCanonicalPaths;

    const auto currentSettings =
        QDir::cleanPath(QDir(currentSettingsDirectory).absolutePath());
    QDir parent(rootAppDataDirectory);
    if (!parent.exists())
    {
        return {};
    }
    parent.cdUp();

    const QStringList directoryNames{
        "chatterino",
        "chatterino2",
        "Chatterino",
        "Chatterino2",
    };

    for (const auto &directoryName : directoryNames)
    {
        const auto candidate = QDir::cleanPath(
            parent.filePath(directoryName + "/Settings"));
        const auto settingsFile = QDir(candidate).filePath("settings.json");
        if (candidate == currentSettings || !QDir(candidate).exists() ||
            !QFileInfo::exists(settingsFile))
        {
            continue;
        }

        QString canonical = QFileInfo(candidate).canonicalFilePath();
        if (canonical.isEmpty())
        {
            canonical = QDir::cleanPath(QDir(candidate).absolutePath());
        }

        if (!seenCanonicalPaths.contains(canonical))
        {
            seenCanonicalPaths.insert(canonical);
            candidates.push_back(canonical);
        }
    }

    return candidates;
}

int importLegacySettingsFiles(const QString &sourceDir,
                              const QString &targetDir)
{
    QDir source(sourceDir);
    QDir target(targetDir);
    if (QDir::cleanPath(source.absolutePath()) ==
        QDir::cleanPath(target.absolutePath()))
    {
        return 0;
    }
    if (!source.exists())
    {
        return 0;
    }
    if (!target.exists() && !QDir().mkpath(targetDir))
    {
        return 0;
    }

    int copied = 0;
    int considered = 0;
    const auto files =
        source.entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const auto &file : files)
    {
        if (!file.fileName().endsWith(".json", Qt::CaseInsensitive))
        {
            continue;
        }
        if (!file.isFile() || file.isSymLink())
        {
            continue;
        }
        if (file.size() < 0 || file.size() > MAX_IMPORTED_JSON_FILE_BYTES)
        {
            continue;
        }
        if (considered >= MAX_IMPORTED_SETTINGS_FILES)
        {
            break;
        }
        considered++;

        const auto destination = target.filePath(file.fileName());
        const bool overwriteExisting = file.fileName().compare(
                                           "window-layout.json",
                                           Qt::CaseInsensitive) == 0;

        QFile sourceJson(file.absoluteFilePath());
        if (!sourceJson.open(QIODevice::ReadOnly))
        {
            continue;
        }

        QJsonParseError parseError;
        const auto parsed =
            QJsonDocument::fromJson(sourceJson.readAll(), &parseError);
        if (parseError.error != QJsonParseError::NoError ||
            (parsed.isNull() || (!parsed.isObject() && !parsed.isArray())))
        {
            continue;
        }

        if (QFileInfo::exists(destination) && !overwriteExisting)
        {
            continue;
        }

        if (overwriteExisting)
        {
            QFile::remove(destination);
        }

        if (QFile::copy(file.absoluteFilePath(), destination))
        {
            copied++;
        }
    }

    return copied;
}

LegacyTwitchAccountsPayload loadLegacyTwitchAccounts(const QString &sourceDir)
{
    LegacyTwitchAccountsPayload payload;

    const auto root = loadLegacySettingsObject(sourceDir);
    const auto accountsValue = root.value("accounts");
    if (!accountsValue.isObject())
    {
        return payload;
    }

    const auto accounts = accountsValue.toObject();
    payload.currentUsername = accounts.value("current").toString().trimmed();

    QSet<QString> seenUserIDs;
    for (auto it = accounts.constBegin(); it != accounts.constEnd(); ++it)
    {
        if (it.key() == "current" || !it.value().isObject())
        {
            continue;
        }

        const auto account = it.value().toObject();
        LegacyTwitchAccount parsed{
            .username = account.value("username").toString().trimmed(),
            .userID = account.value("userID").toString().trimmed(),
            .clientID = account.value("clientID").toString().trimmed(),
            .oauthToken = account.value("oauthToken").toString().trimmed(),
        };

        if (parsed.username.isEmpty() || parsed.userID.isEmpty() ||
            parsed.clientID.isEmpty() || parsed.oauthToken.isEmpty())
        {
            continue;
        }

        if (seenUserIDs.contains(parsed.userID))
        {
            continue;
        }
        seenUserIDs.insert(parsed.userID);
        payload.accounts.emplace_back(std::move(parsed));
    }

    return payload;
}

std::optional<QString> pickImportedCurrentUsername(
    const QString &legacyCurrentUsername, const QStringList &importedUsernames,
    const QString &existingCurrentUsername)
{
    if (!legacyCurrentUsername.isEmpty() &&
        importedUsernames.contains(legacyCurrentUsername, Qt::CaseInsensitive))
    {
        return legacyCurrentUsername;
    }

    if (!existingCurrentUsername.isEmpty())
    {
        return std::nullopt;
    }

    if (importedUsernames.isEmpty())
    {
        return std::nullopt;
    }

    return importedUsernames.front();
}

int countLegacyTwitchAccounts(const QString &sourceDir)
{
    return static_cast<int>(loadLegacyTwitchAccounts(sourceDir).accounts.size());
}

}  // namespace chatterino::openemote
