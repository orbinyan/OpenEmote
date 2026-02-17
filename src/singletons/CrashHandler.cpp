// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/CrashHandler.hpp"

#include "common/Args.hpp"
#include "common/Literals.hpp"
#include "common/QLogging.hpp"
#include "singletons/Paths.hpp"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#ifdef CHATTERINO_WITH_CRASHPAD
#    include <QApplication>
#    include <QDateTime>
#    include <client/crash_report_database.h>
#    include <client/settings.h>

#    include <memory>
#    include <string>
#endif

namespace {

using namespace chatterino;
using namespace literals;

/// The name of the crashpad handler executable.
/// This varies across platforms
#if defined(Q_OS_UNIX)
const QString CRASHPAD_EXECUTABLE_NAME = QStringLiteral("crashpad-handler");
#elif defined(Q_OS_WINDOWS)
const QString CRASHPAD_EXECUTABLE_NAME = QStringLiteral("crashpad-handler.exe");
#else
#    error Unsupported platform
#endif

/// Converts a QString into the platform string representation.
#if defined(Q_OS_UNIX)
[[maybe_unused]] std::string nativeString(const QString &s)
{
    return s.toStdString();
}
#elif defined(Q_OS_WINDOWS)
[[maybe_unused]] std::wstring nativeString(const QString &s)
{
    return s.toStdWString();
}
#else
#    error Unsupported platform
#endif

const QString RECOVERY_FILE = u"chatterino-recovery.json"_s;
constexpr const char *CRASH_UPLOAD_URL_ENV = "OPENEMOTE_CRASH_UPLOAD_URL";
constexpr const char *CRASH_UPLOAD_DEV_ENV = "OPENEMOTE_DEV_CRASH_REPORTS";
const QString DEFAULT_CRASH_UPLOAD_URL = u"https://openemote.com/crash"_s;

struct RecoverySettings {
    bool shouldRecover = false;
    bool shouldUploadCrashReports = false;
};

QJsonObject readRecoverySettingsObject(const Paths &paths)
{
    QFile file(QDir(paths.crashdumpDirectory).filePath(RECOVERY_FILE));
    if (!file.open(QFile::ReadOnly))
    {
        return {};
    }

    QJsonParseError error{};
    auto doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
    {
        qCWarning(chatterinoCrashhandler)
            << "Failed to parse recovery settings" << error.errorString();
        return {};
    }

    return doc.object();
}

/// The recovery options are saved outside the settings
/// to be able to read them without loading the settings.
///
/// The flags are saved in the `RECOVERY_FILE` as JSON.
std::optional<RecoverySettings> readRecoverySettings(const Paths &paths)
{
    const auto obj = readRecoverySettingsObject(paths);
    const auto shouldRecover = obj["shouldRecover"_L1];
    if (!shouldRecover.isBool())
    {
        return std::nullopt;
    }

    RecoverySettings settings;
    settings.shouldRecover = shouldRecover.toBool();
    settings.shouldUploadCrashReports =
        obj["shouldUploadCrashReports"_L1].toBool(false);
    return settings;
}

bool writeRecoverySettings(const Paths &paths, const RecoverySettings &settings)
{
    QFile file(QDir(paths.crashdumpDirectory).filePath(RECOVERY_FILE));
    if (!file.open(QFile::WriteOnly | QFile::Truncate))
    {
        qCWarning(chatterinoCrashhandler)
            << "Failed to open" << file.fileName();
        return false;
    }

    file.write(QJsonDocument(QJsonObject{
                                 {"shouldRecover"_L1, settings.shouldRecover},
                                 {"shouldUploadCrashReports"_L1,
                                  settings.shouldUploadCrashReports},
                             })
                   .toJson(QJsonDocument::Compact));
    return true;
}

[[maybe_unused]] bool canRestart(const Paths &paths,
                                 [[maybe_unused]] const Args &args)
{
#ifdef NDEBUG
    if (args.isFramelessEmbed || args.shouldRunBrowserExtensionHost)
    {
        return false;
    }

    auto settings = readRecoverySettings(paths);
    if (!settings)
    {
        return false;  // default, no settings found
    }
    return settings->shouldRecover;
#else
    (void)paths;
    return false;
#endif
}

/// This encodes the arguments into a single string.
///
/// The command line arguments are joined by '+'. A plus is escaped by an
/// additional plus ('++' -> '+').
///
/// The decoding happens in crash-handler/src/CommandLine.cpp
[[maybe_unused]] std::string encodeArguments(const Args &appArgs)
{
    std::string args;
    for (auto arg : appArgs.currentArguments())
    {
        if (!args.empty())
        {
            args.push_back('+');
        }
        args += arg.replace(u'+', u"++"_s).toStdString();
    }
    return args;
}

}  // namespace

namespace chatterino {

using namespace std::string_literals;

CrashHandler::CrashHandler(const Paths &paths_)
    : paths(paths_)
{
    auto optSettings = readRecoverySettings(this->paths);
    if (optSettings)
    {
        this->shouldRecover_ = optSettings->shouldRecover;
        this->shouldUploadCrashReports_ = optSettings->shouldUploadCrashReports;
    }
    else
    {
        // By default, we don't restart after a crash.
        this->saveShouldRecover(false);
        this->saveShouldUploadCrashReports(false);
    }

    if (CrashHandler::isCrashUploadForcedInDevMode())
    {
        this->shouldUploadCrashReports_ = true;
    }
}

void CrashHandler::saveShouldRecover(bool value)
{
    this->shouldRecover_ = value;
    writeRecoverySettings(this->paths, RecoverySettings{
                                           .shouldRecover = this->shouldRecover_,
                                           .shouldUploadCrashReports =
                                               this->shouldUploadCrashReports_,
                                       });
}

void CrashHandler::saveShouldUploadCrashReports(bool value)
{
    this->shouldUploadCrashReports_ = value;
    writeRecoverySettings(this->paths, RecoverySettings{
                                           .shouldRecover = this->shouldRecover_,
                                           .shouldUploadCrashReports =
                                               this->shouldUploadCrashReports_,
                                       });
}

bool CrashHandler::isCrashUploadForcedInDevMode()
{
#ifndef NDEBUG
    const auto env = qEnvironmentVariable(CRASH_UPLOAD_DEV_ENV);
    return env.isEmpty() || env != "0";
#else
    return qEnvironmentVariableIntValue(CRASH_UPLOAD_DEV_ENV) == 1;
#endif
}

bool CrashHandler::hasCrashUploadUrlOverride()
{
    return qEnvironmentVariableIsSet(CRASH_UPLOAD_URL_ENV) &&
           !qEnvironmentVariable(CRASH_UPLOAD_URL_ENV).trimmed().isEmpty();
}

bool CrashHandler::shouldUploadCrashReportsAtRuntime(
    bool persistedUserPreference)
{
    if (CrashHandler::isCrashUploadForcedInDevMode())
    {
        return CrashHandler::hasCrashUploadUrlOverride();
    }
    return persistedUserPreference;
}

QString CrashHandler::crashUploadUrlForRuntime()
{
    if (CrashHandler::isCrashUploadForcedInDevMode() &&
        !CrashHandler::hasCrashUploadUrlOverride())
    {
        return QString();
    }
    return CrashHandler::crashUploadUrl();
}

QString CrashHandler::crashUploadUrl()
{
    const auto envValue = qEnvironmentVariable(CRASH_UPLOAD_URL_ENV);
    return envValue.isEmpty() ? DEFAULT_CRASH_UPLOAD_URL : envValue;
}

bool CrashHandler::loadShouldUploadCrashReports(const Paths &paths)
{
    auto settings = readRecoverySettings(paths);
    return settings ? settings->shouldUploadCrashReports : false;
}

void CrashHandler::saveShouldUploadCrashReports(const Paths &paths, bool enabled)
{
    auto settings = readRecoverySettings(paths).value_or(RecoverySettings{});
    settings.shouldUploadCrashReports = enabled;
    writeRecoverySettings(paths, settings);
}

bool CrashHandler::applyCrashUploadPreference(const Paths &paths, bool enabled)
{
#ifdef CHATTERINO_WITH_CRASHPAD
    auto databaseDir = base::FilePath(nativeString(paths.crashdumpDirectory));
    auto database = crashpad::CrashReportDatabase::Initialize(databaseDir);
    if (!database)
    {
        return false;
    }

    auto *settings = database->GetSettings();
    if (settings == nullptr)
    {
        return false;
    }

    return settings->SetUploadsEnabled(enabled);
#else
    (void)paths;
    (void)enabled;
    return false;
#endif
}

#ifdef CHATTERINO_WITH_CRASHPAD
std::unique_ptr<crashpad::CrashpadClient> installCrashHandler(
    const Args &args, const Paths &paths)
{
    // Currently, the following directory layout is assumed:
    // [applicationDirPath]
    //  ├─chatterino(.exe)
    //  ╰─[crashpad]
    //     ╰─crashpad-handler(.exe)
    // TODO: The location of the binary might vary across platforms
    auto crashpadBinDir = QDir(QApplication::applicationDirPath());

    if (!crashpadBinDir.cd("crashpad"))
    {
        qCDebug(chatterinoCrashhandler) << "Cannot find crashpad directory";
        return nullptr;
    }
    if (!crashpadBinDir.exists(CRASHPAD_EXECUTABLE_NAME))
    {
        qCDebug(chatterinoCrashhandler)
            << "Cannot find crashpad handler executable";
        return nullptr;
    }

    auto handlerPath = base::FilePath(nativeString(
        crashpadBinDir.absoluteFilePath(CRASHPAD_EXECUTABLE_NAME)));

    // Argument passed in --database
    // > Crash reports are written to this database, and if uploads are enabled,
    //   uploaded from this database to a crash report collection server.
    auto databaseDir = base::FilePath(nativeString(paths.crashdumpDirectory));
    const auto persistedPref = CrashHandler::loadShouldUploadCrashReports(paths);
    const auto uploadEnabled =
        CrashHandler::shouldUploadCrashReportsAtRuntime(persistedPref);
    const auto uploadUrlQString = CrashHandler::crashUploadUrlForRuntime();

    if (uploadEnabled && !uploadUrlQString.isEmpty())
    {
        qCInfo(chatterinoCrashhandler)
            << "Crash upload mode: enabled,"
            << "url =" << uploadUrlQString;
    }
    else if (CrashHandler::isCrashUploadForcedInDevMode())
    {
        qCInfo(chatterinoCrashhandler)
            << "Crash upload mode: dev-local-only (web upload disabled)";
    }
    else
    {
        qCInfo(chatterinoCrashhandler)
            << "Crash upload mode: disabled";
    }

    CrashHandler::applyCrashUploadPreference(paths, uploadEnabled);

    auto client = std::make_unique<crashpad::CrashpadClient>();

    std::map<std::string, std::string> annotations{
        {
            "canRestart"s,
            canRestart(paths, args) ? "true"s : "false"s,
        },
        {
            "exePath"s,
            QApplication::applicationFilePath().toStdString(),
        },
        {
            "startedAt"s,
            QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString(),
        },
        {
            "exeArguments"s,
            encodeArguments(args),
        },
        {
            "openemoteCrashUploadEnabled"s,
            uploadEnabled ? "true"s : "false"s,
        },
        {
            "openemoteCrashUploadDevLocalOnly"s,
            (CrashHandler::isCrashUploadForcedInDevMode() &&
             !CrashHandler::hasCrashUploadUrlOverride())
                ? "true"s
                : "false"s,
        },
    };

    // See https://chromium.googlesource.com/crashpad/crashpad/+/HEAD/handler/crashpad_handler.md
    // for documentation on available options.
    if (!client->StartHandler(handlerPath, databaseDir, {},
                              uploadUrlQString.toStdString(), {}, annotations,
                              {}, true, false))
    {
        qCDebug(chatterinoCrashhandler) << "Failed to start crashpad handler";
        return nullptr;
    }

    qCDebug(chatterinoCrashhandler) << "Started crashpad handler";
    return client;
}
#endif

}  // namespace chatterino
