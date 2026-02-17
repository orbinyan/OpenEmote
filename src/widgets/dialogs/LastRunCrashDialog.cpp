// SPDX-FileCopyrightText: 2018 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "widgets/dialogs/LastRunCrashDialog.hpp"

#include "common/Args.hpp"
#include "common/Version.hpp"  // IWYU pragma: keep
#include "singletons/CrashHandler.hpp"
#include "singletons/Paths.hpp"
#include "util/LayoutCreator.hpp"

#include <QCheckBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QRandomGenerator>
#include <QStringBuilder>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace {

const std::initializer_list<QString> MESSAGES = {
    u"Oops..."_s,        u"NotLikeThis"_s,
    u"NOOOOOO"_s,        u"I'm sorry"_s,
    u"We're sorry"_s,    u"My bad"_s,
    u"FailFish"_s,       u"O_o"_s,
    u"Sorry :("_s,       u"I blame cosmic rays"_s,
    u"I blame TMI"_s,    u"I blame Helix"_s,
    u"Oopsie woopsie"_s,
};

QString randomMessage()
{
    return *(MESSAGES.begin() +
             (QRandomGenerator::global()->generate64() % MESSAGES.size()));
}

}  // namespace

namespace chatterino {

LastRunCrashDialog::LastRunCrashDialog(const Args &args, const Paths &paths)
{
    this->setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    this->setWindowTitle(u"Chatterino - " % randomMessage());

    auto layout =
        LayoutCreator<LastRunCrashDialog>(this).setLayoutType<QVBoxLayout>();

    QString text =
        u"Chatterino unexpectedly crashed and restarted. "_s
        "<i>You can disable automatic restarts in the settings.</i><br><br>";

#ifdef CHATTERINO_WITH_CRASHPAD
    QDir crashDir(paths.crashdumpDirectory);
    auto reportsDir = paths.crashdumpDirectory;
    if (crashDir.exists(u"completed"_s))
    {
        reportsDir = crashDir.filePath(u"completed"_s);
    }
    else if (crashDir.exists(u"reports"_s))
    {
        reportsDir = crashDir.filePath(u"reports"_s);
    }
    text += u"A <b>crash report</b> has been saved to "
            "<a href=\"file:///" %
            reportsDir % u"\">" % reportsDir % u"</a>.<br>";

    if (args.exceptionCode)
    {
        text += u"The last run crashed with code <code>0x" %
                QString::number(*args.exceptionCode, 16) % u"</code>";

        if (args.exceptionMessage)
        {
            text += u" (" % *args.exceptionMessage % u")";
        }

        text += u".<br>"_s;
    }

    if (CrashHandler::isCrashUploadForcedInDevMode())
    {
        if (CrashHandler::hasCrashUploadUrlOverride())
        {
            text +=
                "Developer mode is enabled: crash reports are uploaded for "
                "debugging using the configured override URL.<br>";
        }
        else
        {
            text +=
                "Developer mode is enabled: crash reports stay local by "
                "default (no web upload).<br>";
        }
    }
    else
    {
        text +=
            "Crash reports are stored locally unless you explicitly choose to "
            "send them.<br>";
    }

    text +=
        "<br>Please <a "
        "href=\"https://github.com/orbinyan/chatterino-openemote/issues/new\">report "
        "the crash</a> "
        u"so it can be prevented in the future."_s;

    if (Version::instance().isNightly())
    {
        text += u" Make sure you're using the latest nightly version!"_s;
    }

    text +=
        u"<br>For more information, <a href=\"https://wiki.chatterino.com/Crash%20Analysis/\">consult the wiki</a>."_s;
#endif

    auto label = layout.emplace<QLabel>(text);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);

    QCheckBox *alwaysSendFuture = nullptr;
#ifdef CHATTERINO_WITH_CRASHPAD
    if (!CrashHandler::isCrashUploadForcedInDevMode())
    {
        alwaysSendFuture = layout.emplace<QCheckBox>(
                               "Always send future crash reports automatically")
                               .getElement();
        alwaysSendFuture->setChecked(
            CrashHandler::loadShouldUploadCrashReports(paths));
    }
#endif

    layout->addSpacing(16);

    auto buttons = layout.emplace<QDialogButtonBox>();
#ifdef CHATTERINO_WITH_CRASHPAD
    if (!CrashHandler::isCrashUploadForcedInDevMode())
    {
        auto *sendButton =
            buttons->addButton(u"Send crash report"_s, QDialogButtonBox::YesRole);
        QObject::connect(sendButton, &QPushButton::clicked, [this, paths,
                                                             alwaysSendFuture] {
            const bool keepEnabled =
                alwaysSendFuture != nullptr && alwaysSendFuture->isChecked();
            const auto enabledForNow = CrashHandler::applyCrashUploadPreference(
                paths, true);
            if (!enabledForNow)
            {
                QMessageBox::warning(
                    this, "Crash report upload",
                    "Failed to enable crash report upload in this session.");
            }
            CrashHandler::saveShouldUploadCrashReports(paths, keepEnabled);

            this->accept();
        });
    }
#endif

    auto *okButton = buttons->addButton(u"Ok"_s, QDialogButtonBox::AcceptRole);
    QObject::connect(okButton, &QPushButton::clicked, [this] {
        this->accept();
    });
}

}  // namespace chatterino
