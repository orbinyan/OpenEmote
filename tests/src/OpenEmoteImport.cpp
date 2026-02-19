// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/OpenEmoteImport.hpp"

#include "Test.hpp"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

using namespace chatterino;

namespace {

void writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(contents), contents.size());
}

}  // namespace

TEST(OpenEmoteImport, FindsLegacySettingsDirectories)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("openemote/Settings"));
    ASSERT_TRUE(base.mkpath("chatterino/Settings"));
    ASSERT_TRUE(base.mkpath("Chatterino2/Settings"));
    writeFile(base.filePath("chatterino/Settings/settings.json"), "{}");
    writeFile(base.filePath("Chatterino2/Settings/settings.json"), "{}");

    const auto result = openemote::findLegacySettingsDirectories(
        base.filePath("openemote"), base.filePath("openemote/Settings"));

    EXPECT_TRUE(result.contains(base.filePath("chatterino/Settings")));
    EXPECT_TRUE(result.contains(base.filePath("Chatterino2/Settings")));
    EXPECT_FALSE(result.contains(base.filePath("openemote/Settings")));
}

TEST(OpenEmoteImport, LegacyDirectoryDiscoveryFailsClosedOnEmptyInputs)
{
    const auto result =
        openemote::findLegacySettingsDirectories(QString(), QString());
    EXPECT_TRUE(result.isEmpty());
}

TEST(OpenEmoteImport, IgnoresLegacyDirectoriesWithoutSettingsJson)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("openemote/Settings"));
    ASSERT_TRUE(base.mkpath("chatterino/Settings"));
    ASSERT_TRUE(base.mkpath("Chatterino2/Settings"));
    writeFile(base.filePath("Chatterino2/Settings/settings.json"), "{}");

    const auto result = openemote::findLegacySettingsDirectories(
        base.filePath("openemote"), base.filePath("openemote/Settings"));
    EXPECT_FALSE(result.contains(base.filePath("chatterino/Settings")));
    EXPECT_TRUE(result.contains(base.filePath("Chatterino2/Settings")));
}

TEST(OpenEmoteImport, ImportsSettingsButOnlyOverwritesWindowLayout)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("source"));
    ASSERT_TRUE(base.mkpath("target"));

    const auto sourceDir = base.filePath("source");
    const auto targetDir = base.filePath("target");

    writeFile(QDir(sourceDir).filePath("window-layout.json"),
              "{\"layout\":\"source\"}");
    writeFile(QDir(sourceDir).filePath("settings.json"),
              "{\"settings\":\"source\"}");
    writeFile(QDir(sourceDir).filePath("commands.json"),
              "{\"commands\":\"source\"}");
    writeFile(QDir(targetDir).filePath("window-layout.json"),
              "{\"layout\":\"target\"}");
    writeFile(QDir(targetDir).filePath("settings.json"),
              "{\"settings\":\"target\"}");

    const auto copied =
        openemote::importLegacySettingsFiles(sourceDir, targetDir);

    EXPECT_EQ(copied, 2);

    QFile layoutFile(QDir(targetDir).filePath("window-layout.json"));
    ASSERT_TRUE(layoutFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(layoutFile.readAll(), QByteArray("{\"layout\":\"source\"}"));

    QFile settingsFile(QDir(targetDir).filePath("settings.json"));
    ASSERT_TRUE(settingsFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(settingsFile.readAll(), QByteArray("{\"settings\":\"target\"}"));

    QFile commandsFile(QDir(targetDir).filePath("commands.json"));
    ASSERT_TRUE(commandsFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(commandsFile.readAll(), QByteArray("{\"commands\":\"source\"}"));
}

TEST(OpenEmoteImport, SkipsInvalidWindowLayoutImport)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("source"));
    ASSERT_TRUE(base.mkpath("target"));

    const auto sourceDir = base.filePath("source");
    const auto targetDir = base.filePath("target");

    writeFile(QDir(sourceDir).filePath("window-layout.json"),
              "this-is-not-json");
    writeFile(QDir(targetDir).filePath("window-layout.json"), "target-layout");

    const auto copied =
        openemote::importLegacySettingsFiles(sourceDir, targetDir);
    EXPECT_EQ(copied, 0);

    QFile layoutFile(QDir(targetDir).filePath("window-layout.json"));
    ASSERT_TRUE(layoutFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(layoutFile.readAll(), QByteArray("target-layout"));
}

TEST(OpenEmoteImport, SkipsNonJsonFilesDuringImport)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("source"));
    ASSERT_TRUE(base.mkpath("target"));

    const auto sourceDir = base.filePath("source");
    const auto targetDir = base.filePath("target");

    writeFile(QDir(sourceDir).filePath("custom.dat"), "binary-ish");
    writeFile(QDir(sourceDir).filePath("settings.json"), "{}");

    const auto copied =
        openemote::importLegacySettingsFiles(sourceDir, targetDir);
    EXPECT_EQ(copied, 1);
    EXPECT_FALSE(QFileInfo::exists(QDir(targetDir).filePath("custom.dat")));
}

TEST(OpenEmoteImport, SkipsInvalidJsonFilesDuringImport)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("source"));
    ASSERT_TRUE(base.mkpath("target"));

    const auto sourceDir = base.filePath("source");
    const auto targetDir = base.filePath("target");

    writeFile(QDir(sourceDir).filePath("settings.json"), "{bad-json");
    writeFile(QDir(sourceDir).filePath("window-layout.json"), "{\"layout\":1}");

    const auto copied =
        openemote::importLegacySettingsFiles(sourceDir, targetDir);
    EXPECT_EQ(copied, 1);
    EXPECT_FALSE(QFileInfo::exists(QDir(targetDir).filePath("settings.json")));
    EXPECT_TRUE(
        QFileInfo::exists(QDir(targetDir).filePath("window-layout.json")));
}

TEST(OpenEmoteImport, SkipsTooLargeJsonFilesDuringImport)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("source"));
    ASSERT_TRUE(base.mkpath("target"));

    const auto sourceDir = base.filePath("source");
    const auto targetDir = base.filePath("target");

    QByteArray hugeJson(8 * 1024 * 1024 + 128, 'x');
    hugeJson[0] = '{';
    hugeJson[hugeJson.size() - 1] = '}';
    writeFile(QDir(sourceDir).filePath("settings.json"), hugeJson);

    const auto copied =
        openemote::importLegacySettingsFiles(sourceDir, targetDir);
    EXPECT_EQ(copied, 0);
    EXPECT_FALSE(QFileInfo::exists(QDir(targetDir).filePath("settings.json")));
}

TEST(OpenEmoteImport, CapsImportedFilesPerRun)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("source"));
    ASSERT_TRUE(base.mkpath("target"));

    const auto sourceDir = base.filePath("source");
    const auto targetDir = base.filePath("target");

    for (int i = 0; i < 300; i++)
    {
        writeFile(QDir(sourceDir).filePath(QString("f_%1.json").arg(i)),
                  "{\"ok\":1}");
    }

    const auto copied =
        openemote::importLegacySettingsFiles(sourceDir, targetDir);
    EXPECT_EQ(copied, 256);
}

TEST(OpenEmoteImport, NonJsonFilesDoNotConsumeImportCap)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("source"));
    ASSERT_TRUE(base.mkpath("target"));

    const auto sourceDir = base.filePath("source");
    const auto targetDir = base.filePath("target");

    for (int i = 0; i < 400; i++)
    {
        writeFile(QDir(sourceDir).filePath(QString("junk_%1.txt").arg(i)),
                  "junk");
    }
    for (int i = 0; i < 10; i++)
    {
        writeFile(QDir(sourceDir).filePath(QString("ok_%1.json").arg(i)),
                  "{\"ok\":1}");
    }

    const auto copied =
        openemote::importLegacySettingsFiles(sourceDir, targetDir);
    EXPECT_EQ(copied, 10);
}

TEST(OpenEmoteImport, ReturnsZeroWhenSourceDirectoryMissing)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("target"));

    const auto copied = openemote::importLegacySettingsFiles(
        base.filePath("does-not-exist"), base.filePath("target"));
    EXPECT_EQ(copied, 0);
}

TEST(OpenEmoteImport, NoopWhenSourceAndTargetAreSameDirectory)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QDir base(root.path());
    ASSERT_TRUE(base.mkpath("settings"));

    const auto dir = base.filePath("settings");
    writeFile(QDir(dir).filePath("window-layout.json"), "{\"layout\":1}");

    const auto copied = openemote::importLegacySettingsFiles(dir, dir);
    EXPECT_EQ(copied, 0);
}

TEST(OpenEmoteImport, CountsOnlyCompleteLegacyTwitchAccounts)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QJsonObject validAccount;
    validAccount.insert("username", "orbinyan");
    validAccount.insert("userID", "123");
    validAccount.insert("clientID", "abc");
    validAccount.insert("oauthToken", "oauth:token");

    QJsonObject invalidAccount;
    invalidAccount.insert("username", "missingtoken");
    invalidAccount.insert("userID", "124");
    invalidAccount.insert("clientID", "abc");

    QJsonObject accounts;
    accounts.insert("current", "orbinyan");
    accounts.insert("uid123", validAccount);
    accounts.insert("uid124", invalidAccount);

    QJsonObject rootObj;
    rootObj.insert("accounts", accounts);

    writeFile(QDir(root.path()).filePath("settings.json"),
              QJsonDocument(rootObj).toJson(QJsonDocument::Compact));

    EXPECT_EQ(openemote::countLegacyTwitchAccounts(root.path()), 1);
}

TEST(OpenEmoteImport, ReturnsZeroForInvalidSettingsJson)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());
    writeFile(QDir(root.path()).filePath("settings.json"), "{broken-json");

    EXPECT_EQ(openemote::countLegacyTwitchAccounts(root.path()), 0);
}

TEST(OpenEmoteImport, ReturnsZeroForOversizedSettingsJson)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QByteArray hugePayload(4 * 1024 * 1024 + 16, 'x');
    writeFile(QDir(root.path()).filePath("settings.json"), hugePayload);

    EXPECT_EQ(openemote::countLegacyTwitchAccounts(root.path()), 0);
}

TEST(OpenEmoteImport, LoadsLegacyAccountsPayloadAndDeduplicatesUserIds)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QJsonObject accountA;
    accountA.insert("username", "orbinyan");
    accountA.insert("userID", "123");
    accountA.insert("clientID", "a");
    accountA.insert("oauthToken", "oauth:a");

    QJsonObject accountADuplicate;
    accountADuplicate.insert("username", "orbinyan2");
    accountADuplicate.insert("userID", "123");
    accountADuplicate.insert("clientID", "b");
    accountADuplicate.insert("oauthToken", "oauth:b");

    QJsonObject accountB;
    accountB.insert("username", "mod_user");
    accountB.insert("userID", "124");
    accountB.insert("clientID", "c");
    accountB.insert("oauthToken", "oauth:c");

    QJsonObject accounts;
    accounts.insert("current", " orbinyan ");
    accounts.insert("uid123", accountA);
    accounts.insert("uid123dup", accountADuplicate);
    accounts.insert("uid124", accountB);

    QJsonObject rootObj;
    rootObj.insert("accounts", accounts);

    writeFile(QDir(root.path()).filePath("settings.json"),
              QJsonDocument(rootObj).toJson(QJsonDocument::Compact));

    const auto payload = openemote::loadLegacyTwitchAccounts(root.path());
    EXPECT_EQ(payload.currentUsername, "orbinyan");
    EXPECT_EQ(payload.accounts.size(), 2);
}

TEST(OpenEmoteImport, LegacyAccountsPayloadIsEmptyForWrongAccountsType)
{
    QTemporaryDir root;
    ASSERT_TRUE(root.isValid());

    QJsonObject rootObj;
    rootObj.insert("accounts", QJsonArray{});
    writeFile(QDir(root.path()).filePath("settings.json"),
              QJsonDocument(rootObj).toJson(QJsonDocument::Compact));

    const auto payload = openemote::loadLegacyTwitchAccounts(root.path());
    EXPECT_TRUE(payload.currentUsername.isEmpty());
    EXPECT_TRUE(payload.accounts.empty());
}

TEST(OpenEmoteImport, PicksLegacyCurrentWhenImported)
{
    const QStringList imported{"alpha", "orbinyan", "moduser"};
    const auto selected = openemote::pickImportedCurrentUsername(
        "OrBiNyAn", imported, QString());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value(), "OrBiNyAn");
}

TEST(OpenEmoteImport, LegacyCurrentOverridesExistingWhenImported)
{
    const QStringList imported{"alpha", "orbinyan"};
    const auto selected = openemote::pickImportedCurrentUsername(
        "orbinyan", imported, "already-set");
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value(), "orbinyan");
}

TEST(OpenEmoteImport, KeepsExistingCurrentWhenLegacyCurrentMissing)
{
    const QStringList imported{"alpha", "orbinyan", "moduser"};
    const auto selected = openemote::pickImportedCurrentUsername(
        "missing", imported, "already-set");
    EXPECT_FALSE(selected.has_value());
}

TEST(OpenEmoteImport, PicksFirstImportedWhenNoCurrentExists)
{
    const QStringList imported{"alpha", "orbinyan"};
    const auto selected = openemote::pickImportedCurrentUsername(
        QString(), imported, QString());
    ASSERT_TRUE(selected.has_value());
    EXPECT_EQ(selected.value(), "alpha");
}

TEST(OpenEmoteImport, ReturnsEmptyWhenNoImportedAccountsExist)
{
    const auto selected = openemote::pickImportedCurrentUsername(
        "orbinyan", QStringList{}, QString());
    EXPECT_FALSE(selected.has_value());
}
