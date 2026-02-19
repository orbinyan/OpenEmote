// SPDX-FileCopyrightText: 2017 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/MessageBuilder.hpp"

#include "Application.hpp"
#include "common/LinkParser.hpp"
#include "common/Literals.hpp"
#include "common/QLogging.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "controllers/highlights/HighlightController.hpp"
#include "controllers/highlights/HighlightResult.hpp"
#include "controllers/ignores/IgnoreController.hpp"
#include "controllers/ignores/IgnorePhrase.hpp"
#include "controllers/userdata/UserDataController.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/Message.hpp"
#include "messages/MessageColor.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageThread.hpp"
#include "providers/bttv/BttvBadges.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/chatterino/ChatterinoBadges.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "providers/emoji/Emojis.hpp"
#include "providers/ffz/FfzBadges.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/links/LinkResolver.hpp"
#include "providers/seventv/SeventvBadges.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/ChannelPointReward.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchBadge.hpp"
#include "providers/twitch/TwitchBadges.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrc.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "providers/twitch/TwitchUsers.hpp"
#include "providers/twitch/UserColor.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/Theme.hpp"
#include "singletons/WindowManager.hpp"
#include "util/FormatTime.hpp"
#include "util/Helpers.hpp"
#include "util/IrcHelpers.hpp"
#include "util/QStringHash.hpp"
#include "util/Variant.hpp"
#include "widgets/Window.hpp"

#include <boost/variant.hpp>
#include <QApplication>
#include <QColor>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QStringBuilder>
#include <QTimeZone>

#include <algorithm>
#include <chrono>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace chatterino::literals;

namespace {

using namespace chatterino;
using namespace std::chrono_literals;

const QColor AUTOMOD_USER_COLOR{"blue"};

const QString regexHelpString("(\\w+)[.,!?;:]*?$");

// matches a mention with punctuation at the end, like "@username," or "@username!!!" where capture group would return "username"
const QRegularExpression mentionRegex("^@" + regexHelpString);

// if findAllUsernames setting is enabled, matches strings like in the examples above, but without @ symbol at the beginning
const QRegularExpression allUsernamesMentionRegex("^" + regexHelpString);

const QRegularExpression SPACE_REGEX("\\s");

struct HypeChatPaidLevel {
    std::chrono::seconds duration;
    uint8_t numeric;
};

const std::unordered_map<QString, HypeChatPaidLevel> HYPE_CHAT_PAID_LEVEL{
    {u"ONE"_s, {30s, 1}},    {u"TWO"_s, {2min + 30s, 2}},
    {u"THREE"_s, {5min, 3}}, {u"FOUR"_s, {10min, 4}},
    {u"FIVE"_s, {30min, 5}}, {u"SIX"_s, {1h, 6}},
    {u"SEVEN"_s, {2h, 7}},   {u"EIGHT"_s, {3h, 8}},
    {u"NINE"_s, {4h, 9}},    {u"TEN"_s, {5h, 10}},
};

QString formatUpdatedEmoteList(const QString &platform,
                               const std::vector<QString> &emoteNames,
                               bool isAdd, bool isFirstWord)
{
    QString text = "";
    if (isAdd)
    {
        text += isFirstWord ? "Added" : "added";
    }
    else
    {
        text += isFirstWord ? "Removed" : "removed";
    }

    if (emoteNames.size() == 1)
    {
        text += QString(" %1 emote ").arg(platform);
    }
    else
    {
        text += QString(" %1 %2 emotes ").arg(emoteNames.size()).arg(platform);
    }

    size_t i = 0;
    for (const auto &emoteName : emoteNames)
    {
        i++;
        if (i > 1)
        {
            text += i == emoteNames.size() ? " and " : ", ";
        }
        text += emoteName;
    }

    text += ".";

    return text;
}

/**
 * Gets the default sound url if the user set one,
 * or the chatterino default ping sound if no url is set.
 */
QUrl getFallbackHighlightSound()
{
    QString path = getSettings()->pathHighlightSound;
    bool fileExists =
        !path.isEmpty() && QFileInfo::exists(path) && QFileInfo(path).isFile();

    if (fileExists)
    {
        return QUrl::fromLocalFile(path);
    }

    return QUrl("qrc:/sounds/ping2.wav");
}

void actuallyTriggerHighlights(const QString &channelName, bool playSound,
                               const QUrl &customSoundUrl, bool windowAlert)
{
    if (getApp()->getStreamerMode()->isEnabled() &&
        getSettings()->streamerModeMuteMentions)
    {
        // We are in streamer mode with muting mention sounds enabled. Do nothing.
        return;
    }

    if (getSettings()->isMutedChannel(channelName))
    {
        // Do nothing. Pings are muted in this channel.
        return;
    }

    const bool hasFocus = (QApplication::focusWidget() != nullptr);
    const bool resolveFocus =
        !hasFocus || getSettings()->highlightAlwaysPlaySound;

    if (playSound && resolveFocus)
    {
        QUrl soundUrl = customSoundUrl;
        if (soundUrl.isEmpty())
        {
            soundUrl = getFallbackHighlightSound();
        }
        getApp()->getSound()->play(soundUrl);
    }

    if (windowAlert)
    {
        getApp()->getWindows()->sendAlert();
    }
}

QString stylizeUsername(const QString &username, const Message &message)
{
    const QString &localizedName = message.localizedName;
    bool hasLocalizedName = !localizedName.isEmpty();

    // The full string that will be rendered in the chat widget
    QString usernameText;

    switch (getSettings()->usernameDisplayMode.getValue())
    {
        case UsernameDisplayMode::Username: {
            usernameText = username;
        }
        break;

        case UsernameDisplayMode::LocalizedName: {
            if (hasLocalizedName)
            {
                usernameText = localizedName;
            }
            else
            {
                usernameText = username;
            }
        }
        break;

        default:
        case UsernameDisplayMode::UsernameAndLocalizedName: {
            if (hasLocalizedName)
            {
                usernameText = username + "(" + localizedName + ")";
            }
            else
            {
                usernameText = username;
            }
        }
        break;
    }

    QStringList nicknameCandidates;
    const auto addCandidate = [&nicknameCandidates](const QString &candidate) {
        const auto trimmed = candidate.trimmed();
        if (trimmed.isEmpty())
        {
            return;
        }
        for (const auto &existing : nicknameCandidates)
        {
            if (existing.compare(trimmed, Qt::CaseInsensitive) == 0)
            {
                return;
            }
        }
        nicknameCandidates.append(trimmed);
    };

    if (!message.userID.trimmed().isEmpty())
    {
        addCandidate(QString("id:%1").arg(message.userID.trimmed()));
    }
    addCandidate(message.loginName);
    addCandidate(message.displayName);
    addCandidate(message.localizedName);
    addCandidate(usernameText);

    for (const auto &candidate : nicknameCandidates)
    {
        if (auto nicknameText = getSettings()->matchNickname(candidate))
        {
            return *nicknameText;
        }
    }

    const auto preferredNickname = message.openEmotePreferredNickname.trimmed();
    if (!preferredNickname.isEmpty())
    {
        return preferredNickname;
    }

    return usernameText;
}

std::optional<EmotePtr> getTwitchBadge(const TwitchBadge &badge,
                                       const TwitchChannel *twitchChannel)
{
    if (auto channelBadge =
            twitchChannel->twitchBadge(badge.key_, badge.value_))
    {
        return channelBadge;
    }

    if (auto globalBadge =
            getApp()->getTwitchBadges()->badge(badge.key_, badge.value_))
    {
        return globalBadge;
    }

    return std::nullopt;
}

void appendBadges(MessageBuilder *builder,
                  const std::vector<TwitchBadge> &badges,
                  const std::unordered_map<QString, QString> &badgeInfos,
                  const TwitchChannel *twitchChannel)
{
    if (twitchChannel == nullptr)
    {
        return;
    }

    for (const auto &badge : badges)
    {
        auto badgeEmote = getTwitchBadge(badge, twitchChannel);
        if (!badgeEmote)
        {
            continue;
        }
        auto tooltip = (*badgeEmote)->tooltip.string;

        if (badge.key_ == "bits")
        {
            const auto &cheerAmount = badge.value_;
            tooltip = QString("Twitch cheer %0").arg(cheerAmount);
        }
        else if (badge.key_ == "moderator" &&
                 getSettings()->useCustomFfzModeratorBadges)
        {
            if (auto customModBadge = twitchChannel->ffzCustomModBadge())
            {
                builder
                    ->emplace<ModBadgeElement>(
                        *customModBadge,
                        MessageElementFlag::BadgeChannelAuthority)
                    ->setTooltip((*customModBadge)->tooltip.string);
                // early out, since we have to add a custom badge element here
                continue;
            }
        }
        else if (badge.key_ == "vip" && getSettings()->useCustomFfzVipBadges)
        {
            if (auto customVipBadge = twitchChannel->ffzCustomVipBadge())
            {
                builder
                    ->emplace<VipBadgeElement>(
                        *customVipBadge,
                        MessageElementFlag::BadgeChannelAuthority)
                    ->setTooltip((*customVipBadge)->tooltip.string);
                // early out, since we have to add a custom badge element here
                continue;
            }
        }
        else if (badge.flag_ == MessageElementFlag::BadgeSubscription)
        {
            auto badgeInfoIt = badgeInfos.find(badge.key_);
            if (badgeInfoIt != badgeInfos.end())
            {
                // badge.value_ is 4 chars long if user is subbed on higher tier
                // (tier + amount of months with leading zero if less than 100)
                // e.g. 3054 - tier 3 4,5-year sub. 2108 - tier 2 9-year sub
                const auto &subTier =
                    badge.value_.length() > 3 ? badge.value_.at(0) : '1';
                const auto &subMonths = badgeInfoIt->second;
                tooltip +=
                    QString(" (%1%2 months)")
                        .arg(subTier != '1' ? QString("Tier %1, ").arg(subTier)
                                            : "")
                        .arg(subMonths);
            }
        }
        else if (badge.flag_ == MessageElementFlag::BadgePredictions)
        {
            auto badgeInfoIt = badgeInfos.find(badge.key_);
            if (badgeInfoIt != badgeInfos.end())
            {
                auto infoValue = badgeInfoIt->second;
                auto predictionText =
                    infoValue
                        .replace(R"(\s)", " ")  // standard IRC escapes
                        .replace(R"(\:)", ";")
                        .replace(R"(\\)", R"(\)")
                        .replace("⸝", ",");  // twitch's comma escape
                // Careful, the first character is RIGHT LOW PARAPHRASE BRACKET or U+2E1D, which just looks like a comma

                tooltip = QString("Predicted %1").arg(predictionText);
            }
        }

        builder->emplace<BadgeElement>(*badgeEmote, badge.flag_)
            ->setTooltip(tooltip);
    }

    builder->message().twitchBadges = badges;
    builder->message().twitchBadgeInfos = badgeInfos;
}

std::vector<TwitchBadge> appendSharedChatBadges(
    MessageBuilder *builder, const std::vector<TwitchBadge> &sharedBadges,
    const QString &sharedChannelName, const TwitchChannel *twitchChannel)
{
    auto appendedBadges = std::vector<TwitchBadge>{};
    for (const auto &badge : sharedBadges)
    {
        if (badge.key_ != "moderator" && badge.key_ != "vip")
        {
            continue;
        }

        auto badgeEmote = getTwitchBadge(badge, twitchChannel);
        if (!badgeEmote)
        {
            continue;
        }

        auto tooltip = (*badgeEmote)->tooltip.string;
        if (sharedChannelName != "")
        {
            tooltip = QString("%1 (%2)").arg(tooltip, sharedChannelName);
        }

        builder->emplace<BadgeElement>(*badgeEmote, badge.flag_)
            ->setTooltip(tooltip);
        appendedBadges.push_back(badge);
    }

    return appendedBadges;
}

bool doesWordContainATwitchEmote(
    int cursor, const QString &word,
    const std::vector<TwitchEmoteOccurrence> &twitchEmotes,
    std::vector<TwitchEmoteOccurrence>::const_iterator &currentTwitchEmoteIt)
{
    if (currentTwitchEmoteIt == twitchEmotes.end())
    {
        // No emote to add!
        return false;
    }

    const auto &currentTwitchEmote = *currentTwitchEmoteIt;

    auto wordEnd = cursor + word.length();

    // Check if this emote fits within the word boundaries
    if (currentTwitchEmote.start < cursor || currentTwitchEmote.end > wordEnd)
    {
        // this emote does not fit xd
        return false;
    }

    return true;
}

EmotePtr makeSharedChatBadge(const QString &sourceName,
                             const QString &sourceProfileURL,
                             const QString &sourceLogin)
{
    if (!sourceProfileURL.isEmpty())
    {
        auto [urlBegin, urlEnd] = splitOnce(sourceProfileURL, u"300x300");
        QString url28px = urlBegin % u"28x28" % urlEnd;
        QString url70px = urlBegin % u"70x70" % urlEnd;
        QString url150px = urlBegin % u"150x150" % urlEnd;

        auto badgeLink = [&] {
            if (sourceLogin.isEmpty())
            {
                return Url{"https://link.twitch.tv/SharedChatViewer"};
            }

            return Url{u"https://www.twitch.tv/%1"_s.arg(sourceLogin)};
        }();

        return std::make_shared<Emote>(Emote{
            .name = EmoteName{},
            .images =
                ImageSet{
                    // The images should be displayed like an 18x18 image
                    Image::fromUrl({url28px}, 18.F / 28.F),
                    Image::fromUrl({url70px}, 18.F / 70.F),
                    Image::fromUrl({url150px}, 18.F / 150.F),
                },
            .tooltip =
                Tooltip{"Shared Message" +
                        (sourceName.isEmpty() ? "" : " from " + sourceName)},
            .homePage = badgeLink,
        });
    }

    return std::make_shared<Emote>(Emote{
        .name = EmoteName{},
        .images = ImageSet{Image::fromResourcePixmap(
            getResources().twitch.sharedChat, 0.25)},
        .tooltip = Tooltip{"Shared Message" +
                           (sourceName.isEmpty() ? "" : " from " + sourceName)},
        .homePage = Url{"https://link.twitch.tv/SharedChatViewer"},
    });
}

float openEmoteChannelScaleForName(QStringView channelName)
{
    if (getSettings()->openEmoteBotCompatibilityMode.getValue())
    {
        return 1.F;
    }

    static QString cachedRaw;
    static QHash<QString, float> cachedScales;

    const auto raw =
        getSettings()->openEmoteChannelEmoteScaleOverrides.getValue().trimmed();
    if (raw != cachedRaw)
    {
        cachedRaw = raw;
        cachedScales.clear();

        for (const auto &entry : raw.split(',', Qt::SkipEmptyParts))
        {
            const auto parts = entry.split('=', Qt::SkipEmptyParts);
            if (parts.size() != 2)
            {
                continue;
            }

            bool ok = false;
            const auto parsedScale = parts[1].trimmed().toFloat(&ok);
            if (!ok)
            {
                continue;
            }

            auto key = parts[0].trimmed().toLower();
            if (key.startsWith('#'))
            {
                key.remove(0, 1);
            }
            if (key.isEmpty())
            {
                continue;
            }
            cachedScales.insert(key, parsedScale);
        }
    }

    auto key = channelName.toString().trimmed().toLower();
    if (key.startsWith('#'))
    {
        key.remove(0, 1);
    }
    if (key.isEmpty())
    {
        return 1.F;
    }

    return std::clamp(cachedScales.value(key, 1.F), 0.25F, 6.F);
}

float openEmoteChannelScaleForChannel(const TwitchChannel *twitchChannel)
{
    if (twitchChannel == nullptr)
    {
        return 1.F;
    }

    return openEmoteChannelScaleForName(twitchChannel->getName());
}

void appendOpenEmoteAvatarDecorators(MessageBuilder *builder,
                                     const QVariantMap &tags);
std::vector<std::pair<QString, QColor>> collectOpenEmoteAvatarCornerBadges(
    const QVariantMap &tags);
struct OpenEmoteIdentityMetrics {
    int statusBadgeCount = 0;
    int textBadgeCount = 0;
};

std::optional<EmotePtr> makeOpenEmoteAuthorAvatar(const Message &message,
                                                  float targetPixels = 18.F)
{
    if (message.userID.isEmpty())
    {
        return std::nullopt;
    }

    auto twitchUser = getApp()->getTwitchUsers()->resolveID({message.userID});
    if (!twitchUser || twitchUser->profilePictureUrl.isEmpty())
    {
        return std::nullopt;
    }

    auto avatar1x = twitchUser->profilePictureUrl;
    auto avatar2x = twitchUser->profilePictureUrl;
    auto avatar4x = twitchUser->profilePictureUrl;

    if (twitchUser->profilePictureUrl.contains("300x300"))
    {
        auto [urlBegin, urlEnd] = splitOnce(twitchUser->profilePictureUrl,
                                            u"300x300");
        avatar1x = urlBegin % u"28x28" % urlEnd;
        avatar2x = urlBegin % u"70x70" % urlEnd;
        avatar4x = urlBegin % u"150x150" % urlEnd;
    }

    auto displayName =
        message.displayName.isEmpty() ? message.loginName : message.displayName;
    auto tooltipName = message.localizedName.isEmpty()
                           ? displayName
                           : QString("%1 (%2)")
                                 .arg(message.localizedName, displayName);
    auto profileUrl = message.loginName.isEmpty()
                          ? Url{"https://www.twitch.tv"}
                          : Url{u"https://www.twitch.tv/%1"_s.arg(
                                message.loginName)};

    return std::make_shared<Emote>(Emote{
        .name = EmoteName{},
        .images = ImageSet{
            Image::fromUrl({avatar1x}, targetPixels / 28.F),
            Image::fromUrl({avatar2x}, targetPixels / 70.F),
            Image::fromUrl({avatar4x}, targetPixels / 150.F),
        },
        .tooltip = Tooltip{QString("Author avatar: %1").arg(tooltipName)},
        .homePage = profileUrl,
    });
}

bool appendOpenEmoteAuthorAvatarElement(MessageBuilder *builder,
                                        const QVariantMap &tags,
                                        MessageElementFlags flags,
                                        float targetPixels,
                                        bool appendDecorators)
{
    // Product policy (current phase): avatars are user-card only.
    // Do not render avatar identity inline in chat rows yet.
    (void)builder;
    (void)tags;
    (void)flags;
    (void)targetPixels;
    (void)appendDecorators;
    return false;

    const bool useCornerBadges =
        !getSettings()->openEmoteBotCompatibilityMode.getValue() &&
        getSettings()->openEmoteAvatarCornerBadges;
    auto cornerBadges =
        useCornerBadges ? collectOpenEmoteAvatarCornerBadges(tags)
                        : std::vector<std::pair<QString, QColor>>{};

    auto profileName = builder->message().displayName.isEmpty()
                           ? builder->message().loginName
                           : builder->message().displayName;
    auto tooltipName =
        builder->message().localizedName.isEmpty()
            ? profileName
            : QString("%1 (%2)")
                  .arg(builder->message().localizedName, profileName);

    if (auto avatar = makeOpenEmoteAuthorAvatar(builder->message(), targetPixels))
    {
        if (auto avatarImage = avatar.value()->images.getImage(1.F))
        {
            builder
                ->emplace<CircularImageElement>(avatarImage, 1, Qt::transparent,
                                                flags,
                                                std::move(cornerBadges))
                ->setLink({Link::UserInfo, profileName})
                ->setTooltip(QString("Author: %1").arg(tooltipName));
        }

        if (appendDecorators && getSettings()->openEmoteAvatarDecorators &&
            !useCornerBadges)
        {
            appendOpenEmoteAvatarDecorators(builder, tags);
        }

        return true;
    }

    const auto labelText =
        profileName.isEmpty() ? QString("[?]")
                              : QString("[%1]").arg(profileName.left(1).toUpper());

    builder
        ->emplace<TextElement>(labelText, flags, MessageColor::System,
                               FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, profileName})
        ->setTooltip(QString("Author: %1").arg(tooltipName));

    if (appendDecorators && getSettings()->openEmoteAvatarDecorators)
    {
        appendOpenEmoteAvatarDecorators(builder, tags);
    }

    return true;
}

QStringList openEmoteConfiguredBadgePackIds()
{
    QStringList packIds;
    for (const auto &token :
         getSettings()
             ->openEmoteCustomBadgePackAllowlist.getValue()
             .split(',', Qt::SkipEmptyParts))
    {
        auto value = token.trimmed().toLower();
        if (value.isEmpty() ||
            packIds.contains(value, Qt::CaseInsensitive))
        {
            continue;
        }
        packIds.append(value);
    }
    return packIds;
}

std::pair<QString, QString> parseOpenEmoteBadgeToken(const QString &token)
{
    const auto value = token.trimmed();
    if (value.isEmpty())
    {
        return {};
    }

    for (const auto separator : {':', '/'})
    {
        const auto index = value.indexOf(separator);
        if (index > 0 && index + 1 < value.size())
        {
            return {value.left(index).trimmed().toLower(),
                    value.mid(index + 1).trimmed()};
        }
    }

    return {QString{}, value};
}

std::pair<QString, QString> parseOpenEmoteAvatarActionCommand(
    QStringView messageText)
{
    static const QRegularExpression actionRegex(
        QStringLiteral(R"(^\s*!(shake|hug|wave)\s+@?([A-Za-z0-9_]{2,32})\b)"),
        QRegularExpression::CaseInsensitiveOption);

    const auto text = messageText.toString();
    const auto match = actionRegex.match(text);
    if (!match.hasMatch())
    {
        return {};
    }

    return {match.captured(1).toLower(), match.captured(2)};
}

void parseOpenEmoteAvatarModelMetadata(MessageBuilder *builder,
                                       const QVariantMap &tags,
                                       QStringView content)
{
    if (getSettings()->openEmoteBotCompatibilityMode.getValue())
    {
        return;
    }

    const auto parseBoundedTag = [&tags](const QString &name,
                                         int maxLength) -> QString {
        auto value = parseTagString(tags.value(name).toString()).trimmed();
        if (value.isEmpty())
        {
            return {};
        }
        return value.left(maxLength);
    };

    builder->message().openEmoteAvatarModelId =
        parseBoundedTag("openemote-avatar-model", 96);
    builder->message().openEmoteAvatarSkinId =
        parseBoundedTag("openemote-avatar-skin", 96);
    builder->message().openEmoteAvatarIdleAsset =
        parseBoundedTag("openemote-avatar-idle", 512);
    builder->message().openEmotePreferredNickname =
        parseBoundedTag("openemote-preferred-nickname", 64);
    if (builder->message().openEmotePreferredNickname.isEmpty())
    {
        builder->message().openEmotePreferredNickname =
            parseBoundedTag("openemote-preferred-name", 64);
    }

    auto action = parseBoundedTag("openemote-avatar-action", 24).toLower();
    auto target = parseBoundedTag("openemote-avatar-target", 32);

    if (action.isEmpty())
    {
        const auto parsed = parseOpenEmoteAvatarActionCommand(content);
        action = parsed.first;
        target = parsed.second;
    }

    if (action == "shake" || action == "hug" || action == "wave")
    {
        builder->message().openEmoteAvatarAction = action;
        builder->message().openEmoteAvatarActionTarget = target;
    }
}

bool shouldRenderOpenEmoteTimestamp(Channel *channel,
                                    const Message &currentMessage,
                                    const QDateTime &currentTimestamp)
{
    if (!getSettings()->showTimestamps)
    {
        return false;
    }

    if (getSettings()->openEmoteTimestampAlwaysSystem &&
        currentMessage.flags.hasAny({MessageFlag::System,
                                     MessageFlag::ModerationAction,
                                     MessageFlag::Subscription,
                                     MessageFlag::Timeout}))
    {
        return true;
    }

    const auto alwaysUsersCsv =
        getSettings()->openEmoteTimestampAlwaysUsers.getValue();
    if (!alwaysUsersCsv.trimmed().isEmpty() &&
        !currentMessage.loginName.trimmed().isEmpty())
    {
        for (const auto &token :
             alwaysUsersCsv.split(',', Qt::SkipEmptyParts))
        {
            const auto user = token.trimmed();
            if (user.isEmpty())
            {
                continue;
            }
            if (currentMessage.loginName.compare(user, Qt::CaseInsensitive) ==
                0)
            {
                return true;
            }
        }
    }

    if (!getSettings()->openEmoteTimestampGapsOnly)
    {
        return true;
    }

    const int thresholdMinutes =
        std::clamp(getSettings()->openEmoteTimestampGapMinutes.getValue(), 1,
                   400);
    const auto thresholdSeconds = thresholdMinutes * 60;

    if (channel == nullptr)
    {
        return true;
    }

    const auto snapshot = channel->getMessageSnapshot();
    for (auto it = snapshot.rbegin(); it != snapshot.rend(); ++it)
    {
        const auto &previous = *it;
        if (previous == nullptr || !previous->serverReceivedTime.isValid())
        {
            continue;
        }
        return previous->serverReceivedTime.secsTo(currentTimestamp) >=
               thresholdSeconds;
    }

    // First message in a view should show a timestamp.
    return true;
}

QString openEmoteAvatarCornerLabel(const QString &badgeKey)
{
    if (badgeKey == "broadcaster")
    {
        return "B";
    }
    if (badgeKey == "moderator")
    {
        return "M";
    }
    if (badgeKey == "vip")
    {
        return "V";
    }
    if (badgeKey == "staff")
    {
        return "S";
    }
    if (badgeKey == "admin")
    {
        return "A";
    }
    if (badgeKey == "global_mod")
    {
        return "G";
    }
    if (badgeKey == "partner")
    {
        return "P";
    }
    if (badgeKey == "subscriber")
    {
        return "S";
    }
    if (badgeKey == "premium")
    {
        return "$";
    }
    if (badgeKey == "founder")
    {
        return "F";
    }
    if (badgeKey == "verified")
    {
        return "R";
    }
    if (badgeKey == "dev")
    {
        return "D";
    }
    const auto normalized = badgeKey.trimmed();
    return normalized.isEmpty() ? "?" : normalized.left(1).toUpper();
}

QColor openEmoteAvatarCornerColor(const QString &badgeKey)
{
    if (badgeKey == "broadcaster")
    {
        return QColor("#e91916");
    }
    if (badgeKey == "moderator")
    {
        return QColor("#00ad03");
    }
    if (badgeKey == "vip")
    {
        return QColor("#d269ff");
    }
    if (badgeKey == "staff")
    {
        return QColor("#7f4bff");
    }
    if (badgeKey == "admin")
    {
        return QColor("#ff7a18");
    }
    if (badgeKey == "global_mod")
    {
        return QColor("#2ec9c2");
    }
    if (badgeKey == "partner")
    {
        return QColor("#2b7fff");
    }
    if (badgeKey == "subscriber")
    {
        return QColor("#8f6cff");
    }
    if (badgeKey == "premium")
    {
        return QColor("#00b894");
    }
    if (badgeKey == "founder")
    {
        return QColor("#f3b33d");
    }
    if (badgeKey == "verified")
    {
        return QColor("#1f9bff");
    }
    if (badgeKey == "dev")
    {
        return QColor("#f3b33d");
    }
    return QColor("#7f7f7f");
}

std::vector<std::pair<QString, QColor>> collectOpenEmoteAvatarCornerBadges(
    const QVariantMap &tags)
{
    std::vector<std::pair<QString, QColor>> cornerBadges;
    if (getSettings()->openEmoteBotCompatibilityMode.getValue())
    {
        return cornerBadges;
    }

    if (!getSettings()->openEmoteAvatarCornerBadges)
    {
        return cornerBadges;
    }

    const auto maxBadges = std::clamp(
        getSettings()->openEmoteAvatarCornerBadgeMax.getValue(), 1, 4);

    QSet<QString> activeBadgeKeys;
    auto addBadgeKey = [&activeBadgeKeys](const QString &rawKey) {
        const auto key = rawKey.trimmed().toLower();
        if (key.isEmpty())
        {
            return;
        }
        activeBadgeKeys.insert(key);
    };

    for (const auto &badge : parseBadgeTag(tags))
    {
        addBadgeKey(badge.key_);
    }

    const auto explicitVerified =
        tags.value("openemote-verified").toString().trimmed();
    if (explicitVerified == "1" ||
        explicitVerified.compare("true", Qt::CaseInsensitive) == 0)
    {
        addBadgeKey("verified");
    }

    QString channelBadgeOverride;
    QString subBadgeOverride;
    QStringList customBadges;

    const auto addCustomBadge = [&customBadges](const QString &rawBadge) {
        const auto badge = rawBadge.trimmed().toLower();
        if (badge.isEmpty() || customBadges.contains(badge, Qt::CaseInsensitive))
        {
            return;
        }
        customBadges.push_back(badge);
    };

    static const QStringList CHANNEL_STATUS_ORDER = {
        "broadcaster", "staff", "admin", "global_mod", "moderator", "vip",
        "partner",
    };
    static const QStringList SUB_STATUS_ORDER = {"subscriber", "premium",
                                                 "founder"};

    auto isChannelStatus = [](const QString &key) {
        return CHANNEL_STATUS_ORDER.contains(key, Qt::CaseInsensitive);
    };
    auto isSubStatus = [](const QString &key) {
        return SUB_STATUS_ORDER.contains(key, Qt::CaseInsensitive);
    };

    for (const auto &token :
         parseTagString(tags.value("openemote-badges").toString())
             .split(',', Qt::SkipEmptyParts))
    {
        const auto [packId, badgeName] = parseOpenEmoteBadgeToken(token);
        Q_UNUSED(packId);
        const auto key = badgeName.trimmed().toLower();
        if (key.isEmpty())
        {
            continue;
        }

        if (key == "verified")
        {
            addBadgeKey(key);
            continue;
        }
        if (key == "dev")
        {
            addBadgeKey(key);
            continue;
        }

        if (isChannelStatus(key))
        {
            channelBadgeOverride = key;
            continue;
        }
        if (isSubStatus(key))
        {
            subBadgeOverride = key;
            continue;
        }

        addCustomBadge(key);
    }

    QString channelBadgeKey = channelBadgeOverride;
    if (channelBadgeKey.isEmpty())
    {
        for (const auto &key : CHANNEL_STATUS_ORDER)
        {
            if (activeBadgeKeys.contains(key))
            {
                channelBadgeKey = key;
                break;
            }
        }
    }

    QString subBadgeKey = subBadgeOverride;
    if (subBadgeKey.isEmpty())
    {
        for (const auto &key : SUB_STATUS_ORDER)
        {
            if (activeBadgeKeys.contains(key))
            {
                subBadgeKey = key;
                break;
            }
        }
    }

    const auto appendBadge = [&cornerBadges, maxBadges](const QString &key) {
        const auto normalized = key.trimmed().toLower();
        if (normalized.isEmpty())
        {
            return;
        }
        if (int(cornerBadges.size()) >= maxBadges)
        {
            return;
        }
        cornerBadges.emplace_back(openEmoteAvatarCornerLabel(normalized),
                                  openEmoteAvatarCornerColor(normalized));
    };

    // Order is intentionally fixed (not user reorderable):
    // 1) channel status, 2) sub status, 3-4) custom badges
    appendBadge(channelBadgeKey);
    appendBadge(subBadgeKey);
    for (const auto &custom : customBadges)
    {
        appendBadge(custom);
    }

    // Fallback slots (when available) keep stable ordering.
    if (int(cornerBadges.size()) < maxBadges)
    {
        if (activeBadgeKeys.contains("verified"))
        {
            appendBadge("verified");
        }
        if (activeBadgeKeys.contains("dev") ||
            activeBadgeKeys.contains("founder"))
        {
            appendBadge("dev");
        }
    }

    return cornerBadges;
}

bool openEmoteBadgePackAllowed(const QString &packId,
                               bool allowUntrustedBadgePacks,
                               const QStringList &allowlist)
{
    if (packId.isEmpty() || allowUntrustedBadgePacks)
    {
        return true;
    }

    return allowlist.contains(packId, Qt::CaseInsensitive);
}

OpenEmoteIdentityMetrics appendOpenEmoteCompactRoleBadges(
    MessageBuilder *builder, const QVariantMap &tags,
    TwitchChannel *twitchChannel)
{
    OpenEmoteIdentityMetrics metrics;
    if (getSettings()->openEmoteBotCompatibilityMode.getValue())
    {
        return metrics;
    }

    QStringList renderedStatusBadges;
    bool hasVerified = false;
    bool hasDev = false;
    std::vector<std::pair<QString, QString>> customBadges;

    auto appendTwitchStatusBadge = [&](const TwitchBadge &badge,
                                       const QString &tooltip) -> bool {
        const auto key = badge.key_.trimmed().toLower();
        if (key.isEmpty() ||
            renderedStatusBadges.contains(key, Qt::CaseInsensitive))
        {
            return false;
        }

        if (auto badgeEmote = getTwitchBadge(badge, twitchChannel))
        {
            builder->emplace<BadgeElement>(*badgeEmote, badge.flag_)
                ->setTooltip(tooltip);
            renderedStatusBadges.append(key);
            metrics.statusBadgeCount++;
            return true;
        }
        return false;
    };

    bool statusBadgeRendered = false;
    bool membershipBadgeRendered = false;
    const auto twitchBadges = parseBadgeTag(tags);
    for (const auto &badge : twitchBadges)
    {
        const auto key = badge.key_.trimmed().toLower();
        if (!statusBadgeRendered && key == "broadcaster")
        {
            statusBadgeRendered = appendTwitchStatusBadge(badge, "Broadcaster");
            continue;
        }

        if (!statusBadgeRendered && key == "moderator")
        {
            statusBadgeRendered = appendTwitchStatusBadge(badge, "Moderator");
            continue;
        }

        if (!statusBadgeRendered && key == "vip")
        {
            statusBadgeRendered = appendTwitchStatusBadge(badge, "VIP");
            continue;
        }

        if (!statusBadgeRendered && key == "staff")
        {
            statusBadgeRendered =
                appendTwitchStatusBadge(badge, "Twitch Staff");
            continue;
        }

        if (!statusBadgeRendered && key == "admin")
        {
            statusBadgeRendered =
                appendTwitchStatusBadge(badge, "Twitch Admin");
            continue;
        }

        if (!statusBadgeRendered && key == "global_mod")
        {
            statusBadgeRendered =
                appendTwitchStatusBadge(badge, "Global Moderator");
            continue;
        }

        if (!statusBadgeRendered && key == "partner")
        {
            statusBadgeRendered = appendTwitchStatusBadge(badge, "Partner");
            continue;
        }

        if (!membershipBadgeRendered && key == "founder")
        {
            membershipBadgeRendered = appendTwitchStatusBadge(badge, "Founder");
            continue;
        }

        if (!membershipBadgeRendered && key == "subscriber")
        {
            membershipBadgeRendered =
                appendTwitchStatusBadge(badge, "Subscriber");
            continue;
        }

        if (!hasVerified && key == "verified")
        {
            hasVerified = true;
            continue;
        }

        if (!hasDev && key == "dev")
        {
            hasDev = true;
        }

        if (statusBadgeRendered && membershipBadgeRendered)
        {
            break;
        }
    }

    const bool enableCustomBadgePacks =
        getSettings()->openEmoteEnableCustomBadgePacks;
    const bool allowUntrustedBadgePacks =
        getSettings()->openEmoteAllowUntrustedBadgePacks;
    const auto configuredPackIds = openEmoteConfiguredBadgePackIds();
    const auto explicitVerified =
        tags.value("openemote-verified").toString().trimmed();
    if (explicitVerified == "1" ||
        explicitVerified.compare("true", Qt::CaseInsensitive) == 0)
    {
        hasVerified = true;
    }

    for (const auto &token :
         parseTagString(tags.value("openemote-badges").toString())
             .split(',', Qt::SkipEmptyParts))
    {
        const auto [packId, badgeName] = parseOpenEmoteBadgeToken(token);
        if (badgeName.compare("verified", Qt::CaseInsensitive) == 0)
        {
            hasVerified = true;
            continue;
        }

        if (badgeName.compare("dev", Qt::CaseInsensitive) == 0)
        {
            hasDev = true;
            continue;
        }

        if (!enableCustomBadgePacks || badgeName.isEmpty() ||
            !openEmoteBadgePackAllowed(packId, allowUntrustedBadgePacks,
                                       configuredPackIds))
        {
            continue;
        }

        const auto normalizedBadge = badgeName.simplified().left(12).toUpper();
        if (normalizedBadge.isEmpty())
        {
            continue;
        }

        const auto alreadyPresent = std::any_of(
            customBadges.begin(), customBadges.end(),
            [&normalizedBadge](const auto &entry) {
                return entry.first.compare(normalizedBadge,
                                           Qt::CaseInsensitive) == 0;
            });
        if (alreadyPresent)
        {
            continue;
        }

        customBadges.emplace_back(normalizedBadge, packId);
    }

    if (hasVerified)
    {
        builder
            ->emplace<TextElement>("VERIFIED", MessageElementFlag::BadgeVanity,
                                   MessageColor::System,
                                   FontStyle::ChatMediumSmall)
            ->setTooltip("OpenEmote verified identity (Twitch OAuth)");
        metrics.textBadgeCount++;
    }

    if (hasDev)
    {
        builder
            ->emplace<TextElement>("DEV", MessageElementFlag::BadgeVanity,
                                   MessageColor::System,
                                   FontStyle::ChatMediumSmall)
            ->setTooltip("OpenEmote dev");
        metrics.textBadgeCount++;
    }

    constexpr size_t MAX_CUSTOM_BADGES = 3;
    for (size_t i = 0; i < customBadges.size() && i < MAX_CUSTOM_BADGES; i++)
    {
        const auto &[badgeLabel, packId] = customBadges.at(i);
        const auto tooltip = packId.isEmpty()
                                 ? QString("OpenEmote custom badge")
                                 : QString("OpenEmote badge pack: %1")
                                       .arg(packId);
        builder
            ->emplace<TextElement>(badgeLabel, MessageElementFlag::BadgeVanity,
                                   MessageColor::System,
                                   FontStyle::ChatMediumSmall)
            ->setTooltip(tooltip);
        metrics.textBadgeCount++;
    }

    return metrics;
}

void appendOpenEmoteIdentityRailSpacer(
    MessageBuilder *builder, const OpenEmoteIdentityMetrics &metrics)
{
    if (getSettings()->openEmoteBotCompatibilityMode.getValue())
    {
        return;
    }

    if (!getSettings()->openEmoteIdentityRailEnabled)
    {
        return;
    }

    const auto railWidth =
        std::clamp(getSettings()->openEmoteIdentityRailWidth.getValue(), 48, 180);
    const auto minRowHeight = std::clamp(
        getSettings()->openEmoteIdentityRailMinRowHeight.getValue(), 16, 40);

    constexpr int AVATAR_WIDTH = 20;
    constexpr int STATUS_BADGE_WIDTH = 18;
    constexpr int TEXT_BADGE_WIDTH = 16;
    const auto usedWidth = AVATAR_WIDTH +
                           (metrics.statusBadgeCount * STATUS_BADGE_WIDTH) +
                           (metrics.textBadgeCount * TEXT_BADGE_WIDTH);
    const auto spacerWidth = std::max(0, railWidth - usedWidth);

    builder->emplace<FixedSpaceElement>(
        float(spacerWidth), float(minRowHeight),
        MessageElementFlags({MessageElementFlag::Username,
                             MessageElementFlag::BadgeVanity,
                             MessageElementFlag::ReplyButton,
                             MessageElementFlag::Text}));
}

void appendOpenEmoteAvatarDecorators(MessageBuilder *builder,
                                     const QVariantMap &tags)
{
    if (getSettings()->openEmoteBotCompatibilityMode.getValue())
    {
        return;
    }

    QStringList decorators;
    auto addDecorator = [&decorators](const QString &decorator) {
        auto value = decorator.trimmed();
        if (value.isEmpty())
        {
            return;
        }
        if (value.compare("founder", Qt::CaseInsensitive) == 0)
        {
            value = "dev";
        }
        if (decorators.contains(value, Qt::CaseInsensitive))
        {
            return;
        }
        decorators.append(value.toUpper());
    };

    for (const auto &token :
         parseTagString(tags.value("openemote-decorators").toString())
             .split(',', Qt::SkipEmptyParts))
    {
        addDecorator(token.left(12));
    }

    for (const auto &token :
         parseTagString(tags.value("openemote-badges").toString())
             .split(',', Qt::SkipEmptyParts))
    {
        addDecorator(token.left(12));
    }

    constexpr int MAX_DECORATORS = 3;
    for (int i = 0; i < decorators.size() && i < MAX_DECORATORS; i++)
    {
        const auto &decorator = decorators.at(i);
        builder
            ->emplace<TextElement>(QString("[%1]").arg(decorator),
                                   MessageElementFlag::Username,
                                   MessageColor::System,
                                   FontStyle::ChatMediumSmall)
            ->setTooltip(QString("Avatar decorator: %1").arg(decorator));
    }
}

void appendOpenEmoteCompactReplyButton(
    MessageBuilder *builder, const std::shared_ptr<MessageThread> &thread)
{
    if (thread)
    {
        if (!getSettings()->openEmoteBotCompatibilityMode.getValue() &&
            getSettings()->openEmoteShowThreadActivityIndicator)
        {
            const auto replies = thread->liveCount();
            if (replies > 0)
            {
                builder
                    ->emplace<TextElement>(u"•"_s, MessageElementFlag::ReplyButton,
                                           MessageColor::System,
                                           FontStyle::ChatMediumBold)
                    ->setLink({Link::ViewThread, thread->rootId()})
                    ->setTooltip(QString::number(replies));
            }
        }

        auto &img = getResources().buttons.replyThreadDark;
        builder
            ->emplace<CircularImageElement>(
                Image::fromResourcePixmap(img, 0.15), 2, Qt::gray,
                MessageElementFlag::ReplyButton)
            ->setLink({Link::ViewThread, thread->rootId()})
            ->setTooltip("View reply thread");
    }
    else
    {
        auto &img = getResources().buttons.replyDark;
        builder
            ->emplace<CircularImageElement>(
                Image::fromResourcePixmap(img, 0.15), 2, Qt::gray,
                MessageElementFlag::ReplyButton)
            ->setLink({Link::ReplyToMessage, builder->message().id})
            ->setTooltip("Reply to message");
    }
}

QString normalizeCrossChannelName(QString name)
{
    name = name.trimmed().toLower();
    while (name.startsWith('#'))
    {
        name.remove(0, 1);
    }
    return name;
}

QSet<QString> parseCrossChannelSet(const QString &csv)
{
    QSet<QString> set;
    for (const auto &entry : csv.split(',', Qt::SkipEmptyParts))
    {
        auto normalized = normalizeCrossChannelName(entry);
        if (!normalized.isEmpty())
        {
            set.insert(normalized);
        }
    }
    return set;
}

bool isCrossChannelAllowed(const QString &sourceChannelName,
                           const QSet<QString> &allowChannels,
                           const QSet<QString> &blockChannels,
                           bool allowlistOnly)
{
    if (sourceChannelName.isEmpty() || blockChannels.contains(sourceChannelName))
    {
        return false;
    }

    if (allowlistOnly)
    {
        return allowChannels.contains(sourceChannelName);
    }

    return true;
}

struct CrossChannelEmoteCache {
    QHash<QString, EmotePtr> bttv;
    QHash<QString, EmotePtr> ffz;
    QHash<QString, EmotePtr> seventv;
    QString signature;
    qint64 builtAtMs = 0;
};

CrossChannelEmoteCache &crossChannelEmoteCache()
{
    static CrossChannelEmoteCache cache;
    return cache;
}

QString crossChannelEmoteCacheSignature()
{
    const auto *settings = getSettings();
    return QStringLiteral("%1|%2|%3|%4")
        .arg(settings->openEmoteEnableCrossChannelEmotes.getValue() ? "1"
                                                                     : "0")
        .arg(settings->openEmoteCrossChannelEmotesAllowlistMode.getValue()
                 ? "1"
                 : "0")
        .arg(settings->openEmoteCrossChannelEmotesAllowChannels.getValue())
        .arg(settings->openEmoteCrossChannelEmotesBlockChannels.getValue());
}

CrossChannelEmoteCache &getCrossChannelEmoteCache()
{
    constexpr qint64 TTL_MS = 5000;

    auto &cache = crossChannelEmoteCache();
    const auto signature = crossChannelEmoteCacheSignature();
    const auto now = QDateTime::currentMSecsSinceEpoch();

    if (cache.signature == signature && (now - cache.builtAtMs) < TTL_MS)
    {
        return cache;
    }

    cache.bttv.clear();
    cache.ffz.clear();
    cache.seventv.clear();
    cache.signature = signature;
    cache.builtAtMs = now;

    if (!getSettings()->openEmoteEnableCrossChannelEmotes.getValue())
    {
        return cache;
    }

    const bool allowlistOnly =
        getSettings()->openEmoteCrossChannelEmotesAllowlistMode.getValue();
    const auto allowChannels = parseCrossChannelSet(
        getSettings()->openEmoteCrossChannelEmotesAllowChannels);
    const auto blockChannels = parseCrossChannelSet(
        getSettings()->openEmoteCrossChannelEmotesBlockChannels);

    auto mergeInto = [](const EmoteMap &map, QHash<QString, EmotePtr> &out) {
        for (const auto &entry : map)
        {
            if (!out.contains(entry.first.string))
            {
                out.insert(entry.first.string, entry.second);
            }
        }
    };

    getApp()->getTwitch()->forEachChannel([&](const auto &channel) {
        auto *twitchChannel = dynamic_cast<TwitchChannel *>(channel.get());
        if (twitchChannel == nullptr)
        {
            return;
        }

        const auto sourceChannelName =
            normalizeCrossChannelName(twitchChannel->getName());
        if (!isCrossChannelAllowed(sourceChannelName, allowChannels,
                                   blockChannels, allowlistOnly))
        {
            return;
        }

        if (auto bttv = twitchChannel->bttvEmotes())
        {
            mergeInto(*bttv, cache.bttv);
        }
        if (auto ffz = twitchChannel->ffzEmotes())
        {
            mergeInto(*ffz, cache.ffz);
        }
        if (auto seventv = twitchChannel->seventvEmotes())
        {
            mergeInto(*seventv, cache.seventv);
        }
    });

    return cache;
}

EmotePtr parseEmote(TwitchChannel *twitchChannel, const EmoteName &name)
{
    // Emote order:
    //  - FrankerFaceZ Channel
    //  - BetterTTV Channel
    //  - 7TV Channel
    //  - FrankerFaceZ Global
    //  - BetterTTV Global
    //  - 7TV Global

    const auto *globalFfzEmotes = getApp()->getFfzEmotes();
    const auto *globalBttvEmotes = getApp()->getBttvEmotes();
    const auto *globalSeventvEmotes = getApp()->getSeventvEmotes();

    std::optional<EmotePtr> emote{};

    if (twitchChannel != nullptr)
    {
        // Check for channel emotes

        emote = twitchChannel->ffzEmote(name);
        if (emote)
        {
            return *emote;
        }

        emote = twitchChannel->bttvEmote(name);
        if (emote)
        {
            return *emote;
        }

        emote = twitchChannel->seventvEmote(name);
        if (emote)
        {
            return *emote;
        }
    }

    // Check for global emotes

    emote = globalFfzEmotes->emote(name);
    if (emote)
    {
        return *emote;
    }

    emote = globalBttvEmotes->emote(name);
    if (emote)
    {
        return *emote;
    }

    emote = globalSeventvEmotes->globalEmote(name);
    if (emote)
    {
        return *emote;
    }

    if (getSettings()->openEmoteEnableCrossChannelEmotes.getValue())
    {
        const auto &crossCache = getCrossChannelEmoteCache();

        const auto ffzIt = crossCache.ffz.constFind(name.string);
        if (ffzIt != crossCache.ffz.cend())
        {
            return ffzIt.value();
        }

        const auto bttvIt = crossCache.bttv.constFind(name.string);
        if (bttvIt != crossCache.bttv.cend())
        {
            return bttvIt.value();
        }

        const auto seventvIt = crossCache.seventv.constFind(name.string);
        if (seventvIt != crossCache.seventv.cend())
        {
            return seventvIt.value();
        }
    }

    return {};
}

}  // namespace

namespace chatterino {

MessagePtr makeSystemMessage(const QString &text)
{
    return MessageBuilder(systemMessage, text).release();
}

MessagePtr makeSystemMessage(const QString &text, const QTime &time)
{
    return MessageBuilder(systemMessage, text, time).release();
}

MessageBuilder::MessageBuilder()
    : message_(std::make_shared<Message>())
{
}

MessageBuilder::MessageBuilder(SystemMessageTag, const QString &text,
                               const QTime &time)
    : MessageBuilder()
{
    this->emplace<TimestampElement>(time);

    // check system message for links
    // (e.g. needed for sub ticket message in sub only mode)
    const QStringList textFragments =
        text.split(SPACE_REGEX, Qt::SkipEmptyParts);
    for (const auto &word : textFragments)
    {
        auto link = linkparser::parse(word);
        if (link)
        {
            this->addLink(*link, word);
            continue;
        }

        this->appendOrEmplaceText(word, MessageColor::System);
    }
    this->message().flags.set(MessageFlag::System);
    this->message().flags.set(MessageFlag::DoNotTriggerNotification);
    this->message().messageText = text;
    this->message().searchText = text;
}

MessagePtrMut MessageBuilder::makeSystemMessageWithUser(
    const QString &text, const QString &loginName, const QString &displayName,
    const MessageColor &userColor, const QTime &time)
{
    MessageBuilder builder;
    builder.emplace<TimestampElement>(time);

    const auto textFragments = text.split(SPACE_REGEX, Qt::SkipEmptyParts);
    for (const auto &word : textFragments)
    {
        if (word == displayName)
        {
            builder.emplace<MentionElement>(displayName, loginName,
                                            MessageColor::System, userColor);
            continue;
        }

        builder.appendOrEmplaceText(word, MessageColor::System);
    }

    builder->flags.set(MessageFlag::System);
    builder->flags.set(MessageFlag::DoNotTriggerNotification);
    builder->messageText = text;
    builder->searchText = text;

    return builder.release();
}

MessagePtrMut MessageBuilder::makeSubgiftMessage(const QString &text,
                                                 const QVariantMap &tags,
                                                 const QTime &time,
                                                 TwitchChannel *channel)
{
    const auto *userDataController = getApp()->getUserData();
    assert(userDataController != nullptr);

    MessageBuilder builder;
    builder.emplace<TimestampElement>(time);

    auto gifterLogin = tags.value("login").toString();
    auto gifterDisplayName = tags.value("display-name").toString();
    if (gifterDisplayName.isEmpty())
    {
        gifterDisplayName = gifterLogin;
    }

    auto gifterColor =
        twitch::getUserColor({
                                 .userLogin = gifterLogin,
                                 .userID = tags.value("user-id").toString(),
                                 .userDataController = userDataController,
                                 .channelChatters = channel,
                                 .color = tags.value("color").value<QColor>(),
                             })
            .value_or(MessageColor::System);

    auto recipientLogin =
        tags.value("msg-param-recipient-user-name").toString();
    if (recipientLogin.isEmpty())
    {
        recipientLogin = tags.value("msg-param-recipient-name").toString();
    }
    auto recipientDisplayName =
        tags.value("msg-param-recipient-display-name").toString();
    if (recipientDisplayName.isEmpty())
    {
        recipientDisplayName = recipientLogin;
    }

    auto recipientColor =
        twitch::getUserColor(
            {
                .userLogin = recipientLogin,
                .userID = tags.value("msg-param-recipient-id").toString(),

                .userDataController = userDataController,
                .channelChatters = channel,
            })
            .value_or(MessageColor::System);

    const auto textFragments = text.split(SPACE_REGEX, Qt::SkipEmptyParts);
    for (const auto &word : textFragments)
    {
        if (word == gifterDisplayName)
        {
            builder.emplace<MentionElement>(gifterDisplayName, gifterLogin,
                                            MessageColor::System, gifterColor);
            continue;
        }
        if (word.endsWith('!') &&
            word.size() == recipientDisplayName.size() + 1 &&
            word.startsWith(recipientDisplayName))
        {
            builder
                .emplace<MentionElement>(recipientDisplayName, recipientLogin,
                                         MessageColor::System, recipientColor)
                ->setTrailingSpace(false);
            builder.emplace<TextElement>(u"!"_s, MessageElementFlag::Text,
                                         MessageColor::System);
            continue;
        }

        builder.appendOrEmplaceText(word, MessageColor::System);
    }

    builder->flags.set(MessageFlag::System);
    builder->flags.set(MessageFlag::DoNotTriggerNotification);
    builder->messageText = text;
    builder->searchText = text;

    return builder.release();
}

MessageBuilder::MessageBuilder(TimeoutMessageTag, const QString &timeoutUser,
                               const QString &sourceUser,
                               const QString &channel,
                               const QString &systemMessageText, uint32_t times,
                               const QDateTime &time)
    : MessageBuilder()
{
    QString usernameText = systemMessageText.split(" ").at(0);
    QString remainder = systemMessageText.mid(usernameText.length() + 1);
    bool timeoutUserIsFirst =
        usernameText == "You" || timeoutUser == usernameText;
    QString messageText;

    this->emplace<TimestampElement>(time.time());
    this->emplaceSystemTextAndUpdate(usernameText, messageText)
        ->setLink(
            {Link::UserInfo, timeoutUserIsFirst ? timeoutUser : sourceUser});

    auto appendUser = [&](const QString &name) {
        auto pos = remainder.indexOf(name);
        if (pos > 0)
        {
            QString start = remainder.mid(0, pos - 1);
            remainder = remainder.mid(pos + name.length());

            this->emplaceSystemTextAndUpdate(start, messageText);
            auto *el = this->emplaceSystemTextAndUpdate(name, messageText)
                           ->setLink({Link::UserInfo, name});
            if (remainder.startsWith(' '))
            {
                removeFirstQS(remainder);
            }
            else
            {
                assert(messageText.endsWith(' '));
                removeLastQS(messageText);
                el->setTrailingSpace(false);
            }
        }
    };

    if (!sourceUser.isEmpty())
    {
        // the second username in the message
        appendUser(timeoutUserIsFirst ? sourceUser : timeoutUser);
    }

    if (!channel.isEmpty())
    {
        appendUser(channel);
    }

    this->emplaceSystemTextAndUpdate(
        QString("%1 (%2 times)").arg(remainder.trimmed()).arg(times),
        messageText);

    this->message().messageText = messageText;
    this->message().searchText = messageText;
    this->message().serverReceivedTime = time;
}

MessageBuilder::MessageBuilder(TimeoutMessageTag, const QString &username,
                               const QString &durationInSeconds,
                               bool multipleTimes, const QDateTime &time)
    : MessageBuilder()
{
    QString fullText;
    QString text;

    this->emplace<TimestampElement>(time.time());
    this->emplaceSystemTextAndUpdate(username, fullText)
        ->setLink({Link::UserInfo, username});

    if (!durationInSeconds.isEmpty())
    {
        text.append("has been timed out");

        // TODO: Implement who timed the user out

        text.append(" for ");
        bool ok = true;
        int timeoutSeconds = durationInSeconds.toInt(&ok);
        if (ok)
        {
            text.append(formatTime(timeoutSeconds));
        }
    }
    else
    {
        text.append("has been permanently banned");
    }

    text.append(".");

    if (multipleTimes)
    {
        text.append(" (multiple times)");
    }

    this->message().flags.set(MessageFlag::System);
    this->message().flags.set(MessageFlag::Timeout);
    this->message().flags.set(MessageFlag::ModerationAction);
    this->message().flags.set(MessageFlag::DoNotTriggerNotification);
    this->message().timeoutUser = username;

    this->emplaceSystemTextAndUpdate(text, fullText);
    this->message().messageText = fullText;
    this->message().searchText = fullText;
    this->message().serverReceivedTime = time;
}

MessageBuilder::MessageBuilder(LiveUpdatesAddEmoteMessageTag /*unused*/,
                               const QString &platform, const QString &actor,
                               const std::vector<QString> &emoteNames)
    : MessageBuilder()
{
    auto text =
        formatUpdatedEmoteList(platform, emoteNames, true, actor.isEmpty());

    this->emplace<TimestampElement>();
    if (!actor.isEmpty())
    {
        this->emplace<TextElement>(actor, MessageElementFlag::Username,
                                   MessageColor::System)
            ->setLink({Link::UserInfo, actor});
    }
    this->emplace<TextElement>(text, MessageElementFlag::Text,
                               MessageColor::System);

    QString finalText;
    if (actor.isEmpty())
    {
        finalText = text;
    }
    else
    {
        finalText = QString("%1 %2").arg(actor, text);
    }

    this->message().loginName = actor;
    this->message().messageText = finalText;
    this->message().searchText = finalText;

    this->message().flags.set(MessageFlag::System);
    this->message().flags.set(MessageFlag::LiveUpdatesAdd);
    this->message().flags.set(MessageFlag::DoNotTriggerNotification);
}

MessageBuilder::MessageBuilder(LiveUpdatesRemoveEmoteMessageTag /*unused*/,
                               const QString &platform, const QString &actor,
                               const std::vector<QString> &emoteNames)
    : MessageBuilder()
{
    auto text =
        formatUpdatedEmoteList(platform, emoteNames, false, actor.isEmpty());

    this->emplace<TimestampElement>();
    if (!actor.isEmpty())
    {
        this->emplace<TextElement>(actor, MessageElementFlag::Username,
                                   MessageColor::System)
            ->setLink({Link::UserInfo, actor});
    }
    this->emplace<TextElement>(text, MessageElementFlag::Text,
                               MessageColor::System);

    QString finalText;
    if (actor.isEmpty())
    {
        finalText = text;
    }
    else
    {
        finalText = QString("%1 %2").arg(actor, text);
    }

    this->message().loginName = actor;
    this->message().messageText = finalText;
    this->message().searchText = finalText;

    this->message().flags.set(MessageFlag::System);
    this->message().flags.set(MessageFlag::LiveUpdatesRemove);
    this->message().flags.set(MessageFlag::DoNotTriggerNotification);
}

MessageBuilder::MessageBuilder(LiveUpdatesUpdateEmoteMessageTag /*unused*/,
                               const QString &platform, const QString &actor,
                               const QString &emoteName,
                               const QString &oldEmoteName)
    : MessageBuilder()
{
    QString text;
    if (actor.isEmpty())
    {
        text = "Renamed";
    }
    else
    {
        text = "renamed";
    }
    text +=
        QString(" %1 emote %2 to %3.").arg(platform, oldEmoteName, emoteName);

    this->emplace<TimestampElement>();
    if (!actor.isEmpty())
    {
        this->emplace<TextElement>(actor, MessageElementFlag::Username,
                                   MessageColor::System)
            ->setLink({Link::UserInfo, actor});
    }
    this->emplace<TextElement>(text, MessageElementFlag::Text,
                               MessageColor::System);

    QString finalText;
    if (actor.isEmpty())
    {
        finalText = text;
    }
    else
    {
        finalText = QString("%1 %2").arg(actor, text);
    }

    this->message().loginName = actor;
    this->message().messageText = finalText;
    this->message().searchText = finalText;

    this->message().flags.set(MessageFlag::System);
    this->message().flags.set(MessageFlag::LiveUpdatesUpdate);
    this->message().flags.set(MessageFlag::DoNotTriggerNotification);
}

MessageBuilder::MessageBuilder(LiveUpdatesUpdateEmoteSetMessageTag /*unused*/,
                               const QString &platform, const QString &actor,
                               const QString &emoteSetName)
    : MessageBuilder()
{
    auto text = QString("switched the active %1 Emote Set to \"%2\".")
                    .arg(platform, emoteSetName);

    this->emplace<TimestampElement>();
    this->emplace<TextElement>(actor, MessageElementFlag::Username,
                               MessageColor::System)
        ->setLink({Link::UserInfo, actor});
    this->emplace<TextElement>(text, MessageElementFlag::Text,
                               MessageColor::System);

    auto finalText = QString("%1 %2").arg(actor, text);

    this->message().loginName = actor;
    this->message().messageText = finalText;
    this->message().searchText = finalText;

    this->message().flags.set(MessageFlag::System);
    this->message().flags.set(MessageFlag::LiveUpdatesUpdate);
    this->message().flags.set(MessageFlag::DoNotTriggerNotification);
}

MessageBuilder::MessageBuilder(ImageUploaderResultTag /*unused*/,
                               const QString &imageLink,
                               const QString &deletionLink,
                               size_t imagesStillQueued, size_t secondsLeft)
    : MessageBuilder()
{
    this->message().flags.set(MessageFlag::System);
    this->message().flags.set(MessageFlag::DoNotTriggerNotification);

    this->emplace<TimestampElement>();

    using MEF = MessageElementFlag;
    auto addText = [this](QString text,
                          MessageColor color =
                              MessageColor::System) -> TextElement * {
        this->message().searchText += text;
        this->message().messageText += text;
        return this->emplace<TextElement>(text, MEF::Text, color);
    };

    addText("Your image has been uploaded to");

    // ASSUMPTION: the user gave this uploader configuration to the program
    // therefore they trust that the host is not wrong/malicious. This doesn't obey getSettings()->lowercaseDomains.
    // This also ensures that the LinkResolver doesn't get these links.
    addText(imageLink, MessageColor::Link)
        ->setLink({Link::Url, imageLink})
        ->setTrailingSpace(!deletionLink.isEmpty());

    if (!deletionLink.isEmpty())
    {
        addText("(Deletion link:");
        addText(deletionLink, MessageColor::Link)
            ->setLink({Link::Url, deletionLink})
            ->setTrailingSpace(false);
        addText(")")->setTrailingSpace(false);
    }
    addText(".");

    if (imagesStillQueued == 0)
    {
        return;
    }

    addText(QString("%1 left. Please wait until all of them are uploaded. "
                    "About %2 seconds left.")
                .arg(imagesStillQueued)
                .arg(secondsLeft));
}

Message *MessageBuilder::operator->()
{
    return this->message_.get();
}

Message &MessageBuilder::message()
{
    return *this->message_;
}

MessagePtrMut MessageBuilder::release()
{
    std::shared_ptr<Message> ptr;
    this->message_.swap(ptr);
    return ptr;
}

std::weak_ptr<const Message> MessageBuilder::weakOf()
{
    return this->message_;
}

void MessageBuilder::append(std::unique_ptr<MessageElement> element)
{
    this->message().elements.push_back(std::move(element));
}

void MessageBuilder::addLink(const linkparser::Parsed &parsedLink,
                             QStringView source)
{
    QString lowercaseLinkString;
    QString origLink = parsedLink.link.toString();
    QString fullUrl;

    if (parsedLink.protocol.isNull())
    {
        fullUrl = QStringLiteral("http://") + origLink;
    }
    else
    {
        lowercaseLinkString += parsedLink.protocol;
        fullUrl = origLink;
    }

    lowercaseLinkString += parsedLink.host.toString().toLower();
    lowercaseLinkString += parsedLink.rest;

    auto textColor = MessageColor(MessageColor::Link);

    if (parsedLink.hasPrefix(source))
    {
        this->emplace<TextElement>(parsedLink.prefix(source).toString(),
                                   MessageElementFlag::Text, this->textColor_)
            ->setTrailingSpace(false);
    }
    auto *el = this->emplace<LinkElement>(
        LinkElement::Parsed{.lowercase = lowercaseLinkString,
                            .original = origLink},
        fullUrl, MessageElementFlag::Text, textColor);
    if (parsedLink.hasSuffix(source))
    {
        el->setTrailingSpace(false);
        this->emplace<TextElement>(parsedLink.suffix(source).toString(),
                                   MessageElementFlag::Text, this->textColor_);
    }

    getApp()->getLinkResolver()->resolve(el->linkInfo());
}

bool MessageBuilder::isIgnored(const QString &originalMessage,
                               const QString &userID, const Channel *channel)
{
    return isIgnoredMessage({
        .message = originalMessage,
        .twitchUserID = userID,
        .isMod = channel->isMod(),
        .isBroadcaster = channel->isBroadcaster(),
    });
}

void MessageBuilder::appendOrEmplaceText(const QString &text,
                                         MessageColor color)
{
    auto fallback = [&] {
        this->emplace<TextElement>(text, MessageElementFlag::Text, color);
    };
    if (this->message_->elements.empty())
    {
        fallback();
        return;
    }

    auto *back =
        dynamic_cast<TextElement *>(this->message_->elements.back().get());
    if (!back ||                                         //
        dynamic_cast<MentionElement *>(back) ||          //
        dynamic_cast<LinkElement *>(back) ||             //
        !back->hasTrailingSpace() ||                     //
        back->getFlags() != MessageElementFlag::Text ||  //
        back->color() != color)
    {
        fallback();
        return;
    }

    back->appendText(text);
}

void MessageBuilder::appendOrEmplaceSystemTextAndUpdate(const QString &text,
                                                        QString &toUpdate)
{
    toUpdate.append(text);
    toUpdate.append(' ');
    this->appendOrEmplaceText(text, MessageColor::System);
}

void MessageBuilder::triggerHighlights(const Channel *channel,
                                       const HighlightAlert &alert)
{
    if (!alert.windowAlert && !alert.playSound)
    {
        return;
    }
    actuallyTriggerHighlights(channel->getName(), alert.playSound,
                              alert.customSound, alert.windowAlert);
}

void MessageBuilder::appendChannelPointRewardMessage(
    const ChannelPointReward &reward, bool isMod, bool isBroadcaster)
{
    if (isIgnoredMessage({
            .message = {},
            .twitchUserID = reward.user.id,
            .isMod = isMod,
            .isBroadcaster = isBroadcaster,
        }))
    {
        return;
    }

    this->emplace<TimestampElement>();
    QString redeemed = "Redeemed";
    QStringList textList;
    if (!reward.isUserInputRequired)
    {
        this->emplace<TextElement>(
                reward.user.login, MessageElementFlag::ChannelPointReward,
                MessageColor::Text, FontStyle::ChatMediumBold)
            ->setLink({Link::UserInfo, reward.user.login});
        redeemed = "redeemed";
        textList.append(reward.user.login);
    }
    this->emplace<TextElement>(redeemed,
                               MessageElementFlag::ChannelPointReward);
    if (reward.id == "CELEBRATION")
    {
        const auto emotePtr =
            getApp()->getEmotes()->getTwitchEmotes()->getOrCreateEmote(
                EmoteId{reward.emoteId}, EmoteName{reward.emoteName});
        this->emplace<EmoteElement>(emotePtr,
                                    MessageElementFlag::ChannelPointReward,
                                    MessageColor::Text);
    }
    this->emplace<TextElement>(reward.title,
                               MessageElementFlag::ChannelPointReward,
                               MessageColor::Text, FontStyle::ChatMediumBold);
    this->emplace<ScalingImageElement>(
        reward.image, MessageElementFlag::ChannelPointRewardImage);
    this->emplace<TextElement>(QString::number(reward.cost),
                               MessageElementFlag::ChannelPointReward,
                               MessageColor::Text, FontStyle::ChatMediumBold);
    if (reward.isBits)
    {
        this->emplace<TextElement>(
            "bits", MessageElementFlag::ChannelPointReward, MessageColor::Text,
            FontStyle::ChatMediumBold);
    }
    if (reward.isUserInputRequired)
    {
        this->emplace<LinebreakElement>(MessageElementFlag::ChannelPointReward);
    }

    this->message().flags.set(MessageFlag::RedeemedChannelPointReward);

    textList.append({redeemed, reward.title, QString::number(reward.cost)});
    this->message().messageText = textList.join(" ");
    this->message().searchText = textList.join(" ");
    if (!reward.user.login.isEmpty())
    {
        this->message().loginName = reward.user.login;
    }

    this->message().reward = std::make_shared<ChannelPointReward>(reward);
}

MessagePtr MessageBuilder::makeChannelPointRewardMessage(
    const ChannelPointReward &reward, bool isMod, bool isBroadcaster)
{
    MessageBuilder builder;

    builder.appendChannelPointRewardMessage(reward, isMod, isBroadcaster);

    return builder.release();
}

MessagePtr MessageBuilder::makeLiveMessage(const QString &channelName,
                                           const QString &channelID,
                                           const QString &title,
                                           MessageFlags extraFlags)
{
    MessageBuilder builder;

    builder.emplace<TimestampElement>();
    builder
        .emplace<TextElement>(channelName, MessageElementFlag::Username,
                              MessageColor::Text, FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, channelName});

    QString text;
    if (getSettings()->showTitleInLiveMessage)
    {
        text = QString("%1 is live: %2").arg(channelName, title);
        builder.emplace<TextElement>("is live:", MessageElementFlag::Text,
                                     MessageColor::Text);
        builder.emplace<TextElement>(title, MessageElementFlag::Text,
                                     MessageColor::Text);
    }
    else
    {
        text = QString("%1 is live!").arg(channelName);
        builder.emplace<TextElement>("is live!", MessageElementFlag::Text,
                                     MessageColor::Text);
    }

    builder.message().messageText = text;
    builder.message().searchText = text;
    builder.message().id = channelID;

    if (!extraFlags.isEmpty())
    {
        builder.message().flags.set(extraFlags);
    }

    return builder.release();
}

MessagePtr MessageBuilder::makeOfflineSystemMessage(const QString &channelName,
                                                    const QString &channelID)
{
    MessageBuilder builder;
    builder.emplace<TimestampElement>();
    builder.message().flags.set(MessageFlag::System);
    builder.message().flags.set(MessageFlag::DoNotTriggerNotification);
    builder
        .emplace<TextElement>(channelName, MessageElementFlag::Username,
                              MessageColor::System, FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, channelName});
    builder.emplace<TextElement>("is now offline.", MessageElementFlag::Text,
                                 MessageColor::System);
    auto text = QString("%1 is now offline.").arg(channelName);
    builder.message().messageText = text;
    builder.message().searchText = text;
    builder.message().id = channelID;

    return builder.release();
}

MessagePtr MessageBuilder::makeHostingSystemMessage(const QString &channelName,
                                                    bool hostOn)
{
    MessageBuilder builder;
    QString text;
    builder.emplace<TimestampElement>();
    builder.message().flags.set(MessageFlag::System);
    builder.message().flags.set(MessageFlag::DoNotTriggerNotification);
    if (hostOn)
    {
        builder.emplace<TextElement>("Now hosting", MessageElementFlag::Text,
                                     MessageColor::System);
        builder
            .emplace<TextElement>(
                channelName + ".", MessageElementFlag::Username,
                MessageColor::System, FontStyle::ChatMediumBold)
            ->setLink({Link::UserInfo, channelName});
        text = QString("Now hosting %1.").arg(channelName);
    }
    else
    {
        builder
            .emplace<TextElement>(channelName, MessageElementFlag::Username,
                                  MessageColor::System,
                                  FontStyle::ChatMediumBold)
            ->setLink({Link::UserInfo, channelName});
        builder.emplace<TextElement>("has gone offline. Exiting host mode.",
                                     MessageElementFlag::Text,
                                     MessageColor::System);
        text =
            QString("%1 has gone offline. Exiting host mode.").arg(channelName);
    }
    builder.message().messageText = text;
    builder.message().searchText = text;
    return builder.release();
}

MessagePtr MessageBuilder::makeDeletionMessageFromIRC(
    const MessagePtr &originalMessage)
{
    MessageBuilder builder;

    builder.emplace<TimestampElement>();
    builder.message().flags.set(MessageFlag::System);
    builder.message().flags.set(MessageFlag::DoNotTriggerNotification);
    builder.message().flags.set(MessageFlag::ModerationAction);
    // TODO(mm2pl): If or when jumping to a single message gets implemented a link,
    // add a link to the originalMessage
    builder.emplace<TextElement>("A message from", MessageElementFlag::Text,
                                 MessageColor::System);
    builder
        .emplace<TextElement>(originalMessage->displayName,
                              MessageElementFlag::Username,
                              MessageColor::System, FontStyle::ChatMediumBold)
        ->setLink({Link::UserInfo, originalMessage->loginName});
    builder.emplace<TextElement>("was deleted:", MessageElementFlag::Text,
                                 MessageColor::System);

    auto deletedMessageText = originalMessage->messageText;
    auto limit = getSettings()->deletedMessageLengthLimit.getValue();
    if (limit > 0 && deletedMessageText.length() > limit)
    {
        deletedMessageText = deletedMessageText.left(limit) + "…";
    }

    builder
        .emplace<TextElement>(deletedMessageText, MessageElementFlag::Text,
                              MessageColor::Text)
        ->setLink({Link::JumpToMessage, originalMessage->id});
    builder.message().timeoutUser = "msg:" + originalMessage->id;

    const auto deletionText =
        QString("A message from %1 was deleted: %2")
            .arg(originalMessage->loginName, deletedMessageText);
    builder.message().messageText = deletionText;
    builder.message().searchText = deletionText;

    return builder.release();
}

MessagePtr MessageBuilder::makeListOfUsersMessage(QString prefix,
                                                  QStringList users,
                                                  Channel *channel,
                                                  MessageFlags extraFlags)
{
    MessageBuilder builder;

    QString text = prefix + users.join(", ");

    builder.message().messageText = text;
    builder.message().searchText = text;

    builder.emplace<TimestampElement>();
    builder.message().flags.set(MessageFlag::System);
    builder.message().flags.set(MessageFlag::DoNotTriggerNotification);
    builder.emplace<TextElement>(prefix, MessageElementFlag::Text,
                                 MessageColor::System);
    bool isFirst = true;
    auto *tc = dynamic_cast<TwitchChannel *>(channel);
    for (const QString &username : users)
    {
        if (!isFirst)
        {
            // this is used to add the ", " after each but the last entry
            builder.emplace<TextElement>(",", MessageElementFlag::Text,
                                         MessageColor::System);
        }
        isFirst = false;

        MessageColor color = MessageColor::System;

        if (tc)
        {
            if (auto userColor = tc->getUserColor(username);
                userColor.isValid())
            {
                color = MessageColor(userColor);
            }
        }

        // TODO: Ensure we make use of display name / username(login name) correctly here
        builder
            .emplace<MentionElement>(username, username, MessageColor::System,
                                     color)
            ->setTrailingSpace(false);
    }

    if (!extraFlags.isEmpty())
    {
        builder.message().flags.set(extraFlags);
    }

    return builder.release();
}

MessagePtr MessageBuilder::makeListOfUsersMessage(
    QString prefix, const std::vector<HelixModerator> &users, Channel *channel,
    MessageFlags extraFlags)
{
    MessageBuilder builder;

    QString text = prefix;

    builder.emplace<TimestampElement>();
    builder.message().flags.set(MessageFlag::System);
    builder.message().flags.set(MessageFlag::DoNotTriggerNotification);
    builder.emplace<TextElement>(prefix, MessageElementFlag::Text,
                                 MessageColor::System);
    bool isFirst = true;
    auto *tc = dynamic_cast<TwitchChannel *>(channel);
    for (const auto &user : users)
    {
        if (!isFirst)
        {
            // this is used to add the ", " after each but the last entry
            builder.emplace<TextElement>(",", MessageElementFlag::Text,
                                         MessageColor::System);
            text += QString(", %1").arg(user.userName);
        }
        else
        {
            text += user.userName;
        }
        isFirst = false;

        MessageColor color = MessageColor::System;

        if (tc)
        {
            if (auto userColor = tc->getUserColor(user.userLogin);
                userColor.isValid())
            {
                color = MessageColor(userColor);
            }
        }

        builder
            .emplace<MentionElement>(user.userName, user.userLogin,
                                     MessageColor::System, color)
            ->setTrailingSpace(false);
    }

    builder.message().messageText = text;
    builder.message().searchText = text;

    if (!extraFlags.isEmpty())
    {
        builder.message().flags.set(extraFlags);
    }

    return builder.release();
}

MessagePtr MessageBuilder::buildHypeChatMessage(
    Communi::IrcPrivateMessage *message)
{
    auto levelID = message->tag(u"pinned-chat-paid-level"_s).toString();
    auto currency = message->tag(u"pinned-chat-paid-currency"_s).toString();
    bool okAmount = false;
    auto amount = message->tag(u"pinned-chat-paid-amount"_s).toInt(&okAmount);
    bool okExponent = false;
    auto exponent =
        message->tag(u"pinned-chat-paid-exponent"_s).toInt(&okExponent);
    if (!okAmount || !okExponent || currency.isEmpty())
    {
        return {};
    }
    // additionally, there's `pinned-chat-paid-is-system-message` which isn't used by Chatterino.

    QString subtitle;
    auto levelIt = HYPE_CHAT_PAID_LEVEL.find(levelID);
    if (levelIt != HYPE_CHAT_PAID_LEVEL.end())
    {
        const auto &level = levelIt->second;
        subtitle = u"Level %1 Hype Chat (%2) "_s.arg(level.numeric)
                       .arg(formatTime(level.duration));
    }
    else
    {
        subtitle = u"Hype Chat "_s;
    }

    // actualAmount = amount * 10^(-exponent)
    double actualAmount = std::pow(10.0, double(-exponent)) * double(amount);

    auto locale = getSystemLocale();
    subtitle += locale.toCurrencyString(actualAmount, currency);

    auto dt = calculateMessageTime(message);
    MessageBuilder builder(systemMessage, parseTagString(subtitle), dt.time());
    builder->flags.set(MessageFlag::ElevatedMessage);
    return builder.release();
}

MessagePtrMut MessageBuilder::makeMissingScopesMessage(
    const QString &missingScopes)
{
    QString warnText =
        u"Your account is missing the following permission(s): " %
        missingScopes % u". Some features might not work correctly.";
    auto linkText = u"Consider re-adding your account."_s;

    MessageBuilder builder;
    QString text = warnText % ' ' % linkText;
    builder->messageText = text;
    builder->searchText = text;
    builder->flags.set(MessageFlag::System,
                       MessageFlag::DoNotTriggerNotification);

    builder.emplace<TimestampElement>();
    builder.emplace<TextElement>(warnText, MessageElementFlag::Text,
                                 MessageColor::System);
    builder
        .emplace<TextElement>(linkText, MessageElementFlag::Text,
                              MessageColor::Link)
        ->setLink({Link::OpenAccountsPage, {}});

    return builder.release();
}

MessagePtrMut MessageBuilder::makeClearChatMessage(const QDateTime &now,
                                                   const QString &actor,
                                                   uint32_t count)
{
    MessageBuilder builder;
    builder.emplace<TimestampElement>(now.time());
    builder->count = count;
    builder->serverReceivedTime = now;
    builder.message().flags.set(
        MessageFlag::System, MessageFlag::DoNotTriggerNotification,
        MessageFlag::ClearChat, MessageFlag::ModerationAction);

    QString messageText;
    if (actor.isEmpty())
    {
        builder.emplaceSystemTextAndUpdate(
            "Chat has been cleared by a moderator.", messageText);
    }
    else
    {
        builder.message().flags.set(MessageFlag::PubSub);
        builder.emplace<MentionElement>(actor, actor, MessageColor::System,
                                        MessageColor::System);
        messageText = actor + ' ';
        builder.emplaceSystemTextAndUpdate("cleared the chat.", messageText);
        builder->timeoutUser = actor;
    }

    if (count > 1)
    {
        builder.appendOrEmplaceSystemTextAndUpdate(
            '(' % QString::number(count) % u" times)", messageText);
    }

    builder->messageText = messageText;
    builder->searchText = messageText;

    return builder.release();
}

std::pair<MessagePtrMut, HighlightAlert> MessageBuilder::makeIrcMessage(
    /* mutable */ Channel *channel, const Communi::IrcMessage *ircMessage,
    const MessageParseArgs &args, /* mutable */ QString content,
    const QString::size_type messageOffset,
    const std::shared_ptr<MessageThread> &thread, const MessagePtr &parent)
{
    assert(ircMessage != nullptr);
    assert(channel != nullptr);

    auto tags = ircMessage->tags();
    if (args.allowIgnore)
    {
        bool ignored = MessageBuilder::isIgnored(
            content, tags.value("user-id").toString(), channel);
        if (ignored)
        {
            return {};
        }
    }

    auto *twitchChannel = dynamic_cast<TwitchChannel *>(channel);

    auto userID = tags.value("user-id").toString();

    MessageBuilder builder;
    builder.parseUsernameColor(tags, userID);
    builder->userID = userID;

    if (args.isAction)
    {
        builder.textColor_ = builder.message_->usernameColor;
        builder->flags.set(MessageFlag::Action);
    }

    builder.parseUsername(ircMessage, twitchChannel,
                          args.trimSubscriberUsername);

    builder->flags.set(MessageFlag::Collapsed);

    bool senderIsBroadcaster = builder->loginName == channel->getName();

    builder->channelName = channel->getName();

    builder.parseMessageID(tags);

    MessageBuilder::parseRoomID(tags, twitchChannel);
    twitchChannel = builder.parseSharedChatInfo(tags, twitchChannel);

    // If it is a reward it has to be appended first
    if (!args.channelPointRewardId.isEmpty())
    {
        assert(twitchChannel != nullptr);
        auto reward =
            twitchChannel->channelPointReward(args.channelPointRewardId);
        if (reward)
        {
            builder.appendChannelPointRewardMessage(*reward, channel->isMod(),
                                                    channel->isBroadcaster());
        }
        builder->flags.set(MessageFlag::RedeemedChannelPointReward);
    }

    builder.appendChannelName(channel);

    if (tags.contains("rm-deleted"))
    {
        builder->flags.set(MessageFlag::Disabled);
    }

    if (tags.contains("msg-id") &&
        tags["msg-id"].toString().split(';').contains("highlighted-message"))
    {
        builder->flags.set(MessageFlag::RedeemedHighlight);
    }

    if (tags.contains("first-msg") && tags["first-msg"].toString() == "1")
    {
        builder->flags.set(MessageFlag::FirstMessage);
    }

    if (tags.contains("pinned-chat-paid-amount"))
    {
        builder->flags.set(MessageFlag::ElevatedMessage);
    }

    if (tags.contains("bits"))
    {
        builder->flags.set(MessageFlag::CheerMessage);
    }

    // reply threads
    builder.parseThread(content, tags, channel, thread, parent);

    // timestamp
    builder->serverReceivedTime = calculateMessageTime(ircMessage);
    parseOpenEmoteAvatarModelMetadata(&builder, tags, content);

    bool shouldAddModerationElements = [&] {
        if (senderIsBroadcaster)
        {
            // You cannot timeout the broadcaster
            return false;
        }

        if (tags.value("user-type").toString() == "mod" &&
            !args.isStaffOrBroadcaster)
        {
            // You cannot timeout moderators UNLESS you are Twitch Staff or the broadcaster of the channel
            return false;
        }

        return true;
    }();
    if (shouldAddModerationElements)
    {
        builder.emplace<TwitchModerationElement>();
    }

    const bool compactAuthorMode =
        !getSettings()->openEmoteBotCompatibilityMode.getValue() &&
        getSettings()->openEmoteCompactAuthorAvatar && !args.isSentWhisper &&
        false &&
        !args.isReceivedWhisper;
    const bool compactHeaderLayout =
        !getSettings()->openEmoteBotCompatibilityMode.getValue() &&
        getSettings()->openEmoteCompactHeaderLayout.getValue() &&
        !args.isSentWhisper && !args.isReceivedWhisper && !args.isAction;
    OpenEmoteIdentityMetrics compactIdentityMetrics;
    if (compactAuthorMode)
    {
        builder.message().twitchBadges = parseBadgeTag(tags);
        builder.message().twitchBadgeInfos = parseBadgeInfoTag(tags);
    }
    else
    {
        builder.appendTwitchBadges(tags, twitchChannel);
        builder.appendChatterinoBadges(userID);
        builder.appendFfzBadges(twitchChannel, userID);
        builder.appendBttvBadges(userID);
        builder.appendSeventvBadges(userID);
    }

    if (compactAuthorMode)
    {
        compactIdentityMetrics =
            appendOpenEmoteCompactRoleBadges(&builder, tags, twitchChannel);
    }
    if (compactHeaderLayout)
    {
        const auto authorText =
            stylizeUsername(builder->loginName, builder.message());
        builder
            .emplace<TextElement>(authorText, MessageElementFlag::RepliedMessage,
                                  builder.usernameColor_,
                                  FontStyle::ChatMediumSmall)
            ->setLink({Link::UserInfo, builder.message().displayName});

        if (thread)
        {
            auto threadRoot = parent ? parent : thread->root();
            if (threadRoot)
            {
                const auto targetText =
                    stylizeUsername(threadRoot->loginName, *threadRoot);
                builder.emplace<TextElement>(" -> ",
                                             MessageElementFlag::RepliedMessage,
                                             MessageColor::System,
                                             FontStyle::ChatMediumSmall);
                builder
                    .emplace<TextElement>(
                        targetText, MessageElementFlag::RepliedMessage,
                        threadRoot->usernameColor, FontStyle::ChatMediumSmall)
                    ->setLink({Link::UserInfo, threadRoot->displayName});
                builder.emplace<TextElement>(": ",
                                             MessageElementFlag::RepliedMessage,
                                             MessageColor::System,
                                             FontStyle::ChatMediumSmall);
                builder
                    .emplace<SingleLineTextElement>(
                        threadRoot->messageText,
                        MessageElementFlags(
                            {MessageElementFlag::RepliedMessage,
                             MessageElementFlag::Text}),
                        MessageColor::Text, FontStyle::ChatMediumSmall)
                    ->setLink({Link::ViewThread, thread->rootId()});
            }
        }
        else if (auto replyNameIt = tags.find("reply-parent-display-name");
                 replyNameIt != tags.end())
        {
            const auto targetText = parseTagString(replyNameIt->toString());
            const auto body = parseTagString(
                tags.value("reply-parent-msg-body").toString());
            if (!targetText.isEmpty())
            {
                builder.emplace<TextElement>(" -> ",
                                             MessageElementFlag::RepliedMessage,
                                             MessageColor::System,
                                             FontStyle::ChatMediumSmall);
                builder.emplace<TextElement>(targetText,
                                             MessageElementFlag::RepliedMessage,
                                             MessageColor::Text,
                                             FontStyle::ChatMediumSmall);
                if (!body.isEmpty())
                {
                    builder.emplace<TextElement>(
                        ": ", MessageElementFlag::RepliedMessage,
                        MessageColor::System, FontStyle::ChatMediumSmall);
                    builder.emplace<SingleLineTextElement>(
                        body,
                        MessageElementFlags(
                            {MessageElementFlag::RepliedMessage,
                             MessageElementFlag::Text}),
                        MessageColor::Text, FontStyle::ChatMediumSmall);
                }
            }
        }
    }

    if (!compactHeaderLayout)
    {
        builder.appendUsername(tags, args);
    }

    if (compactAuthorMode && !args.isAction &&
        tags.value("msg-id") != "announcement")
    {
        appendOpenEmoteCompactReplyButton(&builder, thread);
    }
    if (compactAuthorMode)
    {
        appendOpenEmoteIdentityRailSpacer(&builder, compactIdentityMetrics);
    }

    TextState textState{.twitchChannel = twitchChannel};
    QString bits;

    auto iterator = tags.find("bits");
    if (iterator != tags.end())
    {
        textState.hasBits = true;
        textState.bitsLeft = iterator.value().toInt();
        bits = iterator.value().toString();
    }

    // Twitch emotes
    auto twitchEmotes =
        parseTwitchEmotes(tags, content, static_cast<int>(messageOffset));

    // This runs through all ignored phrases and runs its replacements on content
    processIgnorePhrases(*getSettings()->ignoredMessages.readOnly(), content,
                         twitchEmotes);

    std::ranges::sort(twitchEmotes, [](const auto &a, const auto &b) {
        return a.start < b.start;
    });
    auto uniqueEmotes = std::ranges::unique(
        twitchEmotes, [](const auto &first, const auto &second) {
            return first.start == second.start;
        });
    twitchEmotes.erase(uniqueEmotes.begin(), uniqueEmotes.end());

    // words
    QStringList splits = content.split(' ');

    builder.addWords(splits, twitchEmotes, textState);

    QString stylizedUsername =
        stylizeUsername(builder->loginName, builder.message());

    builder->messageText = content;
    builder->searchText = stylizedUsername + " " + builder->localizedName +
                          " " + builder->loginName + ": " + content + " " +
                          builder->searchText;

    // highlights
    HighlightAlert highlight = builder.parseHighlights(tags, content, args);
    if (tags.contains("historical"))
    {
        highlight.playSound = false;
        highlight.windowAlert = false;
    }

    // highlighting incoming whispers if requested per setting
    if (args.isReceivedWhisper && getSettings()->highlightInlineWhispers)
    {
        builder->flags.set(MessageFlag::HighlightedWhisper);
        builder->highlightColor =
            ColorProvider::instance().color(ColorType::Whisper);
    }

    if (!args.isReceivedWhisper && tags.value("msg-id") != "announcement")
    {
        if (!compactAuthorMode && !compactHeaderLayout && thread)
        {
            if (!getSettings()->openEmoteBotCompatibilityMode.getValue() &&
                getSettings()->openEmoteShowThreadActivityIndicator)
            {
                const auto replies = thread->liveCount();
                if (replies > 0)
                {
                    builder
                        .emplace<TextElement>(u"•"_s,
                                              MessageElementFlag::ReplyButton,
                                              MessageColor::System,
                                              FontStyle::ChatMediumBold)
                        ->setLink({Link::ViewThread, thread->rootId()})
                        ->setTooltip(QString::number(replies));
                }
            }

            auto &img = getResources().buttons.replyThreadDark;
            builder
                .emplace<CircularImageElement>(
                    Image::fromResourcePixmap(img, 0.15), 2, Qt::gray,
                    MessageElementFlag::ReplyButton)
                ->setLink({Link::ViewThread, thread->rootId()});
        }
        else if (!compactAuthorMode && !compactHeaderLayout)
        {
            auto &img = getResources().buttons.replyDark;
            builder
                .emplace<CircularImageElement>(
                    Image::fromResourcePixmap(img, 0.15), 2, Qt::gray,
                    MessageElementFlag::ReplyButton)
                ->setLink({Link::ReplyToMessage, builder.message().id});
        }
    }

    // Keep timestamp on the right side of the author/reply header section.
    if (shouldRenderOpenEmoteTimestamp(channel, builder.message(),
                                       builder->serverReceivedTime))
    {
        builder.emplace<TimestampElement>(builder->serverReceivedTime.time());
    }

    return {builder.release(), highlight};
}

void MessageBuilder::addEmoji(const EmotePtr &emote)
{
    this->emplace<EmoteElement>(emote, MessageElementFlag::EmojiAll);
}

void MessageBuilder::addTextOrEmote(TextState &state, QString string)
{
    if (state.hasBits && this->tryAppendCheermote(state, string))
    {
        // This string was parsed as a cheermote
        return;
    }

    // TODO: Implement ignored emotes
    // Format of ignored emotes:
    // Emote name: "forsenPuke" - if string in ignoredEmotes
    // Will match emote regardless of source (i.e. bttv, ffz)
    // Emote source + name: "bttv:nyanPls"
    if (this->tryAppendEmote(state.twitchChannel, {string}))
    {
        // Successfully appended an emote
        return;
    }

    // Actually just text
    auto link = linkparser::parse(string);
    auto textColor = this->textColor_;

    if (link)
    {
        this->addLink(*link, string);
        return;
    }

    if (string.startsWith('@'))
    {
        auto match = mentionRegex.match(string);
        // Only treat as @mention if valid username
        if (match.hasMatch())
        {
            QString username = match.captured(1);
            auto originalTextColor = textColor;

            if (state.twitchChannel != nullptr)
            {
                if (auto userColor =
                        state.twitchChannel->getUserColor(username);
                    userColor.isValid())
                {
                    textColor = userColor;
                }
            }

            auto prefixedUsername = '@' + username;
            auto remainder = string.remove(prefixedUsername);
            this->emplace<MentionElement>(prefixedUsername, username,
                                          originalTextColor, textColor)
                ->setTrailingSpace(remainder.isEmpty());

            if (!remainder.isEmpty())
            {
                this->emplace<TextElement>(remainder, MessageElementFlag::Text,
                                           originalTextColor);
            }

            return;
        }
    }

    if (state.twitchChannel != nullptr && getSettings()->findAllUsernames)
    {
        auto match = allUsernamesMentionRegex.match(string);
        QString username = match.captured(1);

        if (match.hasMatch() &&
            state.twitchChannel->accessChatters()->contains(username))
        {
            auto originalTextColor = textColor;

            if (auto userColor = state.twitchChannel->getUserColor(username);
                userColor.isValid())
            {
                textColor = userColor;
            }

            auto remainder = string.remove(username);
            this->emplace<MentionElement>(username, username, originalTextColor,
                                          textColor)
                ->setTrailingSpace(remainder.isEmpty());

            if (!remainder.isEmpty())
            {
                this->emplace<TextElement>(remainder, MessageElementFlag::Text,
                                           originalTextColor);
            }

            return;
        }
    }

    this->appendOrEmplaceText(string, textColor);
}

bool MessageBuilder::isEmpty() const
{
    return this->message_->elements.empty();
}

MessageElement &MessageBuilder::back()
{
    assert(!this->isEmpty());
    return *this->message().elements.back();
}

std::unique_ptr<MessageElement> MessageBuilder::releaseBack()
{
    assert(!this->isEmpty());

    auto ptr = std::move(this->message().elements.back());
    this->message().elements.pop_back();
    return ptr;
}

TextElement *MessageBuilder::emplaceSystemTextAndUpdate(const QString &text,
                                                        QString &toUpdate)
{
    toUpdate.append(text + " ");
    return this->emplace<TextElement>(text, MessageElementFlag::Text,
                                      MessageColor::System);
}

void MessageBuilder::parseUsernameColor(const QVariantMap &tags,
                                        const QString &userID)
{
    const auto *userData = getApp()->getUserData();
    assert(userData != nullptr);

    if (const auto &user = userData->getUser(userID))
    {
        if (user->color)
        {
            this->usernameColor_ = user->color.value();
            this->message().usernameColor = this->usernameColor_;
            return;
        }
    }

    const auto iterator = tags.find("color");
    if (iterator != tags.end())
    {
        if (const auto color = iterator.value().toString(); !color.isEmpty())
        {
            this->usernameColor_ = QColor(color);
            this->message().usernameColor = this->usernameColor_;
            return;
        }
    }

    if (getSettings()->colorizeNicknames && tags.contains("user-id"))
    {
        this->usernameColor_ = getRandomColor(tags.value("user-id").toString());
        this->message().usernameColor = this->usernameColor_;
    }
}

void MessageBuilder::parseUsername(const Communi::IrcMessage *ircMessage,
                                   TwitchChannel *twitchChannel,
                                   bool trimSubscriberUsername)
{
    // username
    auto userName = ircMessage->nick();

    if (userName.isEmpty() || trimSubscriberUsername)
    {
        userName = ircMessage->tag("login").toString();
    }

    this->message_->loginName = userName;
    if (twitchChannel != nullptr)
    {
        twitchChannel->setUserColor(userName, this->message_->usernameColor);
    }

    // Update current user color if this is our message
    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    if (ircMessage->nick() == currentUser->getUserName())
    {
        currentUser->setColor(this->message_->usernameColor);
    }
}

void MessageBuilder::parseMessageID(const QVariantMap &tags)
{
    auto iterator = tags.find("id");

    if (iterator != tags.end())
    {
        this->message().id = iterator.value().toString();
    }
}

QString MessageBuilder::parseRoomID(const QVariantMap &tags,
                                    TwitchChannel *twitchChannel)
{
    if (twitchChannel == nullptr)
    {
        return {};
    }

    auto iterator = tags.find("room-id");

    if (iterator != std::end(tags))
    {
        auto roomID = iterator->toString();
        if (twitchChannel->roomId() != roomID)
        {
            if (twitchChannel->roomId().isEmpty())
            {
                twitchChannel->setRoomId(roomID);
            }
            else
            {
                qCWarning(chatterinoTwitch)
                    << "The room-ID of the received message doesn't match the "
                       "room-ID of the channel - received:"
                    << roomID << "channel:" << twitchChannel->roomId();
            }
        }
        return roomID;
    }

    return {};
}

TwitchChannel *MessageBuilder::parseSharedChatInfo(const QVariantMap &tags,
                                                   TwitchChannel *twitchChannel)
{
    if (!twitchChannel)
    {
        return twitchChannel;
    }

    if (auto it = tags.find("source-room-id"); it != tags.end())
    {
        auto sourceRoom = it.value().toString();
        if (twitchChannel->roomId() != sourceRoom)
        {
            this->message().flags.set(MessageFlag::SharedMessage);

            auto sourceChan =
                getApp()->getTwitch()->getChannelOrEmptyByID(sourceRoom);
            if (sourceChan && !sourceChan->isEmpty())
            {
                // avoid duplicate pings
                this->message().flags.set(
                    MessageFlag::DoNotTriggerNotification);

                auto *chan = dynamic_cast<TwitchChannel *>(sourceChan.get());
                if (chan)
                {
                    return chan;
                }
            }
        }
    }
    return twitchChannel;
}

void MessageBuilder::parseThread(const QString &messageContent,
                                 const QVariantMap &tags,
                                 const Channel *channel,
                                 const std::shared_ptr<MessageThread> &thread,
                                 const MessagePtr &parent)
{
    const bool compactHeaderLayout =
        getSettings()->openEmoteCompactHeaderLayout.getValue();

    if (thread)
    {
        // set references
        this->message().replyThread = thread;
        this->message().replyParent = parent;
        thread->addToThread(std::weak_ptr{this->message_});

        if (thread->subscribed())
        {
            this->message().flags.set(MessageFlag::SubscribedThread);
        }

        // enable reply flag
        this->message().flags.set(MessageFlag::ReplyMessage);

        if (compactHeaderLayout)
        {
            return;
        }

        if (!getSettings()->openEmoteBotCompatibilityMode.getValue() &&
            getSettings()->openEmoteCompactAuthorAvatar && false)
        {
            appendOpenEmoteAuthorAvatarElement(
                this, tags,
                MessageElementFlags({MessageElementFlag::RepliedMessage,
                                     MessageElementFlag::Username}),
                26.F, false);
        }

        MessagePtr threadRoot;
        if (!parent)
        {
            threadRoot = thread->root();
        }
        else
        {
            threadRoot = parent;
        }

        QString usernameText =
            stylizeUsername(threadRoot->loginName, *threadRoot);

        this->emplace<ReplyCurveElement>();

        // construct reply elements
        this->emplace<TextElement>(
                "Replying to", MessageElementFlag::RepliedMessage,
                MessageColor::System, FontStyle::ChatMediumSmall)
            ->setLink({Link::ViewThread, thread->rootId()});

        this->emplace<TextElement>(
                "@" + usernameText +
                    (threadRoot->flags.has(MessageFlag::Action) ? "" : ":"),
                MessageElementFlag::RepliedMessage, threadRoot->usernameColor,
                FontStyle::ChatMediumSmall)
            ->setLink({Link::UserInfo, threadRoot->displayName});

        MessageColor color = MessageColor::Text;
        if (threadRoot->flags.has(MessageFlag::Action))
        {
            color = threadRoot->usernameColor;
        }
        this->emplace<SingleLineTextElement>(
                threadRoot->messageText,
                MessageElementFlags({MessageElementFlag::RepliedMessage,
                                     MessageElementFlag::Text}),
                color, FontStyle::ChatMediumSmall)
            ->setLink({Link::ViewThread, thread->rootId()});
    }
    else if (tags.find("reply-parent-msg-id") != tags.end())
    {
        if (compactHeaderLayout)
        {
            return;
        }

        // Message is a reply but we couldn't find the original message.
        // Render the message using the additional reply tags

        auto replyDisplayName = tags.find("reply-parent-display-name");
        auto replyBody = tags.find("reply-parent-msg-body");

        if (replyDisplayName != tags.end() && replyBody != tags.end())
        {
            QString body;

            this->emplace<ReplyCurveElement>();
            this->emplace<TextElement>(
                "Replying to", MessageElementFlag::RepliedMessage,
                MessageColor::System, FontStyle::ChatMediumSmall);

            bool ignored = MessageBuilder::isIgnored(
                messageContent, tags.value("reply-parent-user-id").toString(),
                channel);
            if (ignored)
            {
                body = QString("[Blocked user]");
            }
            else
            {
                auto name = replyDisplayName->toString();
                body = parseTagString(replyBody->toString());

                this->emplace<TextElement>(
                        "@" + name + ":", MessageElementFlag::RepliedMessage,
                        this->textColor_, FontStyle::ChatMediumSmall)
                    ->setLink({Link::UserInfo, name});
            }

            this->emplace<SingleLineTextElement>(
                body,
                MessageElementFlags({MessageElementFlag::RepliedMessage,
                                     MessageElementFlag::Text}),
                this->textColor_, FontStyle::ChatMediumSmall);
        }
    }
}

HighlightAlert MessageBuilder::parseHighlights(const QVariantMap &tags,
                                               const QString &originalMessage,
                                               const MessageParseArgs &args)
{
    if (getSettings()->isBlacklistedUser(this->message().loginName))
    {
        // Do nothing. We ignore highlights from this user.
        return {};
    }

    auto badges = parseBadgeTag(tags);
    auto [highlighted, highlightResult] = getApp()->getHighlights()->check(
        args, badges, this->message().loginName, originalMessage,
        this->message().flags);

    if (!highlighted)
    {
        return {};
    }

    // This message triggered one or more highlights, act upon the highlight result

    this->message().flags.set(MessageFlag::Highlighted);

    this->message().highlightColor = highlightResult.color;

    if (highlightResult.showInMentions)
    {
        this->message().flags.set(MessageFlag::ShowInMentions);
    }

    auto customSound = [&] {
        if (highlightResult.customSoundUrl)
        {
            return *highlightResult.customSoundUrl;
        }
        return QUrl{};
    }();
    return {
        .customSound = customSound,
        .playSound = highlightResult.playSound,
        .windowAlert = highlightResult.alert,
    };
}

void MessageBuilder::appendChannelName(const Channel *channel)
{
    QString channelName("#" + channel->getName());
    Link link(Link::JumpToChannel, channel->getName());

    this->emplace<TextElement>(channelName, MessageElementFlag::ChannelName,
                               MessageColor::System)
        ->setLink(link);
}

void MessageBuilder::appendUsername(const QVariantMap &tags,
                                    const MessageParseArgs &args)
{
    auto *app = getApp();

    QString username = this->message_->loginName;
    QString localizedName;

    auto iterator = tags.find("display-name");
    if (iterator != tags.end())
    {
        QString displayName =
            parseTagString(iterator.value().toString()).trimmed();

        if (QString::compare(displayName, username, Qt::CaseInsensitive) == 0)
        {
            username = displayName;

            this->message().displayName = displayName;
        }
        else
        {
            localizedName = displayName;

            this->message().displayName = username;
            this->message().localizedName = displayName;
        }
    }

    QString usernameText = stylizeUsername(username, this->message());

    const bool compactAvatarMode =
        !getSettings()->openEmoteBotCompatibilityMode.getValue() &&
        getSettings()->openEmoteCompactAuthorAvatar && !args.isSentWhisper &&
        false &&
        !args.isReceivedWhisper;
    const bool keepVisibleNames = getSettings()->openEmoteCompactAvatarKeepNames;
    if (compactAvatarMode)
    {
        bool avatarRendered = false;
        const bool avatarHandledInReplyContext =
            this->message().flags.has(MessageFlag::ReplyMessage);
        if (!avatarHandledInReplyContext)
        {
            avatarRendered = appendOpenEmoteAuthorAvatarElement(
                this, tags, MessageElementFlag::Username, 18.F, true);
            if (avatarRendered && !keepVisibleNames)
            {
                return;
            }
        }
        else
        {
            if (!getSettings()->openEmoteBotCompatibilityMode.getValue() &&
                getSettings()->openEmoteAvatarDecorators)
            {
                appendOpenEmoteAvatarDecorators(this, tags);
                avatarRendered = true;
            }
            if (avatarRendered && !keepVisibleNames)
            {
                return;
            }
        }
    }

    if (args.isSentWhisper)
    {
        // TODO(pajlada): Re-implement
        // userDisplayString +=
        // IrcManager::instance().getUser().getUserName();
    }
    else if (args.isReceivedWhisper)
    {
        // Sender username
        this->emplace<TextElement>(usernameText, MessageElementFlag::Username,
                                   this->usernameColor_,
                                   FontStyle::ChatMediumBold)
            ->setLink({Link::UserWhisper, this->message().displayName});

        auto currentUser = app->getAccounts()->twitch.getCurrent();

        // Separator
        this->emplace<TextElement>("->", MessageElementFlag::Username,
                                   MessageColor::System, FontStyle::ChatMedium);

        QColor selfColor = currentUser->color();
        MessageColor selfMsgColor =
            selfColor.isValid() ? selfColor : MessageColor::System;

        // Your own username
        this->emplace<TextElement>(currentUser->getUserName() + ":",
                                   MessageElementFlag::Username, selfMsgColor,
                                   FontStyle::ChatMediumBold);
    }
    else
    {
        if (!args.isAction)
        {
            usernameText += ":";
        }

        this->emplace<TextElement>(usernameText, MessageElementFlag::Username,
                                   this->usernameColor_,
                                   FontStyle::ChatMediumBold)
            ->setLink({Link::UserInfo, this->message().displayName});
    }
}

Outcome MessageBuilder::tryAppendEmote(TwitchChannel *twitchChannel,
                                       const EmoteName &name)
{
    auto emote = parseEmote(twitchChannel, name);
    const auto emoteScaleMultiplier =
        openEmoteChannelScaleForChannel(twitchChannel);

    if (!emote)
    {
        return Failure;
    }

    if (emote->zeroWidth && getSettings()->enableZeroWidthEmotes &&
        !this->isEmpty())
    {
        // Attempt to merge current zero-width emote into any previous emotes
        auto *asEmote = dynamic_cast<EmoteElement *>(&this->back());
        if (asEmote)
        {
            // Make sure to access asEmote before taking ownership when releasing
            auto baseEmote = asEmote->getEmote();
            // Need to remove EmoteElement and replace with LayeredEmoteElement
            auto baseEmoteElement = this->releaseBack();

            std::vector<LayeredEmoteElement::Emote> layers = {
                {baseEmote, baseEmoteElement->getFlags()},
                {emote, MessageElementFlag::Emote},
            };
            this->emplace<LayeredEmoteElement>(
                std::move(layers),
                baseEmoteElement->getFlags() | MessageElementFlag::Emote,
                this->textColor_, emoteScaleMultiplier);
            return Success;
        }

        auto *asLayered = dynamic_cast<LayeredEmoteElement *>(&this->back());
        if (asLayered)
        {
            asLayered->addEmoteLayer({emote, MessageElementFlag::Emote});
            asLayered->addFlags(MessageElementFlag::Emote);
            return Success;
        }

        // No emote to merge with, just show as regular emote
    }

    this->emplace<EmoteElement>(emote, MessageElementFlag::Emote,
                                this->textColor_, emoteScaleMultiplier);
    return Success;
}

void MessageBuilder::addWords(
    const QStringList &words,
    const std::vector<TwitchEmoteOccurrence> &twitchEmotes, TextState &state)
{
    // cursor currently indicates what character index we're currently operating in the full list of words
    int cursor = 0;
    auto currentTwitchEmoteIt = twitchEmotes.begin();
    const auto emoteScaleMultiplier =
        openEmoteChannelScaleForChannel(state.twitchChannel);

    for (auto word : words)
    {
        if (word.isEmpty())
        {
            cursor++;
            continue;
        }

        while (doesWordContainATwitchEmote(cursor, word, twitchEmotes,
                                           currentTwitchEmoteIt))
        {
            const auto &currentTwitchEmote = *currentTwitchEmoteIt;

            if (currentTwitchEmote.start == cursor)
            {
                // This emote exists right at the start of the word!
                this->emplace<EmoteElement>(currentTwitchEmote.ptr,
                                            MessageElementFlag::Emote,
                                            this->textColor_,
                                            emoteScaleMultiplier);

                auto len = currentTwitchEmote.name.string.length();
                cursor += len;
                word = word.mid(len);

                ++currentTwitchEmoteIt;

                if (word.isEmpty())
                {
                    // space
                    cursor += 1;
                    break;
                }
                else
                {
                    this->message().elements.back()->setTrailingSpace(false);
                }

                continue;
            }

            // Emote is not at the start

            // 1. Add text before the emote
            QString preText = word.left(currentTwitchEmote.start - cursor);
            for (auto variant :
                 getApp()->getEmotes()->getEmojis()->parse(preText))
            {
                std::visit(variant::Overloaded{
                               [&](const EmotePtr &emote) {
                                   this->addEmoji(emote);
                               },
                               [&](QStringView text) {
                                   this->addTextOrEmote(state, text.toString());
                               },
                           },
                           variant);
            }

            cursor += preText.size();

            word = word.mid(preText.size());
        }

        if (word.isEmpty())
        {
            continue;
        }

        // split words
        for (auto variant : getApp()->getEmotes()->getEmojis()->parse(word))
        {
            std::visit(variant::Overloaded{
                           [&](const EmotePtr &emote) {
                               this->addEmoji(emote);
                           },
                           [&](QStringView text) {
                               this->addTextOrEmote(state, text.toString());
                           },
                       },
                       variant);
        }

        cursor += word.size() + 1;
    }
}

void MessageBuilder::appendTwitchBadges(const QVariantMap &tags,
                                        TwitchChannel *twitchChannel)
{
    if (twitchChannel == nullptr)
    {
        return;
    }

    auto badges = parseBadgeTag(tags);

    if (this->message().flags.has(MessageFlag::SharedMessage))
    {
        const QString sourceId = tags["source-room-id"].toString();
        QString sourceName;
        QString sourceProfilePicture;
        QString sourceLogin;

        if (sourceId.isEmpty())
        {
            sourceName = "";
        }
        else
        {
            auto twitchUser = getApp()->getTwitchUsers()->resolveID({sourceId});
            sourceProfilePicture = twitchUser->profilePictureUrl;
            sourceLogin = twitchUser->name;

            if (twitchChannel->roomId() == sourceId)
            {
                // We have the source channel open, but we still need to load the profile picture URL
                sourceName = twitchChannel->getName();
            }
            else
            {
                sourceName = twitchUser->displayName;
            }
        }

        this->emplace<BadgeElement>(
            makeSharedChatBadge(sourceName, sourceProfilePicture, sourceLogin),
            MessageElementFlag::BadgeSharedChannel);

        const auto sourceBadges = parseBadgeTag(tags, "source-badges");
        const auto appendedBadges = appendSharedChatBadges(
            this, sourceBadges, sourceName, twitchChannel);

        // Dedup mod/vip badges if user is mod/vip in both chats,
        // preferring source channel's badges for the tooltips
        for (const auto &appendedBadge : appendedBadges)
        {
            if (auto b = std::ranges::find(badges, appendedBadge);
                b != badges.end())
            {
                badges.erase(b);
            }
        }
    }

    auto badgeInfos = parseBadgeInfoTag(tags);
    appendBadges(this, badges, badgeInfos, twitchChannel);
}

void MessageBuilder::appendChatterinoBadges(const QString &userID)
{
    if (auto badge = getApp()->getChatterinoBadges()->getBadge({userID}))
    {
        this->emplace<BadgeElement>(*badge,
                                    MessageElementFlag::BadgeChatterino);

        /// e.g. "chatterino:Chatterino Top donator"
        this->message().externalBadges.emplace_back((*badge)->name.string);
    }
}

void MessageBuilder::appendFfzBadges(TwitchChannel *twitchChannel,
                                     const QString &userID)
{
    for (const auto &badge : getApp()->getFfzBadges()->getUserBadges({userID}))
    {
        this->emplace<FfzBadgeElement>(
            badge.emote, MessageElementFlag::BadgeFfz, badge.color);

        /// e.g. "frankerfacez:subwoofer"
        this->message().externalBadges.emplace_back(badge.emote->name.string);
    }

    if (twitchChannel == nullptr)
    {
        return;
    }

    for (const auto &badge : twitchChannel->ffzChannelBadges(userID))
    {
        this->emplace<FfzBadgeElement>(
            badge.emote, MessageElementFlag::BadgeFfz, badge.color);

        /// e.g. "frankerfacez:subwoofer"
        this->message().externalBadges.emplace_back(badge.emote->name.string);
    }
}

void MessageBuilder::appendBttvBadges(const QString &userID)
{
    if (auto badge = getApp()->getBttvBadges()->getBadge({userID}))
    {
        this->emplace<BadgeElement>(*badge, MessageElementFlag::BadgeBttv);

        /// e.g. "betterttv:Pro Subscriber"
        this->message().externalBadges.emplace_back((*badge)->name.string);
    }
}

void MessageBuilder::appendSeventvBadges(const QString &userID)
{
    if (auto badge = getApp()->getSeventvBadges()->getBadge({userID}))
    {
        this->emplace<BadgeElement>(*badge, MessageElementFlag::BadgeSevenTV);

        /// e.g. "7tv:NNYS 2024"
        this->message().externalBadges.emplace_back((*badge)->name.string);
    }
}

Outcome MessageBuilder::tryAppendCheermote(TextState &state,
                                           const QString &string)
{
    if (state.bitsLeft == 0)
    {
        return Failure;
    }

    auto cheerOpt = state.twitchChannel->cheerEmote(string);

    if (!cheerOpt)
    {
        return Failure;
    }

    auto &cheerEmote = *cheerOpt;
    auto match = cheerEmote.regex.match(string);

    if (!match.hasMatch())
    {
        return Failure;
    }

    int cheerValue = match.captured(1).toInt();

    if (getSettings()->stackBits)
    {
        if (state.bitsStacked)
        {
            return Success;
        }
        const auto emoteScaleMultiplier =
            openEmoteChannelScaleForChannel(state.twitchChannel);
        if (cheerEmote.staticEmote)
        {
            this->emplace<EmoteElement>(cheerEmote.staticEmote,
                                        MessageElementFlag::BitsStatic,
                                        this->textColor_,
                                        emoteScaleMultiplier);
        }
        if (cheerEmote.animatedEmote)
        {
            this->emplace<EmoteElement>(cheerEmote.animatedEmote,
                                        MessageElementFlag::BitsAnimated,
                                        this->textColor_,
                                        emoteScaleMultiplier);
        }
        if (cheerEmote.color != QColor())
        {
            this->emplace<TextElement>(QString::number(state.bitsLeft),
                                       MessageElementFlag::BitsAmount,
                                       cheerEmote.color);
        }
        state.bitsStacked = true;
        return Success;
    }

    if (state.bitsLeft >= cheerValue)
    {
        state.bitsLeft -= cheerValue;
    }
    else
    {
        QString newString = string;
        newString.chop(QString::number(cheerValue).length());
        newString += QString::number(cheerValue - state.bitsLeft);

        return this->tryAppendCheermote(state, newString);
    }

    const auto emoteScaleMultiplier =
        openEmoteChannelScaleForChannel(state.twitchChannel);
    if (cheerEmote.staticEmote)
    {
        this->emplace<EmoteElement>(cheerEmote.staticEmote,
                                    MessageElementFlag::BitsStatic,
                                    this->textColor_, emoteScaleMultiplier);
    }
    if (cheerEmote.animatedEmote)
    {
        this->emplace<EmoteElement>(cheerEmote.animatedEmote,
                                    MessageElementFlag::BitsAnimated,
                                    this->textColor_, emoteScaleMultiplier);
    }
    if (cheerEmote.color != QColor())
    {
        this->emplace<TextElement>(match.captured(1),
                                   MessageElementFlag::BitsAmount,
                                   cheerEmote.color);
    }

    return Success;
}

}  // namespace chatterino
