// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "singletons/CrashHandler.hpp"

#include "Test.hpp"

using namespace chatterino;

namespace {

class ScopedEnvVar
{
public:
    explicit ScopedEnvVar(const char *name)
        : name_(name)
        , oldValue_(qgetenv(name))
        , hadValue_(!oldValue_.isEmpty())
    {
    }

    ~ScopedEnvVar()
    {
        if (this->hadValue_)
        {
            qputenv(this->name_, this->oldValue_);
        }
        else
        {
            qunsetenv(this->name_);
        }
    }

private:
    const char *name_;
    QByteArray oldValue_;
    bool hadValue_;
};

}  // namespace

TEST(CrashHandler, CrashUploadUrlUsesEnvironmentOverride)
{
    auto lock = environmentLock();
    ScopedEnvVar urlEnv("OPENEMOTE_CRASH_UPLOAD_URL");

    qunsetenv("OPENEMOTE_CRASH_UPLOAD_URL");
    EXPECT_EQ(CrashHandler::crashUploadUrl(), "https://openemote.com/crash");

    qputenv("OPENEMOTE_CRASH_UPLOAD_URL", "https://example.com/crash");
    EXPECT_EQ(CrashHandler::crashUploadUrl(), "https://example.com/crash");
}

TEST(CrashHandler, DevCrashUploadFlagRespectsEnvironment)
{
    auto lock = environmentLock();
    ScopedEnvVar devEnv("OPENEMOTE_DEV_CRASH_REPORTS");

    qunsetenv("OPENEMOTE_DEV_CRASH_REPORTS");
#ifndef NDEBUG
    EXPECT_TRUE(CrashHandler::isCrashUploadForcedInDevMode());
#else
    EXPECT_FALSE(CrashHandler::isCrashUploadForcedInDevMode());
#endif

    qputenv("OPENEMOTE_DEV_CRASH_REPORTS", "0");
    EXPECT_FALSE(CrashHandler::isCrashUploadForcedInDevMode());

    qputenv("OPENEMOTE_DEV_CRASH_REPORTS", "1");
    EXPECT_TRUE(CrashHandler::isCrashUploadForcedInDevMode());
}

TEST(CrashHandler, RuntimeUploadPolicyInDevModeDefaultsToLocalOnly)
{
    auto lock = environmentLock();
    ScopedEnvVar devEnv("OPENEMOTE_DEV_CRASH_REPORTS");
    ScopedEnvVar urlEnv("OPENEMOTE_CRASH_UPLOAD_URL");

    qunsetenv("OPENEMOTE_DEV_CRASH_REPORTS");
    qunsetenv("OPENEMOTE_CRASH_UPLOAD_URL");

#ifndef NDEBUG
    EXPECT_FALSE(CrashHandler::hasCrashUploadUrlOverride());
    EXPECT_FALSE(CrashHandler::shouldUploadCrashReportsAtRuntime(true));
    EXPECT_TRUE(CrashHandler::crashUploadUrlForRuntime().isEmpty());
#endif
}

TEST(CrashHandler, RuntimeUploadPolicyHonorsUrlOverride)
{
    auto lock = environmentLock();
    ScopedEnvVar devEnv("OPENEMOTE_DEV_CRASH_REPORTS");
    ScopedEnvVar urlEnv("OPENEMOTE_CRASH_UPLOAD_URL");

    qputenv("OPENEMOTE_DEV_CRASH_REPORTS", "1");
    qputenv("OPENEMOTE_CRASH_UPLOAD_URL", "https://example.com/crash");

    EXPECT_TRUE(CrashHandler::hasCrashUploadUrlOverride());
    EXPECT_TRUE(CrashHandler::shouldUploadCrashReportsAtRuntime(false));
    EXPECT_EQ(CrashHandler::crashUploadUrlForRuntime(),
              "https://example.com/crash");
}

TEST(CrashHandler, RuntimeUploadPolicyUsesPersistedPreferenceOutsideDevForcedMode)
{
    auto lock = environmentLock();
    ScopedEnvVar devEnv("OPENEMOTE_DEV_CRASH_REPORTS");
    ScopedEnvVar urlEnv("OPENEMOTE_CRASH_UPLOAD_URL");

    qputenv("OPENEMOTE_DEV_CRASH_REPORTS", "0");
    qunsetenv("OPENEMOTE_CRASH_UPLOAD_URL");

    EXPECT_FALSE(CrashHandler::isCrashUploadForcedInDevMode());
    EXPECT_FALSE(CrashHandler::shouldUploadCrashReportsAtRuntime(false));
    EXPECT_TRUE(CrashHandler::shouldUploadCrashReportsAtRuntime(true));
}

TEST(CrashHandler, RuntimeUploadUrlUsesPreferenceWhenDevForcedModeDisabled)
{
    auto lock = environmentLock();
    ScopedEnvVar devEnv("OPENEMOTE_DEV_CRASH_REPORTS");
    ScopedEnvVar urlEnv("OPENEMOTE_CRASH_UPLOAD_URL");

    qputenv("OPENEMOTE_DEV_CRASH_REPORTS", "0");
    qunsetenv("OPENEMOTE_CRASH_UPLOAD_URL");
    EXPECT_EQ(CrashHandler::crashUploadUrlForRuntime(),
              "https://openemote.com/crash");

    qputenv("OPENEMOTE_CRASH_UPLOAD_URL", "https://example.com/override");
    EXPECT_EQ(CrashHandler::crashUploadUrlForRuntime(),
              "https://example.com/override");
}
