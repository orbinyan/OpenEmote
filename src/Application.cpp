// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "Application.hpp"

#include "common/Args.hpp"
#include "common/Channel.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/Version.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/Command.hpp"
#include "controllers/commands/CommandController.hpp"
#include "controllers/highlights/HighlightController.hpp"
#include "controllers/hotkeys/HotkeyController.hpp"
#include "controllers/ignores/IgnoreController.hpp"
#include "controllers/notifications/NotificationController.hpp"
#include "controllers/sound/ISoundController.hpp"
#include "controllers/spellcheck/SpellChecker.hpp"
#include "providers/bttv/BttvBadges.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/links/LinkResolver.hpp"
#include "providers/platform/KickPlatformAdapter.hpp"
#include "providers/platform/PlatformRegistry.hpp"
#include "providers/platform/TwitchPlatformAdapter.hpp"
#include "providers/pronouns/Pronouns.hpp"
#include "providers/seventv/SeventvAPI.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/eventsub/Controller.hpp"
#include "providers/twitch/TwitchBadges.hpp"
#include "singletons/ImageUploader.hpp"
#include "singletons/NativeMessaging.hpp"
#ifdef CHATTERINO_HAVE_PLUGINS
#    include "controllers/plugins/PluginController.hpp"
#endif
#include "controllers/emotes/EmoteController.hpp"
#include "controllers/sound/MiniaudioBackend.hpp"
#include "controllers/sound/NullBackend.hpp"
#include "controllers/twitch/LiveController.hpp"
#include "controllers/userdata/UserDataController.hpp"
#include "debug/AssertInGuiThread.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "messages/Selection.hpp"
#include "messages/layouts/MessageLayout.hpp"
#include "messages/layouts/MessageLayoutContext.hpp"
#include "providers/bttv/BttvLiveUpdates.hpp"
#include "providers/chatterino/ChatterinoBadges.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "providers/ffz/FfzBadges.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/seventv/SeventvEventAPI.hpp"
#include "providers/twitch/ChannelPointReward.hpp"
#include "providers/twitch/PubSubManager.hpp"
#include "providers/twitch/PubSubMessages.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "providers/twitch/TwitchUsers.hpp"
#include "singletons/CrashHandler.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/helper/LoggingChannel.hpp"
#include "singletons/Logging.hpp"
#include "singletons/Paths.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/Theme.hpp"
#include "singletons/Toasts.hpp"
#include "singletons/Updates.hpp"
#include "singletons/WindowManager.hpp"
#include "util/OpenEmoteImport.hpp"
#include "util/OpenEmoteIntegration.hpp"
#include "util/Helpers.hpp"
#include "util/PostToThread.hpp"
#include "widgets/Notebook.hpp"
#include "widgets/dialogs/SettingsDialog.hpp"
#include "widgets/splits/Split.hpp"
#include "widgets/Window.hpp"

#include <pajlada/settings/setting.hpp>
#include <miniaudio.h>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QButtonGroup>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QFont>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QPainter>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace {

using namespace chatterino;

const QString BTTV_LIVE_UPDATES_URL = "wss://sockets.betterttv.net/ws";
const QString SEVENTV_EVENTAPI_URL = "wss://events.7tv.io/v3";

std::atomic<bool> STOPPED{false};
std::atomic<bool> ABOUT_TO_QUIT{false};
std::atomic<bool> OPENEMOTE_ONBOARDING_SCHEDULED{false};
std::atomic<int> OPENEMOTE_ONBOARDING_PARENT_RETRIES{0};
constexpr int OPENEMOTE_ONBOARDING_REVISION = 10;

struct OpenEmoteOnboardingLayoutState {
    bool showTimestamps = true;
    bool timestampGapsOnly = true;
    int timestampGapMinutes = 4;
    bool compactAuthorIdentity = false;
    bool compactHeaderLayout = false;
    bool compactKeepNames = true;
    bool avatarDecorators = false;
    bool avatarCornerBadges = false;
    QString avatarBadgeAnchor = "left";
    bool identityRail = false;
    bool showReplyButton = false;
    bool alternateMessages = false;
    bool preferThreadDrawer = false;
    bool showThreadActivity = false;
    bool showBadgesVanity = true;
    bool showBadgesFfz = true;
    bool showBadgesBttv = true;
    bool showBadgesSevenTV = true;
    QString chatFontFamily;
    int chatFontWeight = QFont::Normal;
};

class OpenEmoteOnboardingLivePreview final : public QWidget
{
public:
    struct Config {
        QString username = "user";
        bool showTimestamp = true;
        bool timestampRight = false;
        bool showReplyIcon = false;
        bool showBadges = true;
        bool alternateRows = false;
    };

    explicit OpenEmoteOnboardingLivePreview(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        this->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        this->setMinimumHeight(30);
        this->buildMessage();
        this->layoutMessage();
    }

    void setPreviewConfig(const Config &config)
    {
        this->config_ = config;
        this->buildMessage();
        this->layoutMessage();
        this->update();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        this->layoutMessage();
    }

    void paintEvent(QPaintEvent * /*event*/) override
    {
        QPainter painter(this);
        if (!this->layout_)
        {
            painter.fillRect(this->rect(), this->messageColors_.regularBg);
            return;
        }

        static const Selection EMPTY_SELECTION;
        MessagePaintContext ctx{
            .painter = painter,
            .selection = EMPTY_SELECTION,
            .colorProvider = ColorProvider::instance(),
            .messageColors = this->messageColors_,
            .preferences = this->messagePreferences_,

            .canvasWidth = this->width(),
            .isWindowFocused = true,
            .isMentions = false,

            .y = 0,
            .messageIndex = this->config_.alternateRows ? 1U : 0U,
            .isLastReadMessage = false,
        };
        this->layout_->paint(ctx);
    }

private:
    void refreshTheme()
    {
        this->messageColors_.applyTheme(getTheme(), false, 255);
        this->messageColors_.regularBg = getTheme()->splits.input.background;
    }

    void buildMessage()
    {
        auto message = std::make_shared<Message>();
        message->messageText = "hello chat";
        message->parseTime = QTime(12, 41, 0);

        if (this->config_.showTimestamp && !this->config_.timestampRight)
        {
            message->elements.emplace_back(
                std::make_unique<TimestampElement>(QTime(12, 41, 0)));
        }

        if (this->config_.showBadges)
        {
            message->elements.emplace_back(std::make_unique<TextElement>(
                "MOD", MessageElementFlag::BadgeVanity, MessageColor::System,
                FontStyle::ChatMediumSmall));
            message->elements.emplace_back(std::make_unique<TextElement>(
                "VIP", MessageElementFlag::BadgeVanity, MessageColor::System,
                FontStyle::ChatMediumSmall));
        }

        const auto username = this->config_.username.trimmed().isEmpty()
                                  ? QString("user")
                                  : this->config_.username.trimmed();
        message->elements.emplace_back(std::make_unique<TextElement>(
            username + ":", MessageElementFlag::Username, MessageColor::Text,
            FontStyle::ChatMediumBold));
        message->elements.emplace_back(std::make_unique<TextElement>(
            "hello chat", MessageElementFlag::Text, MessageColor::Text,
            FontStyle::ChatMedium));

        if (this->config_.showReplyIcon)
        {
            message->elements.emplace_back(std::make_unique<TextElement>(
                "↩", MessageElementFlag::ReplyButton, MessageColor::System,
                FontStyle::ChatMedium));
        }

        if (this->config_.showTimestamp && this->config_.timestampRight)
        {
            message->elements.emplace_back(
                std::make_unique<TimestampElement>(QTime(12, 41, 0)));
        }

        this->message_ = std::move(message);
        this->layout_ = std::make_unique<MessageLayout>(this->message_);
    }

    void layoutMessage()
    {
        if (!this->layout_)
        {
            return;
        }

        this->refreshTheme();
        this->messagePreferences_.alternateMessages = true;
        if (this->config_.alternateRows)
        {
            this->layout_->flags.set(MessageLayoutFlag::AlternateBackground);
        }
        else
        {
            this->layout_->flags.unset(MessageLayoutFlag::AlternateBackground);
        }

        constexpr MessageElementFlags PREVIEW_FLAGS{
            MessageElementFlag::Text,
            MessageElementFlag::Username,
            MessageElementFlag::BadgeVanity,
            MessageElementFlag::ReplyButton,
            MessageElementFlag::Timestamp,
        };

        this->layout_->layout(
            {
                .messageColors = this->messageColors_,
                .flags = PREVIEW_FLAGS,
                .width = std::max(1, this->width()),
                .scale = 1.0F,
                .imageScale =
                    static_cast<float>(this->devicePixelRatioF()),
            },
            true);

        this->setFixedHeight(std::max(30, this->layout_->getHeight()));
    }

    Config config_;
    MessagePtr message_;
    std::unique_ptr<MessageLayout> layout_;
    MessageColors messageColors_;
    MessagePreferences messagePreferences_;
};

QString normalizeAvatarBadgeAnchor(QString value)
{
    value = value.trimmed().toLower();
    if (value == "top" || value == "bottom" || value == "left" ||
        value == "right")
    {
        return value;
    }
    return "left";
}

OpenEmoteOnboardingLayoutState captureOnboardingLayoutState(Settings &settings)
{
    return {
        .showTimestamps = settings.showTimestamps,
        .timestampGapsOnly = settings.openEmoteTimestampGapsOnly,
        .timestampGapMinutes = settings.openEmoteTimestampGapMinutes.getValue(),
        .compactAuthorIdentity = settings.openEmoteCompactAuthorAvatar,
        .compactHeaderLayout = settings.openEmoteCompactHeaderLayout,
        .compactKeepNames = settings.openEmoteCompactAvatarKeepNames,
        .avatarDecorators = settings.openEmoteAvatarDecorators,
        .avatarCornerBadges = settings.openEmoteAvatarCornerBadges,
        .avatarBadgeAnchor = normalizeAvatarBadgeAnchor(
            settings.openEmoteAvatarBadgeAnchor.getValue()),
        .identityRail = settings.openEmoteIdentityRailEnabled,
        .showReplyButton = settings.showReplyButton,
        .alternateMessages = settings.alternateMessages,
        .preferThreadDrawer = settings.openEmotePreferThreadDrawer,
        .showThreadActivity = settings.openEmoteShowThreadActivityIndicator,
        .showBadgesVanity = settings.showBadgesVanity,
        .showBadgesFfz = settings.showBadgesFfz,
        .showBadgesBttv = settings.showBadgesBttv,
        .showBadgesSevenTV = settings.showBadgesSevenTV,
        .chatFontFamily = settings.chatFontFamily.getValue(),
        .chatFontWeight = settings.chatFontWeight.getValue(),
    };
}

std::optional<OpenEmoteOnboardingLayoutState> loadLegacyOnboardingLayoutState(
    const QString &sourceDir, const OpenEmoteOnboardingLayoutState &fallback)
{
    QFile sourceSettingsFile(QDir(sourceDir).filePath("settings.json"));
    if (!sourceSettingsFile.open(QIODevice::ReadOnly))
    {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const auto parsed =
        QJsonDocument::fromJson(sourceSettingsFile.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
    {
        return std::nullopt;
    }

    auto state = fallback;
    const auto root = parsed.object();

    const auto appearance = root.value("appearance").toObject();
    const auto messages = appearance.value("messages").toObject();
    if (messages.contains("alternateMessageBackground"))
    {
        const auto value = messages.value("alternateMessageBackground");
        if (value.isBool())
        {
            state.alternateMessages = value.toBool();
        }
    }

    const auto behaviour = root.value("behaviour").toObject();
    if (behaviour.contains("autoCloseThreadPopup"))
    {
        const auto value = behaviour.value("autoCloseThreadPopup");
        if (value.isBool())
        {
            state.preferThreadDrawer = value.toBool();
        }
    }

    return state;
}

void applyOnboardingLayoutState(Settings &settings,
                                const OpenEmoteOnboardingLayoutState &state)
{
    settings.showTimestamps = state.showTimestamps;
    settings.openEmoteTimestampGapsOnly = state.timestampGapsOnly;
    settings.openEmoteTimestampGapMinutes = std::clamp(state.timestampGapMinutes, 1, 400);
    settings.openEmoteCompactAuthorAvatar = state.compactAuthorIdentity;
    settings.openEmoteCompactHeaderLayout = state.compactHeaderLayout;
    settings.openEmoteCompactAvatarKeepNames = state.compactKeepNames;
    settings.openEmoteAvatarDecorators = state.avatarDecorators;
    settings.openEmoteAvatarCornerBadges = state.avatarCornerBadges;
    const auto badgeAnchor = normalizeAvatarBadgeAnchor(state.avatarBadgeAnchor);
    settings.openEmoteAvatarBadgeAnchor = badgeAnchor;
    settings.openEmoteAvatarBadgeRightSide = badgeAnchor == "right";
    settings.openEmoteIdentityRailEnabled = state.identityRail;
    settings.showReplyButton = state.showReplyButton;
    settings.alternateMessages = state.alternateMessages;
    settings.openEmotePreferThreadDrawer = state.preferThreadDrawer;
    settings.openEmoteShowThreadActivityIndicator = state.showThreadActivity;
    settings.showBadgesVanity = state.showBadgesVanity;
    settings.showBadgesFfz = state.showBadgesFfz;
    settings.showBadgesBttv = state.showBadgesBttv;
    settings.showBadgesSevenTV = state.showBadgesSevenTV;
    if (!state.chatFontFamily.trimmed().isEmpty())
    {
        settings.chatFontFamily = state.chatFontFamily.trimmed();
    }
    settings.chatFontWeight = state.chatFontWeight;
}

OpenEmoteOnboardingLayoutState onboardingPreset(QStringView presetId,
                                                Settings &settings)
{
    auto preset = captureOnboardingLayoutState(settings);

    if (presetId == u"classic")
    {
        preset.showTimestamps = true;
        preset.timestampGapsOnly = false;
        preset.compactAuthorIdentity = false;
        preset.compactHeaderLayout = false;
        preset.avatarDecorators = false;
        preset.avatarCornerBadges = false;
        preset.avatarBadgeAnchor = "left";
        preset.identityRail = false;
        preset.showReplyButton = false;
        preset.alternateMessages = false;
        preset.preferThreadDrawer = false;
        preset.showThreadActivity = false;
        preset.showBadgesVanity = true;
        preset.showBadgesFfz = true;
        preset.showBadgesBttv = true;
        preset.showBadgesSevenTV = true;
        return preset;
    }

    if (presetId == u"minimal")
    {
        preset.showTimestamps = false;
        preset.compactAuthorIdentity = false;
        preset.compactHeaderLayout = false;
        preset.avatarDecorators = false;
        preset.avatarCornerBadges = false;
        preset.avatarBadgeAnchor = "left";
        preset.identityRail = false;
        preset.showReplyButton = false;
        preset.alternateMessages = true;
        preset.preferThreadDrawer = true;
        preset.showThreadActivity = false;
        preset.showBadgesVanity = false;
        preset.showBadgesFfz = false;
        preset.showBadgesBttv = false;
        preset.showBadgesSevenTV = false;
        return preset;
    }

    if (presetId == u"compact")
    {
        preset.showTimestamps = true;
        preset.timestampGapsOnly = true;
        preset.timestampGapMinutes = 4;
        preset.compactAuthorIdentity = true;
        preset.compactHeaderLayout = true;
        preset.compactKeepNames = true;
        preset.avatarDecorators = true;
        preset.avatarCornerBadges = false;
        preset.avatarBadgeAnchor = "left";
        preset.identityRail = true;
        preset.showReplyButton = false;
        preset.alternateMessages = true;
        preset.preferThreadDrawer = true;
        preset.showThreadActivity = true;
        preset.showBadgesVanity = true;
        preset.showBadgesFfz = true;
        preset.showBadgesBttv = true;
        preset.showBadgesSevenTV = true;
        return preset;
    }

    if (presetId == u"creator")
    {
        preset.showTimestamps = true;
        preset.timestampGapsOnly = true;
        preset.timestampGapMinutes = 2;
        preset.compactAuthorIdentity = true;
        preset.compactHeaderLayout = true;
        preset.compactKeepNames = true;
        preset.avatarDecorators = true;
        preset.avatarCornerBadges = true;
        preset.avatarBadgeAnchor = "left";
        preset.identityRail = true;
        preset.showReplyButton = false;
        preset.alternateMessages = true;
        preset.preferThreadDrawer = true;
        preset.showThreadActivity = true;
        preset.showBadgesVanity = true;
        preset.showBadgesFfz = true;
        preset.showBadgesBttv = true;
        preset.showBadgesSevenTV = true;
        return preset;
    }

    if (presetId == u"notimestamps")
    {
        preset.showTimestamps = false;
        preset.compactAuthorIdentity = true;
        preset.compactHeaderLayout = true;
        preset.compactKeepNames = true;
        preset.avatarDecorators = false;
        preset.avatarCornerBadges = true;
        preset.avatarBadgeAnchor = "left";
        preset.identityRail = true;
        preset.showReplyButton = false;
        preset.alternateMessages = true;
        preset.preferThreadDrawer = true;
        preset.showThreadActivity = true;
        preset.showBadgesVanity = true;
        preset.showBadgesFfz = true;
        preset.showBadgesBttv = true;
        preset.showBadgesSevenTV = true;
        return preset;
    }

    return preset;
}

QString onboardingPresetDescription(QStringView presetId)
{
    if (presetId == u"classic")
    {
        return "Closest to Chatterino defaults. Conservative and familiar.";
    }
    if (presetId == u"minimal")
    {
        return "Low-clutter mode: hidden timestamps and reduced vanity badge noise.";
    }
    if (presetId == u"compact")
    {
        return "OpenEmote balanced compact layout with right-side timestamps and drawer-first threads.";
    }
    if (presetId == u"creator")
    {
        return "High-signal creator view with compact identity, corner badges, and active thread cues.";
    }
    if (presetId == u"notimestamps")
    {
        return "No timestamps, compact identity, and chat-focused readability.";
    }
    return "Customizable preset baseline.";
}

int onboardingChangedFieldCount(const OpenEmoteOnboardingLayoutState &before,
                                const OpenEmoteOnboardingLayoutState &after)
{
    int changed = 0;
    changed += before.showTimestamps != after.showTimestamps;
    changed += before.timestampGapsOnly != after.timestampGapsOnly;
    changed += before.timestampGapMinutes != after.timestampGapMinutes;
    changed += before.compactAuthorIdentity != after.compactAuthorIdentity;
    changed += before.compactHeaderLayout != after.compactHeaderLayout;
    changed += before.compactKeepNames != after.compactKeepNames;
    changed += before.avatarDecorators != after.avatarDecorators;
    changed += before.avatarCornerBadges != after.avatarCornerBadges;
    changed += before.avatarBadgeAnchor != after.avatarBadgeAnchor;
    changed += before.identityRail != after.identityRail;
    changed += before.showReplyButton != after.showReplyButton;
    changed += before.alternateMessages != after.alternateMessages;
    changed += before.preferThreadDrawer != after.preferThreadDrawer;
    changed += before.showThreadActivity != after.showThreadActivity;
    changed += before.showBadgesVanity != after.showBadgesVanity;
    changed += before.showBadgesFfz != after.showBadgesFfz;
    changed += before.showBadgesBttv != after.showBadgesBttv;
    changed += before.showBadgesSevenTV != after.showBadgesSevenTV;
    changed += before.chatFontFamily != after.chatFontFamily;
    changed += before.chatFontWeight != after.chatFontWeight;
    return changed;
}

int importLegacyTwitchAccounts(Application *app, const QString &sourceDir)
{
    const auto payload = openemote::loadLegacyTwitchAccounts(sourceDir);
    if (payload.accounts.empty())
    {
        return 0;
    }

    int imported = 0;
    QStringList importedUsernames;

    for (const auto &account : payload.accounts)
    {
        const auto basePath = "/accounts/uid" + account.userID.toStdString();
        pajlada::Settings::Setting<QString>::set(basePath + "/username",
                                                 account.username);
        pajlada::Settings::Setting<QString>::set(basePath + "/userID",
                                                 account.userID);
        pajlada::Settings::Setting<QString>::set(basePath + "/clientID",
                                                 account.clientID);
        pajlada::Settings::Setting<QString>::set(basePath + "/oauthToken",
                                                 account.oauthToken);

        imported++;
        importedUsernames.push_back(account.username);
    }

    if (imported == 0)
    {
        return 0;
    }

    if (const auto selectedCurrent = openemote::pickImportedCurrentUsername(
            payload.currentUsername, importedUsernames,
            app->getAccounts()->twitch.currentUsername.getValue()))
    {
        app->getAccounts()->twitch.currentUsername = selectedCurrent.value();
    }

    app->getAccounts()->twitch.reloadUsers();
    getSettings()->requestSave();
    return imported;
}

void applyOpenEmoteIntegrationFromArgs(const Args &args)
{
    if (!args.openEmoteIntegrationUrl.has_value())
    {
        return;
    }

    const auto url = args.openEmoteIntegrationUrl.value();
    const auto query = QUrlQuery(url);
    const auto ticket = query.queryItemValue("ticket").trimmed();
    if (ticket.isEmpty())
    {
        qCWarning(chatterinoApp)
            << "OpenEmote integration URL missing ticket query param";
        return;
    }

    auto endpoint = qEnvironmentVariable(
        "CHATTERINO_OPENEMOTE_INTEGRATION_APPLY_URL");
    if (endpoint.isEmpty())
    {
        endpoint = "https://openemote.com/api/integrations/redeem";
    }

    QJsonObject payload{
        {"ticket", ticket},
        {"client", "chatterino-openemote"},
    };

    NetworkRequest(QUrl(endpoint), NetworkRequestType::Post)
        .json(payload)
        .onSuccess([](NetworkResult result) {
            const auto root = result.parseJson();
            const auto pack = root.contains("pack") && root.value("pack").isObject()
                                  ? root.value("pack").toObject()
                                  : root;

            QString error;
            if (!openemote::integration::applyIntegrationPack(
                    pack, *getSettings(), error))
            {
                qCWarning(chatterinoApp)
                    << "Failed to apply OpenEmote integration pack:" << error;
                return;
            }

            qCInfo(chatterinoApp)
                << "Applied OpenEmote integration pack from URL ticket";
        })
        .onError([](NetworkResult result) {
            qCWarning(chatterinoApp)
                << "Failed to redeem OpenEmote integration ticket:"
                << result.formatError();
        })
        .execute();
}

void showOpenEmoteOnboardingIfNeeded(Application *app)
{
    const bool onboardingAlreadyShown =
        getSettings()->openEmoteOnboardingShown.getValue();
    const bool onboardingRevisionCurrent =
        getSettings()->openEmoteOnboardingRevision.getValue() >=
        OPENEMOTE_ONBOARDING_REVISION;
    if ((onboardingAlreadyShown && onboardingRevisionCurrent) ||
        app->getArgs().isFramelessEmbed ||
        qEnvironmentVariableIsSet("OPENEMOTE_SKIP_ONBOARDING"))
    {
        return;
    }

    bool expected = false;
    if (!OPENEMOTE_ONBOARDING_SCHEDULED.compare_exchange_strong(expected, true))
    {
        return;
    }

    QTimer::singleShot(100, qApp, [app] {
        auto clearScheduledFlag = [] {
            OPENEMOTE_ONBOARDING_SCHEDULED = false;
        };
        auto scheduleRetry = [app, &clearScheduledFlag](int delayMs) {
            clearScheduledFlag();
            QTimer::singleShot(delayMs, qApp, [app] {
                showOpenEmoteOnboardingIfNeeded(app);
            });
        };

        if (ABOUT_TO_QUIT || QCoreApplication::closingDown())
        {
            clearScheduledFlag();
            return;
        }

        const bool alreadyShown = getSettings()->openEmoteOnboardingShown.getValue();
        const bool revisionCurrent =
            getSettings()->openEmoteOnboardingRevision.getValue() >=
            OPENEMOTE_ONBOARDING_REVISION;
        if (alreadyShown && revisionCurrent)
        {
            clearScheduledFlag();
            return;
        }

        auto *parent = app->getWindows()->getMainWindow().window();
        if (parent == nullptr || !parent->isVisible())
        {
            constexpr int maxParentRetries = 40;
            const auto retries = OPENEMOTE_ONBOARDING_PARENT_RETRIES.fetch_add(1) + 1;
            if (retries <= maxParentRetries)
            {
                scheduleRetry(200);
            }
            else
            {
                clearScheduledFlag();
            }
            return;
        }

        // Avoid stacking startup dialogs (settings/auth windows can also open
        // during first-run), which made onboarding feel unresponsive.
        if (QApplication::activeModalWidget() != nullptr ||
            parent->windowState().testFlag(Qt::WindowMinimized))
        {
            constexpr int maxModalRetries = 30;
            const auto retries = OPENEMOTE_ONBOARDING_PARENT_RETRIES.fetch_add(1) + 1;
            if (retries <= maxModalRetries)
            {
                scheduleRetry(250);
            }
            else
            {
                clearScheduledFlag();
            }
            return;
        }
        OPENEMOTE_ONBOARDING_PARENT_RETRIES = 0;

        const auto legacyDirs = openemote::findLegacySettingsDirectories(
            app->getPaths().rootAppDataDirectory,
            app->getPaths().settingsDirectory);
        const auto legacyAccountCount =
            legacyDirs.isEmpty()
                ? 0
                : openemote::countLegacyTwitchAccounts(legacyDirs.front());
        const auto legacyLayoutBaseline =
            legacyDirs.isEmpty()
                ? std::optional<OpenEmoteOnboardingLayoutState>{}
                : loadLegacyOnboardingLayoutState(
                      legacyDirs.front(),
                      captureOnboardingLayoutState(*getSettings()));

        QDialog dialog(parent);
        dialog.setWindowTitle("Welcome to OpenEmote");
        dialog.setModal(true);
        dialog.setWindowModality(Qt::WindowModal);
        dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
        dialog.setMinimumSize(720, 520);
        dialog.resize(820, 640);
        dialog.setSizeGripEnabled(true);
        dialog.raise();
        dialog.activateWindow();

        auto *rootLayout = new QVBoxLayout(&dialog);
        rootLayout->setContentsMargins(8, 8, 8, 8);
        auto *scrollArea = new QScrollArea(&dialog);
        scrollArea->setWidgetResizable(true);
        scrollArea->setFrameShape(QFrame::NoFrame);
        rootLayout->addWidget(scrollArea);

        auto *content = new QWidget(&dialog);
        auto *layout = new QVBoxLayout(content);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(8);
        scrollArea->setWidget(content);
        auto *wizard = new QStackedWidget(content);
        layout->addWidget(wizard, 1);

        auto *pageImport = new QWidget(&dialog);
        auto *pageImportLayout = new QVBoxLayout(pageImport);
        pageImportLayout->setSpacing(8);
        wizard->addWidget(pageImport);

        auto *intro = new QLabel(
            "Import existing setup\n\n"
            "Bring your existing Chatterino profile into OpenEmote. "
            "You can keep everything backward compatible and opt in gradually.",
            pageImport);
        intro->setWordWrap(true);
        pageImportLayout->addWidget(intro);

        auto *importSettings = new QCheckBox(
            "Import existing Chatterino settings into OpenEmote profile",
            pageImport);
        importSettings->setChecked(!legacyDirs.isEmpty());
        importSettings->setEnabled(!legacyDirs.isEmpty());
        pageImportLayout->addWidget(importSettings);

        auto *importLogin = new QCheckBox(
            "Import Twitch login account(s) from Chatterino (explicit opt-in)",
            pageImport);
        importLogin->setChecked(legacyAccountCount > 0);
        importLogin->setEnabled(legacyAccountCount > 0);
        importLogin->setToolTip(
            "Copies account credentials into the OpenEmote profile. "
            "Credentials are stored through your configured secure settings "
            "path and are never logged in plaintext.");
        pageImportLayout->addWidget(importLogin);

        auto *note = new QLabel(
            legacyDirs.isEmpty()
                ? "No legacy settings profile was found automatically."
                : legacyAccountCount > 0
                      ? QString("Legacy profile detected with %1 Twitch "
                                "account(s) ready to import.")
                            .arg(legacyAccountCount)
                      : "Legacy profile detected and ready to import.",
            pageImport);
        note->setWordWrap(true);
        pageImportLayout->addWidget(note);

        auto *streamerModeSetup = new QCheckBox(
            "Enable streamer setup (OAuth + emote hosting options)",
            pageImport);
        streamerModeSetup->setChecked(false);
        pageImportLayout->addWidget(streamerModeSetup);

        auto *streamerModeHint = new QLabel(
            "Leave this off for normal viewer usage. Turn it on only if you "
            "stream and want channel-level emote hosting controls.",
            pageImport);
        streamerModeHint->setWordWrap(true);
        streamerModeHint->setStyleSheet("color: #8d95a5;");
        pageImportLayout->addWidget(streamerModeHint);
        pageImportLayout->addStretch(1);

        auto *pageStreamer = new QWidget(&dialog);
        auto *pageStreamerLayout = new QVBoxLayout(pageStreamer);
        pageStreamerLayout->setSpacing(8);
        wizard->addWidget(pageStreamer);

        auto *streamerIntro = new QLabel(
            "Streamer setup (optional)\n\n"
            "Connect OAuth and choose how your channel emotes are managed.",
            pageStreamer);
        streamerIntro->setWordWrap(true);
        pageStreamerLayout->addWidget(streamerIntro);

        auto *streamerOauthNow = new QCheckBox(
            "Connect Twitch OAuth account now", pageStreamer);
        streamerOauthNow->setChecked(true);
        streamerOauthNow->setToolTip(
            "No manual token paste required. You can still connect later in Settings.");
        pageStreamerLayout->addWidget(streamerOauthNow);

        auto *streamerHostingLabel =
            new QLabel("Choose hosting mode", pageStreamer);
        streamerHostingLabel->setStyleSheet("font-weight: 600;");
        pageStreamerLayout->addWidget(streamerHostingLabel);

        auto *streamerHostingGroup = new QButtonGroup(&dialog);
        auto *streamerHosted = new QRadioButton(
            "Use OpenEmote hosted (free defaults, optional account/donations)",
            pageStreamer);
        streamerHosted->setProperty("value", "hosted");
        streamerHosted->setChecked(true);
        streamerHostingGroup->addButton(streamerHosted);
        pageStreamerLayout->addWidget(streamerHosted);

        auto *streamerSelfHost = new QRadioButton(
            "Use self-hosted OpenEmote-compatible API",
            pageStreamer);
        streamerSelfHost->setProperty("value", "self-host");
        streamerHostingGroup->addButton(streamerSelfHost);
        pageStreamerLayout->addWidget(streamerSelfHost);

        auto *selfHostFrame = new QFrame(pageStreamer);
        selfHostFrame->setStyleSheet(
            "QFrame { background: #14171f; border: 1px solid #303745; "
            "border-radius: 6px; }");
        auto *selfHostLayout = new QVBoxLayout(selfHostFrame);
        selfHostLayout->setContentsMargins(10, 10, 10, 10);
        selfHostLayout->setSpacing(6);

        auto *selfHostBaseUrlLabel =
            new QLabel("Self-host API base URL (must be https)", selfHostFrame);
        selfHostLayout->addWidget(selfHostBaseUrlLabel);

        auto *selfHostBaseUrl = new QLineEdit(selfHostFrame);
        selfHostBaseUrl->setPlaceholderText(
            "https://openemote.com or your own host");
        selfHostBaseUrl->setText("https://openemote.com");
        selfHostLayout->addWidget(selfHostBaseUrl);

        auto *selfHostTokenLabel =
            new QLabel("Bearer token (optional now, can be set later)", selfHostFrame);
        selfHostLayout->addWidget(selfHostTokenLabel);

        auto *selfHostToken = new QLineEdit(selfHostFrame);
        selfHostToken->setEchoMode(QLineEdit::Password);
        selfHostToken->setPlaceholderText("oe_xxx...");
        selfHostLayout->addWidget(selfHostToken);

        auto *selfHostGuide = new QPlainTextEdit(selfHostFrame);
        selfHostGuide->setReadOnly(true);
        selfHostGuide->setMaximumHeight(190);
        selfHostGuide->setPlainText(
            "Self-host integration guide (OpenEmote-compatible)\n"
            "\n"
            "Endpoints:\n"
            "  POST /self-host/register   (register your self-host link + metadata)\n"
            "  POST /self-host/emote-bulk (initial emote bootstrap, chunked)\n"
            "  POST /self-host/badge-bulk (initial badge bootstrap, chunked)\n"
            "  PUT  /self-host/emote/{key}    (incremental upsert)\n"
            "  PATCH/DELETE /self-host/emote/{key} (incremental update/remove)\n"
            "  PUT  /self-host/badge/{key}    (incremental upsert)\n"
            "  DELETE /self-host/badge/{key}  (incremental remove)\n"
            "\n"
            "Authorization header:\n"
            "  Authorization: Bearer <token>\n"
            "  Idempotency-Key: <uuid>  (recommended)\n"
            "\n"
            "Starter JSON template:\n"
            "{\n"
            "  \"base_url\": \"https://your-host.example\",\n"
            "  \"channel_login\": \"your_channel\",\n"
            "  \"endpoints\": {\n"
            "    \"register\": \"/self-host/register\",\n"
            "    \"emote_bulk\": \"/self-host/emote-bulk\",\n"
            "    \"badge_bulk\": \"/self-host/badge-bulk\",\n"
            "    \"emote_item\": \"/self-host/emote/{key}\",\n"
            "    \"badge_item\": \"/self-host/badge/{key}\"\n"
            "  },\n"
            "  \"bulk\": {\n"
            "    \"chunk_size\": 500,\n"
            "    \"fields\": [\"sync_session_id\", \"chunk_index\", \"is_last_chunk\"]\n"
            "  },\n"
            "  \"auth\": {\n"
            "    \"type\": \"bearer\",\n"
            "    \"header\": \"Authorization: Bearer <token>\"\n"
            "  }\n"
            "}\n"
            "\n"
            "Recommended flow:\n"
            "  1) First sync: emote-bulk + badge-bulk in chunks\n"
            "  2) Ongoing sync: per-item incremental endpoints\n"
            "\n"
            "In OpenEmote Chatterino this wizard configures the uploader to the "
            "incremental emote endpoint and stores bulk endpoint hints in headers.");
        selfHostLayout->addWidget(selfHostGuide);

        auto setSelfHostWidgetsEnabled = [selfHostBaseUrlLabel, selfHostBaseUrl,
                                          selfHostTokenLabel, selfHostToken,
                                          selfHostGuide, selfHostFrame](bool enabled) {
            selfHostFrame->setVisible(enabled);
            selfHostBaseUrlLabel->setEnabled(enabled);
            selfHostBaseUrl->setEnabled(enabled);
            selfHostTokenLabel->setEnabled(enabled);
            selfHostToken->setEnabled(enabled);
            selfHostGuide->setEnabled(enabled);
        };
        auto updateStreamerStepVisibility = [streamerModeSetup, streamerSelfHost,
                                             setSelfHostWidgetsEnabled]() {
            setSelfHostWidgetsEnabled(streamerModeSetup->isChecked() &&
                                      streamerSelfHost->isChecked());
        };
        QObject::connect(streamerModeSetup, &QCheckBox::toggled, &dialog,
                         [updateStreamerStepVisibility](bool) {
                             updateStreamerStepVisibility();
                         });
        QObject::connect(
            streamerHostingGroup,
            qOverload<QAbstractButton *>(&QButtonGroup::buttonClicked),
            &dialog, [updateStreamerStepVisibility](QAbstractButton *) {
                updateStreamerStepVisibility();
            });
        updateStreamerStepVisibility();

        pageStreamerLayout->addWidget(selfHostFrame);
        pageStreamerLayout->addStretch(1);

        auto *pagePreset = new QWidget(&dialog);
        auto *pagePresetLayout = new QVBoxLayout(pagePreset);
        pagePresetLayout->setSpacing(8);
        wizard->addWidget(pagePreset);

        auto *presetIntro = new QLabel(
            "Pick a baseline\n\nChoose the closest look first. "
            "You can change every detail later in Settings.",
            pagePreset);
        presetIntro->setWordWrap(true);
        pagePresetLayout->addWidget(presetIntro);

        auto *presetGroup = new QButtonGroup(&dialog);
        const auto currentAccount = getApp()->getAccounts()->twitch.getCurrent();
        QString legacyPreviewUsername;
        if (!legacyDirs.isEmpty())
        {
            const auto payload =
                openemote::loadLegacyTwitchAccounts(legacyDirs.front());
            if (!payload.accounts.empty())
            {
                legacyPreviewUsername = payload.accounts.front().username.trimmed();
            }
        }
        auto resolvePreviewName = [currentAccount, importSettings,
                                   legacyPreviewUsername]() -> QString {
            if (currentAccount && !currentAccount->getUserName().isEmpty())
            {
                return currentAccount->getUserName().toHtmlEscaped();
            }
            if (importSettings->isChecked() && !legacyPreviewUsername.isEmpty())
            {
                return legacyPreviewUsername.toHtmlEscaped();
            }
            return "username";
        };
        const auto previewName = resolvePreviewName();
        const auto previewNameColor =
            (currentAccount && currentAccount->color().isValid())
                ? currentAccount->color().name(QColor::HexRgb)
                : QString("#71c8ff");
        auto previewBadgeDot = [](const QString &color, int sizePx) {
            return QString(
                       "<span style='display:inline-block;width:%1px;height:%1px;"
                       "border-radius:2px;background:%2;vertical-align:middle;'></span>")
                .arg(sizePx)
                .arg(color);
        };
        auto previewStatusBadgesHtml = [previewBadgeDot](bool compact) {
            const auto size = compact ? 8 : 10;
            return previewBadgeDot("#2b8a3e", size) + " " +
                   previewBadgeDot("#9a6a2f", size);
        };
        auto addPresetCard = [&](const QString &id, const QString &title,
                                 const QString &mainHtml,
                                 const QString &rightHtml = QString())
            -> QRadioButton * {
            auto *card = new QWidget(pagePreset);
            card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(8, 8, 8, 8);
            cardLayout->setSpacing(4);
            auto *radio = new QRadioButton(title, card);
            radio->setProperty("presetId", id);
            presetGroup->addButton(radio);
            cardLayout->addWidget(radio);

            auto *frame = new QFrame(card);
            frame->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            frame->setStyleSheet(
                "QFrame { background: #14171f; border: 1px solid #303745; "
                "border-radius: 6px; }");
            auto *frameLayout = new QVBoxLayout(frame);
            frameLayout->setContentsMargins(8, 6, 8, 6);

            auto *row = new QWidget(frame);
            row->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            auto *rowLayout = new QHBoxLayout(row);
            rowLayout->setContentsMargins(0, 0, 0, 0);
            rowLayout->setSpacing(8);

            auto *main = new QLabel(row);
            main->setTextFormat(Qt::RichText);
            main->setWordWrap(false);
            main->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
            main->setText(mainHtml);
            rowLayout->addWidget(main, 1);

            auto *right = new QLabel(row);
            right->setTextFormat(Qt::RichText);
            right->setWordWrap(false);
            right->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            right->setMinimumWidth(52);
            right->setStyleSheet(
                "padding: 1px 6px; border: 1px solid #303745; border-radius: 6px;");
            right->setText(rightHtml);
            right->setVisible(!rightHtml.isEmpty());
            rowLayout->addWidget(right, 0, Qt::AlignRight | Qt::AlignVCenter);

            frameLayout->addWidget(row);
            cardLayout->addWidget(frame);
            pagePresetLayout->addWidget(card);
            return radio;
        };

        auto *classicPreset = addPresetCard(
            "classic", "Classic (closest to Chatterino)",
            QString("<span style='color:#8d95a5;'>12:41</span> ") +
                previewStatusBadgesHtml(false) + " " +
                "<span style='color:" + previewNameColor +
                ";font-weight:600;'>" + previewName +
                "</span>: <span style='color:#dfe5ef;'>hello chat</span>");
        auto *minimalPreset =
            addPresetCard("minimal", "Minimal",
                          "<span style='color:" + previewNameColor +
                              ";font-weight:600;'>" + previewName +
                              "</span>: <span style='color:#dfe5ef;'>hello chat</span>");
        auto *compactPreset = addPresetCard(
            "compact", "Compact",
            previewStatusBadgesHtml(true) + " " + "<span style='color:" +
                previewNameColor + ";font-weight:600;'>" + previewName +
                "</span> <span style='color:#dfe5ef;'>hello chat</span>",
            "<span style='color:#8d95a5;'>12:41</span>");
        auto *creatorPreset = addPresetCard(
            "creator", "Creator",
            QString("<span style='display:inline-block;width:14px;height:14px;"
                    "border-radius:7px;background:#7f4bff;'></span> ") +
                previewStatusBadgesHtml(true) + " " + "<span style='color:" +
                previewNameColor + ";font-weight:600;'>" + previewName +
                "</span> <span style='color:#dfe5ef;'>hello chat</span>",
            "<span style='color:#8d95a5;'>↩ 6</span>");
        auto *noTimestampPreset =
            addPresetCard("notimestamps", "No timestamps",
                          QString("<span style='display:inline-block;width:14px;height:14px;"
                                  "border-radius:7px;background:#7f4bff;'></span> ") +
                              previewStatusBadgesHtml(true) + " " +
                              "<span style='color:" + previewNameColor +
                              ";font-weight:600;'>" + previewName +
                              "</span>: <span style='color:#dfe5ef;'>hello chat</span>");

        {
            const auto presetId = getSettings()
                                      ->openEmoteOnboardingPreset.getValue()
                                      .trimmed()
                                      .toLower();
            QRadioButton *toSelect = classicPreset;
            for (auto *button : presetGroup->buttons())
            {
                if (button->property("presetId").toString() == presetId)
                {
                    toSelect = qobject_cast<QRadioButton *>(button);
                    break;
                }
            }
            if (toSelect != nullptr)
            {
                toSelect->setChecked(true);
            }
        }

        auto *presetDescription = new QLabel(pagePreset);
        presetDescription->setWordWrap(true);
        pagePresetLayout->addWidget(presetDescription);
        pagePresetLayout->addStretch(1);

        auto *pageLayout = new QWidget(&dialog);
        auto *pageLayoutL = new QVBoxLayout(pageLayout);
        pageLayoutL->setSpacing(8);
        wizard->addWidget(pageLayout);

        auto *layoutIntro = new QLabel(
            "Layout preferences\n\n"
            "Pick what chat should look like. This only applies initial defaults. "
            "Advanced startup options are hidden unless you expand them.",
            pageLayout);
        layoutIntro->setWordWrap(true);
        pageLayoutL->addWidget(layoutIntro);

        auto *timestampGroup = new QButtonGroup(&dialog);
        auto *timestampSectionLabel = new QLabel("Timestamp placement", pageLayout);
        timestampSectionLabel->setStyleSheet("font-weight: 600;");
        pageLayoutL->addWidget(timestampSectionLabel);
        using PreviewConfig = OpenEmoteOnboardingLivePreview::Config;
        auto addOptionPreviewRow = [&](QVBoxLayout *parentLayout,
                                       QWidget *parent,
                                       const PreviewConfig &previewConfig) {
            auto *frame = new QFrame(parent);
            frame->setStyleSheet(
                "QFrame { background: #14171f; border: 1px solid #303745; "
                "border-radius: 6px; }");
            auto *frameLayout = new QVBoxLayout(frame);
            frameLayout->setContentsMargins(8, 6, 8, 6);
            auto *preview = new OpenEmoteOnboardingLivePreview(frame);
            preview->setPreviewConfig(previewConfig);
            frameLayout->addWidget(preview);
            parentLayout->addWidget(frame);
        };
        auto addTimestampOption = [&](const QString &label, const QString &value,
                                      const PreviewConfig &previewConfig) {
            auto *radio = new QRadioButton(label, pageLayout);
            radio->setProperty("value", value);
            timestampGroup->addButton(radio);
            pageLayoutL->addWidget(radio);
            auto *previewWrap = new QWidget(pageLayout);
            auto *previewWrapLayout = new QVBoxLayout(previewWrap);
            previewWrapLayout->setContentsMargins(20, 0, 0, 0);
            addOptionPreviewRow(previewWrapLayout, previewWrap, previewConfig);
            pageLayoutL->addWidget(previewWrap);
        };
        addTimestampOption("Timestamp on left", "left",
                           PreviewConfig{.username = previewName,
                                         .showTimestamp = true,
                                         .timestampRight = false,
                                         .showReplyIcon = false,
                                         .showBadges = true,
                                         .alternateRows = false});
        addTimestampOption("Timestamp on right", "right",
                           PreviewConfig{.username = previewName,
                                         .showTimestamp = true,
                                         .timestampRight = true,
                                         .showReplyIcon = false,
                                         .showBadges = true,
                                         .alternateRows = false});
        addTimestampOption("No timestamp", "hidden",
                           PreviewConfig{.username = previewName,
                                         .showTimestamp = false,
                                         .timestampRight = false,
                                         .showReplyIcon = false,
                                         .showBadges = true,
                                         .alternateRows = false});
        auto *timestampHint = new QLabel(
            "Right-side timestamps stay pinned to the right edge for cleaner scanning.",
            pageLayout);
        timestampHint->setWordWrap(true);
        timestampHint->setStyleSheet("color: #8d95a5;");
        pageLayoutL->addWidget(timestampHint);

        auto *smartTimestamps = new QCheckBox(
            "Smart timestamps (show only when chat gap exceeds threshold)",
            pageLayout);
        pageLayoutL->addWidget(smartTimestamps);

        auto *gapMinutesLabel =
            new QLabel("Smart timestamp gap threshold (minutes)", pageLayout);
        pageLayoutL->addWidget(gapMinutesLabel);

        auto *gapMinutes = new QSpinBox(pageLayout);
        gapMinutes->setRange(1, 400);
        gapMinutes->setSingleStep(1);
        gapMinutes->setSuffix(" min");
        pageLayoutL->addWidget(gapMinutes);

        auto *badgeModeGroup = new QButtonGroup(&dialog);
        auto *badgeSectionLabel = new QLabel("Badge placement", pageLayout);
        badgeSectionLabel->setStyleSheet("font-weight: 600;");
        pageLayoutL->addWidget(badgeSectionLabel);
        auto addBadgeModeOption = [&](const QString &label, const QString &value,
                                      const PreviewConfig &previewConfig) {
            auto *radio = new QRadioButton(label, pageLayout);
            radio->setProperty("value", value);
            badgeModeGroup->addButton(radio);
            pageLayoutL->addWidget(radio);
            auto *previewWrap = new QWidget(pageLayout);
            auto *previewWrapLayout = new QVBoxLayout(previewWrap);
            previewWrapLayout->setContentsMargins(20, 0, 0, 0);
            addOptionPreviewRow(previewWrapLayout, previewWrap, previewConfig);
            pageLayoutL->addWidget(previewWrap);
        };
        addBadgeModeOption("Badges near username", "standard",
                           PreviewConfig{.username = previewName,
                                         .showTimestamp = true,
                                         .timestampRight = false,
                                         .showReplyIcon = false,
                                         .showBadges = true,
                                         .alternateRows = false});
        auto *badgeSoon = new QLabel(
            "Compact identity-rail badge placement preview is coming soon. "
            "Startup wizard currently applies stable badge placement near username.",
            pageLayout);
        badgeSoon->setWordWrap(true);
        badgeSoon->setStyleSheet("color: #8d95a5;");
        pageLayoutL->addWidget(badgeSoon);

        auto *advancedToggle = new QCheckBox(
            "Show advanced startup options", pageLayout);
        advancedToggle->setChecked(false);
        pageLayoutL->addWidget(advancedToggle);

        QList<QWidget *> advancedWidgets;
        auto addAdvanced = [&advancedWidgets](QWidget *widget) {
            if (widget != nullptr)
            {
                advancedWidgets.push_back(widget);
            }
        };

        auto *badgeLayoutLabel = new QLabel(
            "Compact badge stack layout (used when compact identity is enabled)",
            pageLayout);
        pageLayoutL->addWidget(badgeLayoutLabel);
        addAdvanced(badgeLayoutLabel);

        auto *badgeShapeGroup = new QButtonGroup(&dialog);
        auto addBadgeShapeOption = [&](const QString &label, const QString &value) {
            auto *radio = new QRadioButton(label, pageLayout);
            radio->setProperty("value", value);
            badgeShapeGroup->addButton(radio);
            pageLayoutL->addWidget(radio);
            addAdvanced(radio);
        };
        addBadgeShapeOption("Corner badges: 1x1x1x1 vertical", "linear-vertical");
        addBadgeShapeOption("Corner badges: 1x1x1x1 horizontal",
                            "linear-horizontal");

        auto *badgeAnchorLabel = new QLabel(
            "Compact badge anchor (left/right force vertical stack)", pageLayout);
        pageLayoutL->addWidget(badgeAnchorLabel);
        addAdvanced(badgeAnchorLabel);

        auto *badgeAnchorGroup = new QButtonGroup(&dialog);
        auto addBadgeAnchorOption = [&](const QString &label, const QString &value) {
            auto *radio = new QRadioButton(label, pageLayout);
            radio->setProperty("value", value);
            badgeAnchorGroup->addButton(radio);
            pageLayoutL->addWidget(radio);
            addAdvanced(radio);
        };
        addBadgeAnchorOption("Badge anchor: left", "left");
        addBadgeAnchorOption("Badge anchor: right", "right");
        addBadgeAnchorOption("Badge anchor: top", "top");
        addBadgeAnchorOption("Badge anchor: bottom", "bottom");

        auto *keepNames = new QCheckBox(
            "Keep visible usernames with compact identity", pageLayout);
        pageLayoutL->addWidget(keepNames);
        addAdvanced(keepNames);

        auto *showReplyButton =
            new QCheckBox("Show reply icon on each message", pageLayout);
        showReplyButton->setChecked(false);
        showReplyButton->setToolTip(
            "Off by default. Enable only if you want per-message reply icons.");
        pageLayoutL->addWidget(showReplyButton);
        addAdvanced(showReplyButton);

        auto *alternateRows =
            new QCheckBox("Alternate message background rows", pageLayout);
        pageLayoutL->addWidget(alternateRows);
        addAdvanced(alternateRows);

        auto *preferThreadDrawer = new QCheckBox(
            "Prefer thread drawer (Shift to force popout)", pageLayout);
        pageLayoutL->addWidget(preferThreadDrawer);
        addAdvanced(preferThreadDrawer);

        auto *fontFamilyLabel = new QLabel("Chat font family", pageLayout);
        pageLayoutL->addWidget(fontFamilyLabel);
        addAdvanced(fontFamilyLabel);

        auto *fontFamilyCombo = new QComboBox(pageLayout);
        fontFamilyCombo->setSizeAdjustPolicy(QComboBox::AdjustToContents);
        {
            QStringList families = QFontDatabase::families();
            families.removeDuplicates();
            families.sort(Qt::CaseInsensitive);
            for (const auto &family : families)
            {
                fontFamilyCombo->addItem(family, family);
            }

            auto currentFamily =
                getSettings()->chatFontFamily.getValue().trimmed();
            if (currentFamily.isEmpty())
            {
                currentFamily = QFont().family();
            }
            auto idx = fontFamilyCombo->findData(currentFamily);
            if (idx < 0)
            {
                fontFamilyCombo->addItem(currentFamily, currentFamily);
                idx = fontFamilyCombo->findData(currentFamily);
            }
            fontFamilyCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        pageLayoutL->addWidget(fontFamilyCombo);
        addAdvanced(fontFamilyCombo);

        auto *fontWeightLabel = new QLabel("Chat font weight", pageLayout);
        pageLayoutL->addWidget(fontWeightLabel);
        addAdvanced(fontWeightLabel);

        auto *fontWeightCombo = new QComboBox(pageLayout);
        fontWeightCombo->addItem("Light", int(QFont::Light));
        fontWeightCombo->addItem("Normal", int(QFont::Normal));
        fontWeightCombo->addItem("Medium", int(QFont::Medium));
        fontWeightCombo->addItem("Demi Bold", int(QFont::DemiBold));
        fontWeightCombo->addItem("Bold", int(QFont::Bold));
        fontWeightCombo->addItem("Black", int(QFont::Black));
        {
            const auto currentWeight = getSettings()->chatFontWeight.getValue();
            auto idx = fontWeightCombo->findData(currentWeight);
            if (idx < 0)
            {
                fontWeightCombo->addItem(QString::number(currentWeight),
                                         currentWeight);
                idx = fontWeightCombo->findData(currentWeight);
            }
            fontWeightCombo->setCurrentIndex(idx >= 0 ? idx : 0);
        }
        pageLayoutL->addWidget(fontWeightCombo);
        addAdvanced(fontWeightCombo);

        auto *reportActions = new QCheckBox(
            "Enable OpenEmote report actions (emote/message/thread)", pageLayout);
        reportActions->setChecked(getSettings()->openEmoteEnableReportActions);
        pageLayoutL->addWidget(reportActions);
        addAdvanced(reportActions);

        auto *apiReports = new QCheckBox(
            "Enable direct API report submission when configured", pageLayout);
        apiReports->setChecked(getSettings()->openEmoteEnableApiReports);
        apiReports->setEnabled(reportActions->isChecked());
        pageLayoutL->addWidget(apiReports);
        addAdvanced(apiReports);
        QObject::connect(reportActions, &QCheckBox::toggled, apiReports,
                         &QWidget::setEnabled);

        auto setAdvancedVisible = [&advancedWidgets](bool visible) {
            for (auto *widget : advancedWidgets)
            {
                if (widget != nullptr)
                {
                    widget->setVisible(visible);
                }
            }
        };
        QObject::connect(advancedToggle, &QCheckBox::toggled, &dialog,
                         [setAdvancedVisible](bool checked) {
                             setAdvancedVisible(checked);
                         });
        setAdvancedVisible(false);

        auto *layoutStateNote = new QLabel(
            "All choices remain editable later in Settings.",
            pageLayout);
        layoutStateNote->setWordWrap(true);
        pageLayoutL->addWidget(layoutStateNote);

        auto *livePreviewTitle = new QLabel("Live preview", pageLayout);
        livePreviewTitle->setStyleSheet("font-weight: 600;");
        pageLayoutL->addWidget(livePreviewTitle);
        auto *livePreviewWrap = new QFrame(pageLayout);
        livePreviewWrap->setStyleSheet(
            "QFrame { background: #14171f; border: 1px solid #303745; "
            "border-radius: 6px; }");
        auto *livePreviewWrapLayout = new QVBoxLayout(livePreviewWrap);
        livePreviewWrapLayout->setContentsMargins(8, 6, 8, 6);
        auto *livePreviewWidget =
            new OpenEmoteOnboardingLivePreview(livePreviewWrap);
        livePreviewWrapLayout->addWidget(livePreviewWidget);
        auto *livePreviewMeta = new QLabel(livePreviewWrap);
        livePreviewMeta->setTextFormat(Qt::RichText);
        livePreviewMeta->setWordWrap(true);
        livePreviewWrapLayout->addWidget(livePreviewMeta);
        pageLayoutL->addWidget(livePreviewWrap);
        pageLayoutL->addStretch(1);

        auto checkedGroupValue = [](QButtonGroup *group,
                                    const QString &fallback) -> QString {
            if (group == nullptr || group->checkedButton() == nullptr)
            {
                return fallback;
            }
            const auto value =
                group->checkedButton()->property("value").toString().trimmed().toLower();
            return value.isEmpty() ? fallback : value;
        };
        auto setCheckedGroupValue = [](QButtonGroup *group,
                                       const QString &value) {
            if (group == nullptr)
            {
                return;
            }
            for (auto *button : group->buttons())
            {
                if (button->property("value").toString().trimmed().toLower() ==
                    value)
                {
                    button->setChecked(true);
                    return;
                }
            }
        };
        auto selectedPresetId = [presetGroup]() -> QString {
            if (presetGroup == nullptr || presetGroup->checkedButton() == nullptr)
            {
                return "classic";
            }
            auto id = presetGroup->checkedButton()
                          ->property("presetId")
                          .toString()
                          .trimmed()
                          .toLower();
            return id.isEmpty() ? "classic" : id;
        };
        auto updateLivePreview = [checkedGroupValue, timestampGroup, badgeModeGroup,
                                  showReplyButton, smartTimestamps, gapMinutes,
                                  alternateRows, livePreviewWidget,
                                  livePreviewMeta, resolvePreviewName]() {
            const auto timestampMode = checkedGroupValue(timestampGroup, "left");
            const auto badgeMode = checkedGroupValue(badgeModeGroup, "standard");
            const auto previewName = resolvePreviewName();
            livePreviewWidget->setPreviewConfig(
                OpenEmoteOnboardingLivePreview::Config{
                    .username = previewName,
                    .showTimestamp = timestampMode != "hidden",
                    .timestampRight = timestampMode == "right",
                    .showReplyIcon = showReplyButton->isChecked(),
                    .showBadges = badgeMode != "compact",
                    .alternateRows = alternateRows->isChecked(),
                });
            const auto smartLine = (timestampMode == "hidden")
                                       ? QString("<span style='color:#7b8493;'>Smart timestamps disabled (no timestamps)</span>")
                                       : QString("<span style='color:#7b8493;'>Smart timestamps: %1 (gap %2 min)</span>")
                                             .arg(smartTimestamps->isChecked() ? "ON" : "OFF")
                                             .arg(gapMinutes->value());
            livePreviewMeta->setText(smartLine);
        };

        auto updateSmartTimestampEnabled = [checkedGroupValue, timestampGroup,
                                            smartTimestamps, gapMinutesLabel,
                                            gapMinutes]() {
            const auto timestampMode = checkedGroupValue(timestampGroup, "left");
            const bool enabled = timestampMode != "hidden";
            smartTimestamps->setEnabled(enabled);
            gapMinutesLabel->setEnabled(enabled && smartTimestamps->isChecked());
            gapMinutes->setEnabled(enabled && smartTimestamps->isChecked());
        };

        auto updateLayoutFromPreset =
            [selectedPresetId, presetDescription, setCheckedGroupValue,
             timestampGroup, badgeModeGroup, badgeShapeGroup, keepNames,
             showReplyButton, alternateRows, preferThreadDrawer,
             badgeAnchorGroup, smartTimestamps, gapMinutes, importSettings,
             legacyLayoutBaseline,
             updateSmartTimestampEnabled, updateLivePreview]() {
                const auto presetId = selectedPresetId();
                OpenEmoteOnboardingLayoutState state;
                if (importSettings->isChecked())
                {
                    state = legacyLayoutBaseline.value_or(
                        captureOnboardingLayoutState(*getSettings()));
                    presetDescription->setText(
                        "Import baseline active. Detected legacy settings are used as the "
                        "default where available; you can still tune options below.");
                }
                else
                {
                    state = onboardingPreset(presetId, *getSettings());
                    presetDescription->setText(
                        onboardingPresetDescription(presetId));
                }

                if (!state.showTimestamps)
                {
                    setCheckedGroupValue(timestampGroup, "hidden");
                }
                else if (state.compactAuthorIdentity)
                {
                    setCheckedGroupValue(timestampGroup, "right");
                }
                else
                {
                    setCheckedGroupValue(timestampGroup, "left");
                }

                setCheckedGroupValue(badgeModeGroup, "standard");

                const auto anchor = normalizeAvatarBadgeAnchor(state.avatarBadgeAnchor);
                setCheckedGroupValue(badgeAnchorGroup, anchor);
                setCheckedGroupValue(
                    badgeShapeGroup,
                    (anchor == "left" || anchor == "right") ? "linear-vertical"
                                                            : "linear-horizontal");

                keepNames->setChecked(state.compactKeepNames);
                showReplyButton->setChecked(state.showReplyButton);
                alternateRows->setChecked(state.alternateMessages);
                preferThreadDrawer->setChecked(state.preferThreadDrawer);
                smartTimestamps->setChecked(state.timestampGapsOnly);
                gapMinutes->setValue(std::clamp(state.timestampGapMinutes, 1, 400));
                updateSmartTimestampEnabled();
                updateLivePreview();
            };
        QObject::connect(
            presetGroup,
            qOverload<QAbstractButton *>(&QButtonGroup::buttonClicked),
            &dialog, [updateLayoutFromPreset](QAbstractButton *) {
                updateLayoutFromPreset();
            });
        QObject::connect(importSettings, &QCheckBox::toggled, &dialog,
                         [updateLayoutFromPreset](bool) {
                             updateLayoutFromPreset();
                         });

        auto updateBadgeShapeEnabled = [checkedGroupValue, setCheckedGroupValue,
                                        badgeShapeGroup, badgeAnchorGroup]() {
            const auto anchor = checkedGroupValue(badgeAnchorGroup, "left");
            const bool forceVertical = anchor == "left" || anchor == "right";
            if (forceVertical)
            {
                setCheckedGroupValue(badgeShapeGroup, "linear-vertical");
            }
            for (auto *button : badgeShapeGroup->buttons())
            {
                button->setEnabled(!forceVertical);
            }
        };
        QObject::connect(
            badgeAnchorGroup,
            qOverload<QAbstractButton *>(&QButtonGroup::buttonClicked),
            &dialog, [updateBadgeShapeEnabled](QAbstractButton *) {
                updateBadgeShapeEnabled();
            });
        QObject::connect(
            timestampGroup,
            qOverload<QAbstractButton *>(&QButtonGroup::buttonClicked),
            &dialog, [updateSmartTimestampEnabled](QAbstractButton *) {
                updateSmartTimestampEnabled();
            });
        QObject::connect(
            timestampGroup,
            qOverload<QAbstractButton *>(&QButtonGroup::buttonClicked),
            &dialog, [updateLivePreview](QAbstractButton *) {
                updateLivePreview();
            });
        QObject::connect(smartTimestamps, &QCheckBox::toggled, &dialog,
                         [updateSmartTimestampEnabled](bool) {
                             updateSmartTimestampEnabled();
                         });
        QObject::connect(smartTimestamps, &QCheckBox::toggled, &dialog,
                         [updateLivePreview](bool) { updateLivePreview(); });
        QObject::connect(gapMinutes, qOverload<int>(&QSpinBox::valueChanged),
                         &dialog, [updateLivePreview](int) {
                             updateLivePreview();
                         });
        QObject::connect(
            badgeModeGroup,
            qOverload<QAbstractButton *>(&QButtonGroup::buttonClicked),
            &dialog, [updateLivePreview](QAbstractButton *) {
                updateLivePreview();
            });
        QObject::connect(showReplyButton, &QCheckBox::toggled, &dialog,
                         [updateLivePreview](bool) { updateLivePreview(); });
        QObject::connect(alternateRows, &QCheckBox::toggled, &dialog,
                         [updateLivePreview](bool) { updateLivePreview(); });

        updateLayoutFromPreset();
        updateBadgeShapeEnabled();
        updateLivePreview();

        auto *navRow = new QHBoxLayout();
        navRow->setContentsMargins(0, 6, 0, 0);
        navRow->addStretch(1);
        auto *backButton = new QPushButton("Back", &dialog);
        auto *nextButton = new QPushButton("Next", &dialog);
        auto *applyButton = new QPushButton("Apply", &dialog);
        auto *skipButton = new QPushButton("Skip", &dialog);
        applyButton->setDefault(true);
        navRow->addWidget(backButton);
        navRow->addWidget(nextButton);
        navRow->addWidget(applyButton);
        navRow->addWidget(skipButton);
        rootLayout->addLayout(navRow);

        const int importPageIndex = wizard->indexOf(pageImport);
        const int presetPageIndex = wizard->indexOf(pagePreset);
        const int layoutPageIndex = wizard->indexOf(pageLayout);

        auto nextPageIndex = [streamerModeSetup, importPageIndex,
                              presetPageIndex, layoutPageIndex](int current) {
            if (current == importPageIndex && !streamerModeSetup->isChecked())
            {
                return presetPageIndex;
            }
            return std::min(layoutPageIndex, current + 1);
        };
        auto previousPageIndex = [streamerModeSetup, importPageIndex,
                                  presetPageIndex](int current) {
            if (current == presetPageIndex && !streamerModeSetup->isChecked())
            {
                return importPageIndex;
            }
            return std::max(importPageIndex, current - 1);
        };

        auto updateWizardButtons = [wizard, backButton, nextButton, applyButton,
                                    importPageIndex, layoutPageIndex,
                                    previousPageIndex]() {
            const auto idx = wizard->currentIndex();
            const auto prevIdx = previousPageIndex(idx);
            backButton->setEnabled(idx > importPageIndex && prevIdx != idx);
            nextButton->setVisible(idx < layoutPageIndex);
            applyButton->setVisible(idx == layoutPageIndex);
        };

        QObject::connect(backButton, &QPushButton::clicked, &dialog,
                         [wizard, updateWizardButtons, previousPageIndex]() {
                             wizard->setCurrentIndex(
                                 previousPageIndex(wizard->currentIndex()));
                             updateWizardButtons();
                         });
        QObject::connect(nextButton, &QPushButton::clicked, &dialog,
                         [wizard, updateWizardButtons, nextPageIndex]() {
                             wizard->setCurrentIndex(
                                 nextPageIndex(wizard->currentIndex()));
                             updateWizardButtons();
                         });
        QObject::connect(streamerModeSetup, &QCheckBox::toggled, &dialog,
                         [updateWizardButtons](bool) { updateWizardButtons(); });
        QObject::connect(applyButton, &QPushButton::clicked, &dialog,
                         &QDialog::accept);
        QObject::connect(skipButton, &QPushButton::clicked, &dialog,
                         &QDialog::reject);
        updateWizardButtons();

        const bool onboardingAutodrive =
            qEnvironmentVariableIntValue("OPENEMOTE_ONBOARDING_AUTODRIVE") != 0;
        if (onboardingAutodrive)
        {
            const auto autodrivePreset =
                qEnvironmentVariable("OPENEMOTE_ONBOARDING_AUTODRIVE_PROFILE")
                    .trimmed()
                    .toLower();
            const auto autodriveTimestamp =
                qEnvironmentVariable("OPENEMOTE_ONBOARDING_AUTODRIVE_TIMESTAMP")
                    .trimmed()
                    .toLower();
            const bool autodriveTimestampMatrix =
                qEnvironmentVariableIntValue(
                    "OPENEMOTE_ONBOARDING_AUTODRIVE_TIMESTAMP_MATRIX") != 0;
            const bool autodriveAdvanced =
                qEnvironmentVariableIntValue(
                    "OPENEMOTE_ONBOARDING_AUTODRIVE_ADVANCED") != 0;
            const auto autodriveScreenDir =
                qEnvironmentVariable("OPENEMOTE_ONBOARDING_AUTODRIVE_SCREEN_DIR")
                    .trimmed();
            const auto gapRaw =
                qEnvironmentVariable("OPENEMOTE_ONBOARDING_AUTODRIVE_GAP_MINUTES")
                    .trimmed();
            bool gapOk = false;
            const int autodriveGap = std::clamp(gapRaw.toInt(&gapOk), 1, 400);
            if (!autodriveScreenDir.isEmpty())
            {
                QDir().mkpath(autodriveScreenDir);
            }
            auto captureStep = [&dialog, autodriveScreenDir](const QString &name) {
                if (autodriveScreenDir.isEmpty())
                {
                    return;
                }
                dialog.grab().save(
                    QDir(autodriveScreenDir).filePath(name + ".png"));
            };
            auto writeStepState = [&autodriveScreenDir](const QString &name,
                                                        const QJsonObject &state) {
                if (autodriveScreenDir.isEmpty())
                {
                    return;
                }
                QFile file(QDir(autodriveScreenDir).filePath(name + ".json"));
                if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
                {
                    return;
                }
                file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
            };
            auto applyPresetById = [&presetGroup, &updateLayoutFromPreset](
                                       const QString &id) {
                for (auto *button : presetGroup->buttons())
                {
                    if (button->property("presetId").toString() == id)
                    {
                        button->setChecked(true);
                        updateLayoutFromPreset();
                        return;
                    }
                }
            };

            QTimer::singleShot(250, &dialog, [captureStep]() {
                captureStep("step1_import");
            });
            QTimer::singleShot(500, &dialog, [nextButton]() {
                nextButton->click();
            });
            QTimer::singleShot(
                800, &dialog,
                [captureStep, applyPresetById, autodrivePreset]() {
                    if (!autodrivePreset.isEmpty())
                    {
                        applyPresetById(autodrivePreset);
                    }
                    captureStep("step2_preset");
                });
            QTimer::singleShot(1100, &dialog, [nextButton]() {
                nextButton->click();
            });
            QTimer::singleShot(
                1450, &dialog,
                [captureStep, setCheckedGroupValue, timestampGroup,
                 badgeModeGroup, showReplyButton, smartTimestamps, gapMinutes,
                 &dialog, scrollArea, advancedToggle, updateLivePreview, autodriveTimestamp,
                 autodriveGap, gapOk, autodriveAdvanced, writeStepState,
                 autodriveTimestampMatrix]() {
                    const auto timestamp =
                        (autodriveTimestamp == "left" ||
                         autodriveTimestamp == "right" ||
                         autodriveTimestamp == "hidden")
                            ? autodriveTimestamp
                            : "right";
                    setCheckedGroupValue(timestampGroup, timestamp);
                    setCheckedGroupValue(badgeModeGroup, "standard");
                    showReplyButton->setChecked(false);
                    smartTimestamps->setChecked(true);
                    if (gapOk)
                    {
                        gapMinutes->setValue(autodriveGap);
                    }
                    updateLivePreview();
                    writeStepState("step3_state",
                                   QJsonObject{
                                       {"timestamp_mode", timestamp},
                                       {"badge_mode", "standard"},
                                       {"show_reply_icon",
                                        showReplyButton->isChecked()},
                                       {"smart_timestamps",
                                        smartTimestamps->isChecked()},
                                       {"gap_minutes", gapMinutes->value()},
                                       {"advanced_visible",
                                        advancedToggle->isChecked()},
                                   });
                    if (autodriveTimestampMatrix)
                    {
                        setCheckedGroupValue(timestampGroup, "left");
                        updateLivePreview();
                        captureStep("step3_timestamp_left");

                        setCheckedGroupValue(timestampGroup, "right");
                        updateLivePreview();
                        captureStep("step3_timestamp_right");

                        setCheckedGroupValue(timestampGroup, "hidden");
                        updateLivePreview();
                        captureStep("step3_timestamp_hidden");

                        setCheckedGroupValue(timestampGroup, timestamp);
                        updateLivePreview();
                    }
                    captureStep("step3_layout");
                    if (autodriveAdvanced)
                    {
                        advancedToggle->setChecked(true);
                        updateLivePreview();
                        QTimer::singleShot(220, &dialog, [captureStep, scrollArea, updateLivePreview]() {
                            updateLivePreview();
                            captureStep("step3_layout_advanced");
                            if (scrollArea != nullptr &&
                                scrollArea->verticalScrollBar() != nullptr)
                            {
                                scrollArea->verticalScrollBar()->setValue(
                                    scrollArea->verticalScrollBar()->maximum());
                                captureStep("step3_layout_advanced_bottom");
                            }
                        });
                        writeStepState("step3_state_advanced",
                                       QJsonObject{
                                           {"advanced_visible", true},
                                           {"show_reply_icon",
                                            showReplyButton->isChecked()},
                                           {"smart_timestamps",
                                            smartTimestamps->isChecked()},
                                           {"gap_minutes", gapMinutes->value()},
                                       });
                    }
                });
            QTimer::singleShot(autodriveAdvanced ? 2300 : 1900, &dialog, [applyButton]() {
                applyButton->click();
            });
        }

        const auto accepted = dialog.exec() == QDialog::Accepted;
        if (accepted)
        {
            auto *settings = getSettings();
            const auto beforeState = captureOnboardingLayoutState(*settings);

            getSettings()->openEmoteEnableReportActions =
                reportActions->isChecked();
            getSettings()->openEmoteEnableApiReports = apiReports->isChecked();

            const auto presetId = selectedPresetId();
            auto appliedState = importSettings->isChecked()
                                    ? legacyLayoutBaseline.value_or(
                                          captureOnboardingLayoutState(
                                              *settings))
                                    : onboardingPreset(presetId, *settings);

            const auto timestampMode =
                checkedGroupValue(timestampGroup, "left");
            if (timestampMode == "hidden")
            {
                appliedState.showTimestamps = false;
            }
            else if (timestampMode == "right")
            {
                appliedState.showTimestamps = true;
                appliedState.compactAuthorIdentity = false;
            }
            else
            {
                appliedState.showTimestamps = true;
                appliedState.compactAuthorIdentity = false;
            }

            const auto badgeMode = checkedGroupValue(badgeModeGroup, "standard");
            if (badgeMode == "standard")
            {
                appliedState.compactAuthorIdentity = false;
                appliedState.avatarDecorators = false;
                appliedState.avatarCornerBadges = false;
                appliedState.identityRail = false;
            }
            else
            {
                appliedState.compactAuthorIdentity = false;
                appliedState.avatarDecorators = false;
                appliedState.avatarCornerBadges = false;
                appliedState.identityRail = false;
            }

            appliedState.compactKeepNames = keepNames->isChecked();
            appliedState.showReplyButton = showReplyButton->isChecked();
            appliedState.alternateMessages = alternateRows->isChecked();
            appliedState.preferThreadDrawer = preferThreadDrawer->isChecked();
            appliedState.showThreadActivity = preferThreadDrawer->isChecked();
            appliedState.timestampGapsOnly = smartTimestamps->isChecked();
            appliedState.timestampGapMinutes =
                std::clamp(gapMinutes->value(), 1, 400);
            const auto badgeAnchor =
                normalizeAvatarBadgeAnchor(
                    checkedGroupValue(badgeAnchorGroup, "left"));
            appliedState.avatarBadgeAnchor = badgeAnchor;

            applyOnboardingLayoutState(*settings, appliedState);
            const auto badgeLayout =
                checkedGroupValue(badgeShapeGroup, "linear-vertical");
            const bool forceVerticalStack =
                badgeAnchor == "left" || badgeAnchor == "right";
            settings->openEmoteAvatarBadgeLinear = true;
            settings->openEmoteAvatarBadgeLinearVertical =
                forceVerticalStack || badgeLayout == "linear-vertical";
            settings->openEmoteOnboardingPreset = presetId;
            settings->openEmoteUseVisualMessageLimit = true;
            settings->openEmoteVisualMessageLimit = 500;
            settings->chatFontFamily =
                fontFamilyCombo->currentData().toString().trimmed();
            settings->chatFontWeight = fontWeightCombo->currentData().toInt();

            const auto afterState = captureOnboardingLayoutState(*settings);
            const auto changedFields =
                onboardingChangedFieldCount(beforeState, afterState);

            const bool streamerModeEnabled = streamerModeSetup->isChecked();
            const bool oauthConnectRequested =
                streamerModeEnabled && streamerOauthNow->isChecked();
            const auto hostingMode = streamerSelfHost->isChecked()
                                         ? QString("self-host")
                                         : (streamerHosted->isChecked()
                                                ? QString("openemote-hosted")
                                                : QString("openemote-hosted"));

            bool selfHostConfigured = false;
            QString selfHostRegisterEndpoint;
            if (streamerModeEnabled && streamerSelfHost->isChecked())
            {
                auto baseUrl = selfHostBaseUrl->text().trimmed();
                while (baseUrl.endsWith('/'))
                {
                    baseUrl.chop(1);
                }

                const QUrl parsed(baseUrl);
                const bool validHttps =
                    parsed.isValid() && parsed.scheme() == "https" &&
                    !parsed.host().trimmed().isEmpty();
                if (!validHttps)
                {
                    QMessageBox::warning(
                        parent, "OpenEmote self-host validation",
                        "Self-host API base URL must be a valid https URL. "
                        "Skipping self-host setup for now.");
                }
                else
                {
                    const auto token = selfHostToken->text().trimmed();
                    const auto emoteEndpoint = baseUrl + "/self-host/emote";
                    const auto badgeEndpoint = baseUrl + "/self-host/badge";
                    const auto emoteBulkEndpoint =
                        baseUrl + "/self-host/emote-bulk";
                    const auto badgeBulkEndpoint =
                        baseUrl + "/self-host/badge-bulk";
                    selfHostRegisterEndpoint =
                        baseUrl + "/self-host/register";

                    settings->imageUploaderEnabled = true;
                    settings->imageUploaderUrl = emoteEndpoint;
                    settings->imageUploaderFormField = "file";
                    QStringList headers;
                    if (!token.isEmpty())
                    {
                        headers << QString("Authorization: Bearer %1")
                                       .arg(token);
                    }
                    headers << "X-OpenEmote-Client: chatterino-openemote";
                    headers << "X-OpenEmote-Sync-Mode: hybrid";
                    headers << QString("X-OpenEmote-Badge-Endpoint: %1")
                                   .arg(badgeEndpoint);
                    headers << QString("X-OpenEmote-Emote-Bulk-Endpoint: %1")
                                   .arg(emoteBulkEndpoint);
                    headers << QString("X-OpenEmote-Badge-Bulk-Endpoint: %1")
                                   .arg(badgeBulkEndpoint);
                    settings->imageUploaderHeaders = headers.join('\n');
                    settings->imageUploaderLink = "{url}";
                    settings->imageUploaderDeletionLink = "{delete_url}";
                    settings->openEmoteEnableCustomBadgePacks = true;
                    settings->openEmoteAllowUntrustedBadgePacks = false;
                    selfHostConfigured = true;
                }
            }

            int importedSettingsFiles = 0;
            if (importSettings->isChecked() && !legacyDirs.isEmpty())
            {
                const auto sourceDir = legacyDirs.front();
                importedSettingsFiles = openemote::importLegacySettingsFiles(
                    sourceDir, app->getPaths().settingsDirectory);

                if (importedSettingsFiles > 0)
                {
                    qCInfo(chatterinoApp)
                        << "OpenEmote onboarding imported" << importedSettingsFiles
                        << "settings file(s) from" << sourceDir;
                }
            }

            int importedAccounts = 0;
            if (importLogin->isChecked() && !legacyDirs.isEmpty())
            {
                importedAccounts =
                    importLegacyTwitchAccounts(app, legacyDirs.front());

                if (importedAccounts > 0)
                {
                    qCInfo(chatterinoApp)
                        << "OpenEmote onboarding imported"
                        << importedAccounts << "Twitch account(s)";
                }
            }

            QMessageBox::information(
                parent, "OpenEmote onboarding applied",
                QString("Preset: %1\nChanged layout fields: %2\nImported settings files: %3\nImported Twitch accounts: %4\nStreamer setup enabled: %5\nStreamer hosting mode: %6\nOAuth connect requested: %7\nSelf-host configured: %8%9")
                    .arg(presetGroup->checkedButton() != nullptr
                             ? presetGroup->checkedButton()->text()
                             : QString("Classic"))
                    .arg(changedFields)
                    .arg(importedSettingsFiles)
                    .arg(importedAccounts)
                    .arg(streamerModeEnabled ? "yes" : "no")
                    .arg(hostingMode)
                    .arg(oauthConnectRequested ? "yes" : "no")
                    .arg(selfHostConfigured ? "yes" : "no")
                    .arg(selfHostConfigured
                             ? QString("\nSelf-host register endpoint: %1")
                                   .arg(selfHostRegisterEndpoint)
                             : QString()));
        }

        getSettings()->openEmoteOnboardingShown = true;
        getSettings()->openEmoteOnboardingRevision =
            OPENEMOTE_ONBOARDING_REVISION;
        getSettings()->requestSave();
        clearScheduledFlag();
    });
}

ISoundController *makeSoundController(Settings &settings)
{
    SoundBackend soundBackend = settings.soundBackend;
    switch (soundBackend)
    {
        case SoundBackend::Miniaudio: {
            return new MiniaudioBackend();
        }
        break;

        case SoundBackend::Null: {
            return new NullBackend();
        }
        break;

        default: {
            return new MiniaudioBackend();
        }
        break;
    }
}

BttvLiveUpdates *makeBttvLiveUpdates(Settings &settings)
{
    bool enabled =
        settings.enableBTTVLiveUpdates &&
        (settings.enableBTTVChannelEmotes || settings.showBadgesBttv);

    if (enabled)
    {
        return new BttvLiveUpdates(BTTV_LIVE_UPDATES_URL);
    }

    return nullptr;
}

SeventvEventAPI *makeSeventvEventAPI(Settings &settings)
{
    bool enabled = settings.enableSevenTVEventAPI;

    if (enabled)
    {
        return new SeventvEventAPI(SEVENTV_EVENTAPI_URL);
    }

    return nullptr;
}

eventsub::IController *makeEventSubController(Settings &settings)
{
    bool enabled = settings.enableExperimentalEventSub;

    if (enabled)
    {
        return new eventsub::Controller();
    }

    return new eventsub::DummyController();
}

const QString TWITCH_PUBSUB_URL = "wss://pubsub-edge.twitch.tv";

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
IApplication *INSTANCE = nullptr;

}  // namespace

namespace chatterino {

IApplication::IApplication()
{
    INSTANCE = this;
}

IApplication::~IApplication()
{
    INSTANCE = nullptr;
}

// this class is responsible for handling the workflow of Chatterino
// It will create the instances of the major classes, and connect their signals
// to each other

Application::Application(Settings &_settings, const Paths &paths,
                         const Args &_args, Updates &_updates)
    : paths_(paths)
    , args_(_args)
    , themes(new Theme(paths))
    , fonts(new Fonts(_settings))
    , logging(new Logging(_settings))
    , emotes(new EmoteController)
    , accounts(new AccountController)
    , eventSub(makeEventSubController(_settings))
    , hotkeys(new HotkeyController)
    , windows(new WindowManager(_args, paths, _settings, *this->themes,
                                *this->fonts))
    , toasts(new Toasts)
    , imageUploader(new ImageUploader)
    , seventvAPI(new SeventvAPI)
    , crashHandler(new CrashHandler(paths))

    , commands(new CommandController(paths))
    , notifications(new NotificationController)
    , highlights(new HighlightController(_settings, this->accounts.get()))
    , twitch(new TwitchIrcServer)
    , ffzBadges(new FfzBadges)
    , bttvBadges(new BttvBadges)
    , seventvBadges(new SeventvBadges)
    , userData(new UserDataController(paths))
    , sound(makeSoundController(_settings))
    , twitchLiveController(new TwitchLiveController)
    , twitchPubSub(new PubSub(TWITCH_PUBSUB_URL))
    , twitchBadges(new TwitchBadges)
    , chatterinoBadges(new ChatterinoBadges)
    , bttvEmotes(new BttvEmotes)
    , bttvLiveUpdates(makeBttvLiveUpdates(_settings))
    , ffzEmotes(new FfzEmotes)
    , seventvEmotes(new SeventvEmotes)
    , seventvEventAPI(makeSeventvEventAPI(_settings))
    , linkResolver(new LinkResolver)
    , streamerMode(new StreamerMode)
    , twitchUsers(new TwitchUsers)
    , pronouns(new pronouns::Pronouns)
    , spellChecker(new SpellChecker)
    , platforms(new platform::PlatformRegistry)
#ifdef CHATTERINO_HAVE_PLUGINS
    , plugins(new PluginController(paths))
#endif
    , nmServer(new NativeMessagingServer())
    , updates(_updates)
{
}

Application::~Application()
{
    // we do this early to ensure getApp isn't used in any dtors
    INSTANCE = nullptr;
}

void Application::initialize(Settings &settings, const Paths &paths)
{
    assert(!this->initialized);

    // Show changelog
    if (!this->args_.isFramelessEmbed &&
        getSettings()->currentVersion.getValue() != "" &&
        getSettings()->currentVersion.getValue() != CHATTERINO_VERSION)
    {
        auto *box = new QMessageBox(QMessageBox::Information, "Chatterino 2",
                                    "Show changelog?",
                                    QMessageBox::Yes | QMessageBox::No);
        box->setAttribute(Qt::WA_DeleteOnClose);
        if (box->exec() == QMessageBox::Yes)
        {
            QDesktopServices::openUrl(
                QUrl("https://www.chatterino.com/changelog"));
        }
    }

    if (!this->args_.isFramelessEmbed)
    {
        getSettings()->currentVersion.setValue(CHATTERINO_VERSION);
    }
    this->emotes->initialize();

    this->accounts->load();
    applyOpenEmoteIntegrationFromArgs(this->args_);

    this->windows->initialize();

    this->ffzBadges->load();

    // Load global emotes
    this->bttvEmotes->loadEmotes();
    this->ffzEmotes->loadEmotes();
    this->seventvEmotes->loadGlobalEmotes();

    this->twitch->initialize();

    // Load live status
    this->notifications->initialize();

    // XXX: Loading Twitch badges after Helix has been initialized, which only happens after
    // the AccountController initialize has been called
    this->twitchBadges->loadTwitchBadges();

#ifdef CHATTERINO_HAVE_PLUGINS
    this->plugins->initialize(settings);
#endif

    if (!this->args_.isFramelessEmbed)
    {
        this->initNm(paths);
    }

    this->twitch->initEventAPIs(this->bttvLiveUpdates.get(),
                                this->seventvEventAPI.get());

    this->platforms->registerAdapter(
        std::make_unique<platform::TwitchPlatformAdapter>());
    this->platforms->registerAdapter(
        std::make_unique<platform::KickPlatformAdapter>());
    this->platforms->initializeAll();

    this->streamerMode->start();

    this->initialized = true;
}

int Application::run()
{
    assert(this->initialized);

    this->twitch->connect();
    this->platforms->connectAll();

    if (!this->args_.isFramelessEmbed)
    {
        this->windows->getMainWindow().show();
        showOpenEmoteOnboardingIfNeeded(this);
    }

    getSettings()->enableBTTVChannelEmotes.connect(
        [this] {
            this->twitch->reloadAllBTTVChannelEmotes();
        },
        false);
    getSettings()->enableFFZChannelEmotes.connect(
        [this] {
            this->twitch->reloadAllFFZChannelEmotes();
        },
        false);
    getSettings()->enableSevenTVChannelEmotes.connect(
        [this] {
            this->twitch->reloadAllSevenTVChannelEmotes();
        },
        false);

    return QApplication::exec();
}

platform::PlatformRegistry *Application::getPlatforms()
{
    assertInGuiThread();
    assert(this->platforms);

    return this->platforms.get();
}

Theme *Application::getThemes()
{
    assertInGuiThread();
    assert(this->themes);

    return this->themes.get();
}

Fonts *Application::getFonts()
{
    assertInGuiThread();
    assert(this->fonts);

    return this->fonts.get();
}

EmoteController *Application::getEmotes()
{
    assertInGuiThread();
    assert(this->emotes);

    return this->emotes.get();
}

AccountController *Application::getAccounts()
{
    assertInGuiThread();
    assert(this->accounts);

    return this->accounts.get();
}

HotkeyController *Application::getHotkeys()
{
    assertInGuiThread();
    assert(this->hotkeys);

    return this->hotkeys.get();
}

WindowManager *Application::getWindows()
{
    assertInGuiThread();
    assert(this->windows);

    return this->windows.get();
}

Toasts *Application::getToasts()
{
    assertInGuiThread();
    assert(this->toasts);

    return this->toasts.get();
}

CrashHandler *Application::getCrashHandler()
{
    assertInGuiThread();
    assert(this->crashHandler);

    return this->crashHandler.get();
}

CommandController *Application::getCommands()
{
    assertInGuiThread();
    assert(this->commands);

    return this->commands.get();
}

NotificationController *Application::getNotifications()
{
    assertInGuiThread();
    assert(this->notifications);

    return this->notifications.get();
}

HighlightController *Application::getHighlights()
{
    assertInGuiThread();
    assert(this->highlights);

    return this->highlights.get();
}

FfzBadges *Application::getFfzBadges()
{
    assertInGuiThread();
    assert(this->ffzBadges);

    return this->ffzBadges.get();
}

BttvBadges *Application::getBttvBadges()
{
    // BttvBadges handles its own locks, so we don't need to assert that this is called in the GUI thread
    assert(this->bttvBadges);

    return this->bttvBadges.get();
}

SeventvBadges *Application::getSeventvBadges()
{
    // SeventvBadges handles its own locks, so we don't need to assert that this is called in the GUI thread
    assert(this->seventvBadges);

    return this->seventvBadges.get();
}

IUserDataController *Application::getUserData()
{
    assertInGuiThread();

    return this->userData.get();
}

ISoundController *Application::getSound()
{
    assertInGuiThread();

    return this->sound.get();
}

ITwitchLiveController *Application::getTwitchLiveController()
{
    assertInGuiThread();
    assert(this->twitchLiveController);

    return this->twitchLiveController.get();
}

TwitchBadges *Application::getTwitchBadges()
{
    assertInGuiThread();
    assert(this->twitchBadges);

    return this->twitchBadges.get();
}

IChatterinoBadges *Application::getChatterinoBadges()
{
    assertInGuiThread();
    assert(this->chatterinoBadges);

    return this->chatterinoBadges.get();
}

ImageUploader *Application::getImageUploader()
{
    assertInGuiThread();
    assert(this->imageUploader);

    return this->imageUploader.get();
}

SeventvAPI *Application::getSeventvAPI()
{
    assertInGuiThread();
    assert(this->seventvAPI);

    return this->seventvAPI.get();
}

#ifdef CHATTERINO_HAVE_PLUGINS
PluginController *Application::getPlugins()
{
    assertInGuiThread();
    assert(this->plugins);

    return this->plugins.get();
}
#endif

Updates &Application::getUpdates()
{
    assertInGuiThread();

    return this->updates;
}

ITwitchIrcServer *Application::getTwitch()
{
    return this->twitch.get();
}

PubSub *Application::getTwitchPubSub()
{
    assertInGuiThread();

    return this->twitchPubSub.get();
}

ILogging *Application::getChatLogger()
{
    assertInGuiThread();
    assert(this->logging);

    return this->logging.get();
}

ILinkResolver *Application::getLinkResolver()
{
    assertInGuiThread();

    return this->linkResolver.get();
}

IStreamerMode *Application::getStreamerMode()
{
    return this->streamerMode.get();
}

ITwitchUsers *Application::getTwitchUsers()
{
    assertInGuiThread();
    assert(this->twitchUsers);

    return this->twitchUsers.get();
}

BttvEmotes *Application::getBttvEmotes()
{
    assertInGuiThread();
    assert(this->bttvEmotes);

    return this->bttvEmotes.get();
}

BttvLiveUpdates *Application::getBttvLiveUpdates()
{
    assertInGuiThread();
    // bttvLiveUpdates may be nullptr if it's not enabled

    return this->bttvLiveUpdates.get();
}

FfzEmotes *Application::getFfzEmotes()
{
    assertInGuiThread();
    assert(this->ffzEmotes);

    return this->ffzEmotes.get();
}

SeventvEmotes *Application::getSeventvEmotes()
{
    assertInGuiThread();
    assert(this->seventvEmotes);

    return this->seventvEmotes.get();
}

SeventvEventAPI *Application::getSeventvEventAPI()
{
    assertInGuiThread();
    // seventvEventAPI may be nullptr if it's not enabled

    return this->seventvEventAPI.get();
}

pronouns::Pronouns *Application::getPronouns()
{
    // pronouns::Pronouns handles its own locks, so we don't need to assert that this is called in the GUI thread
    assert(this->pronouns);

    return this->pronouns.get();
}

eventsub::IController *Application::getEventSub()
{
    assert(this->eventSub);

    return this->eventSub.get();
}

SpellChecker *Application::getSpellChecker()
{
    assertInGuiThread();
    assert(this->spellChecker);

    return this->spellChecker.get();
}

void Application::aboutToQuit()
{
    ABOUT_TO_QUIT.store(true);

    this->platforms->aboutToQuitAll();
    this->eventSub->setQuitting();

    this->twitch->aboutToQuit();

    this->hotkeys->save();
    this->windows->save();

    this->windows->closeAll();
}

void Application::stop()
{
#ifdef CHATTERINO_HAVE_PLUGINS
    this->plugins.reset();
#endif
    this->platforms.reset();
    this->pronouns.reset();
    this->twitchUsers.reset();
    this->streamerMode.reset();
    this->linkResolver.reset();
    this->seventvEventAPI.reset();
    this->seventvEmotes.reset();
    this->ffzEmotes.reset();
    this->bttvLiveUpdates.reset();
    this->bttvEmotes.reset();
    this->chatterinoBadges.reset();
    this->twitchBadges.reset();
    this->twitchPubSub.reset();
    this->twitchLiveController.reset();
    this->sound.reset();
    this->userData.reset();
    this->seventvBadges.reset();
    this->ffzBadges.reset();
    this->twitch.reset();
    this->highlights.reset();
    this->notifications.reset();
    this->commands.reset();
    this->crashHandler.reset();
    this->seventvAPI.reset();
    this->imageUploader.reset();
    this->toasts.reset();
    this->windows.reset();
    this->hotkeys.reset();
    this->eventSub.reset();
    this->accounts.reset();
    this->emotes.reset();
    this->logging.reset();
    this->fonts.reset();
    this->themes.reset();
    this->spellChecker.reset();

    STOPPED.store(true);
}

void Application::initNm(const Paths &paths)
{
    (void)paths;

#if defined QT_NO_DEBUG || defined CHATTERINO_DEBUG_NM
    registerNmHost(paths);
    this->nmServer->start();
#endif
}

IApplication *getApp()
{
    assert(INSTANCE != nullptr);
    assert(STOPPED.load() == false);

    return INSTANCE;
}

IApplication *tryGetApp()
{
    return INSTANCE;
}

bool isAppAboutToQuit()
{
    return ABOUT_TO_QUIT.load();
}

}  // namespace chatterino
