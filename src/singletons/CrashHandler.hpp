// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QtGlobal>
#include <QString>

#ifdef CHATTERINO_WITH_CRASHPAD
#    include <client/crashpad_client.h>

#    include <memory>
#endif

namespace chatterino {

class Args;
class Paths;

class CrashHandler
{
    const Paths &paths;

public:
    explicit CrashHandler(const Paths &paths_);

    bool shouldRecover() const
    {
        return this->shouldRecover_;
    }

    /// Sets and saves whether Chatterino should restart on a crash
    void saveShouldRecover(bool value);

    bool shouldUploadCrashReports() const
    {
        return this->shouldUploadCrashReports_;
    }

    /// Sets and saves whether crash reports should be uploaded
    void saveShouldUploadCrashReports(bool value);

    static bool isCrashUploadForcedInDevMode();
    static bool hasCrashUploadUrlOverride();
    static bool shouldUploadCrashReportsAtRuntime(
        bool persistedUserPreference);
    static QString crashUploadUrlForRuntime();
    static QString crashUploadUrl();
    static bool loadShouldUploadCrashReports(const Paths &paths);
    static void saveShouldUploadCrashReports(const Paths &paths, bool enabled);
    static bool applyCrashUploadPreference(const Paths &paths, bool enabled);

private:
    bool shouldRecover_ = false;
    bool shouldUploadCrashReports_ = false;
};

#ifdef CHATTERINO_WITH_CRASHPAD
std::unique_ptr<crashpad::CrashpadClient> installCrashHandler(
    const Args &args, const Paths &paths);
#endif

}  // namespace chatterino
