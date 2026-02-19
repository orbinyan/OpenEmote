// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/commands/builtin/twitch/SendWhisper.hpp"

#include "Application.hpp"
#include "common/Credentials.hpp"
#include "common/LinkParser.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/commands/CommandContext.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageElement.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/emoji/Emojis.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/twitch/api/Helix.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "singletons/StreamerMode.hpp"
#include "singletons/Theme.hpp"
#include "util/OpenEmoteSecureGroupWhisper.hpp"
#include "util/Twitch.hpp"

#include <QApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

using namespace chatterino;
using namespace chatterino::openemote::groupwhisper;

QString formatWhisperError(HelixWhisperError error, const QString &message)
{
    using Error = HelixWhisperError;

    QString errorMessage = "Failed to send whisper - ";

    switch (error)
    {
        case Error::NoVerifiedPhone: {
            errorMessage += "Due to Twitch restrictions, you are now "
                            "required to have a verified phone number "
                            "to send whispers. You can add a phone "
                            "number in Twitch settings. "
                            "https://www.twitch.tv/settings/security";
        };
        break;

        case Error::RecipientBlockedUser: {
            errorMessage += "The recipient doesn't allow whispers "
                            "from strangers or you directly.";
        };
        break;

        case Error::WhisperSelf: {
            errorMessage += "You cannot whisper yourself.";
        };
        break;

        case Error::Forwarded: {
            errorMessage += message;
        }
        break;

        case Error::Ratelimited: {
            errorMessage += "You may only whisper a maximum of 40 "
                            "unique recipients per day. Within the "
                            "per day limit, you may whisper a "
                            "maximum of 3 whispers per second and "
                            "a maximum of 100 whispers per minute.";
        }
        break;

        case Error::UserMissingScope: {
            // TODO(pajlada): Phrase MISSING_REQUIRED_SCOPE
            errorMessage += "Missing required scope. "
                            "Re-login with your "
                            "account and try again.";
        }
        break;

        case Error::UserNotAuthorized: {
            // TODO(pajlada): Phrase MISSING_PERMISSION
            errorMessage += "You don't have permission to "
                            "perform that action.";
        }
        break;

        case Error::Unknown: {
            errorMessage += "An unknown error has occurred.";
        }
        break;
    }

    return errorMessage;
}

bool appendWhisperMessageWordsLocally(const QStringList &words)
{
    auto *app = getApp();

    MessageBuilder b;

    b.emplace<TimestampElement>();
    b.emplace<TextElement>(
        app->getAccounts()->twitch.getCurrent()->getUserName(),
        MessageElementFlag::Text, MessageColor::Text,
        FontStyle::ChatMediumBold);
    b.emplace<TextElement>("->", MessageElementFlag::Text,
                           getApp()->getThemes()->messages.textColors.system);
    b.emplace<TextElement>(words[1] + ":", MessageElementFlag::Text,
                           MessageColor::Text, FontStyle::ChatMediumBold);

    const auto &acc = app->getAccounts()->twitch.getCurrent();
    const auto &accemotes = *acc->accessEmotes();
    const auto *bttvemotes = app->getBttvEmotes();
    const auto *ffzemotes = app->getFfzEmotes();
    auto emote = std::optional<EmotePtr>{};
    for (int i = 2; i < words.length(); i++)
    {
        {  // Twitch emote
            auto it = accemotes->find({words[i]});
            if (it != accemotes->end())
            {
                b.emplace<EmoteElement>(it->second, MessageElementFlag::Emote);
                continue;
            }
        }  // Twitch emote

        {  // bttv/ffz emote
            emote = bttvemotes->emote({words[i]});
            if (!emote)
            {
                emote = ffzemotes->emote({words[i]});
            }
            // TODO: Load 7tv global emotes
            if (emote)
            {
                b.emplace<EmoteElement>(*emote, MessageElementFlag::Emote);
                continue;
            }
        }  // bttv/ffz emote
        {  // emoji/text
            for (auto &variant : app->getEmotes()->getEmojis()->parse(words[i]))
            {
                constexpr const static struct {
                    void operator()(EmotePtr emote, MessageBuilder &b) const
                    {
                        b.emplace<EmoteElement>(emote,
                                                MessageElementFlag::EmojiAll);
                    }
                    void operator()(QStringView string, MessageBuilder &b) const
                    {
                        auto link = linkparser::parse(string);
                        if (link)
                        {
                            b.addLink(*link, string);
                        }
                        else
                        {
                            b.emplace<TextElement>(string.toString(),
                                                   MessageElementFlag::Text);
                        }
                    }
                } visitor;
                std::visit(
                    [&b](auto &&arg) {
                        visitor(arg, b);
                    },
                    variant);
            }  // emoji/text
        }
    }

    b->flags.set(MessageFlag::DoNotTriggerNotification);
    b->flags.set(MessageFlag::Whisper);
    auto messagexD = b.release();

    getApp()->getTwitch()->getWhispersChannel()->addMessage(
        messagexD, MessageContext::Original);

    if (getSettings()->inlineWhispers &&
        !(getSettings()->streamerModeSuppressInlineWhispers &&
          getApp()->getStreamerMode()->isEnabled()))
    {
        app->getTwitch()->forEachChannel([&messagexD](ChannelPtr _channel) {
            _channel->addMessage(messagexD, MessageContext::Repost);
        });
    }

    return true;
}

struct GroupDefinition {
    QString name;
    QString channel;
    QStringList members;
};

using GroupMap = QMap<QString, GroupDefinition>;

QString normalizeMember(QStringView input)
{
    auto value = input.toString().trimmed().toLower();
    if (value.startsWith('@'))
    {
        value.remove(0, 1);
    }

    stripChannelName(value);

    return value;
}

GroupMap loadGroupDefinitions()
{
    GroupMap out;
    const auto encoded = getSettings()->openEmoteSecureGroupDefinitions.getValue();
    const auto parsed = QJsonDocument::fromJson(encoded.toUtf8());
    if (!parsed.isObject())
    {
        return out;
    }

    const auto root = parsed.object();
    for (auto it = root.begin(); it != root.end(); ++it)
    {
        if (!it.value().isObject())
        {
            continue;
        }

        GroupDefinition def;
        def.name = normalizeGroupName(it.key());
        if (def.name.isEmpty())
        {
            continue;
        }

        auto obj = it.value().toObject();
        def.channel = obj.value("channel").toString().trimmed().toLower();
        if (def.channel.startsWith('#'))
        {
            def.channel.remove(0, 1);
        }

        if (auto members = obj.value("members"); members.isArray())
        {
            for (const auto &v : members.toArray())
            {
                auto normalized = normalizeMember(v.toString());
                if (!normalized.isEmpty() && !def.members.contains(normalized))
                {
                    def.members.push_back(normalized);
                }
            }
        }

        out.insert(def.name, def);
    }

    return out;
}

void saveGroupDefinitions(const GroupMap &groups)
{
    QJsonObject root;
    for (auto it = groups.begin(); it != groups.end(); ++it)
    {
        QJsonObject obj;
        obj.insert("channel", it->channel);

        QJsonArray members;
        for (const auto &member : it->members)
        {
            members.append(member);
        }
        obj.insert("members", members);
        root.insert(it.key(), obj);
    }

    const auto compact = QJsonDocument(root).toJson(QJsonDocument::Compact);
    getSettings()->openEmoteSecureGroupDefinitions.setValue(
        QString::fromUtf8(compact));
}

void sendOneGroupWhisper(const ChannelPtr &feedbackChannel, QString target,
                         QString payload)
{
    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    getHelix()->getUserByName(
        target,
        [feedbackChannel, currentUser, payload](const auto &targetUser) {
            getHelix()->sendWhisper(
                currentUser->getUserId(), targetUser.id, payload, [] {},
                [feedbackChannel](auto error, auto message) {
                    auto errorMessage = formatWhisperError(error, message);
                    feedbackChannel->addSystemMessage(errorMessage);
                });
        },
        [feedbackChannel, target] {
            feedbackChannel->addSystemMessage(
                QStringLiteral("No user matching \"%1\".").arg(target));
        });
}

QString groupWhisperUsage()
{
    return QStringLiteral(
        "Usage: /gw create <group> <members_csv> <secret> | "
        "/gw send <group> <message> | /gw key <group> <secret> | "
        "/gw add <group> <member> | /gw remove <group> <member> | "
        "/gw delete <group> | /gw list");
}

}  // namespace

namespace chatterino::commands {

QString sendWhisper(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return "";
    }

    if (ctx.words.size() < 3)
    {
        ctx.channel->addSystemMessage("Usage: /w <username> <message>");
        return "";
    }

    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    if (currentUser->isAnon())
    {
        ctx.channel->addSystemMessage(
            "You must be logged in to send a whisper!");
        return "";
    }
    auto target = ctx.words.at(1);
    stripChannelName(target);
    auto message = ctx.words.mid(2).join(' ');
    if (ctx.channel->isTwitchChannel())
    {
        getHelix()->getUserByName(
            target,
            [channel{ctx.channel}, currentUser, target, message,
             words{ctx.words}](const auto &targetUser) {
                getHelix()->sendWhisper(
                    currentUser->getUserId(), targetUser.id, message,
                    [words] {
                        appendWhisperMessageWordsLocally(words);
                    },
                    [channel, target, targetUser](auto error, auto message) {
                        auto errorMessage = formatWhisperError(error, message);
                        channel->addSystemMessage(errorMessage);
                    });
            },
            [channel{ctx.channel}] {
                channel->addSystemMessage("No user matching that username.");
            });
        return "";
    }

    return "";
}

QString sendGroupWhisper(const CommandContext &ctx)
{
    if (ctx.channel == nullptr)
    {
        return {};
    }

    if (!getSettings()->openEmoteEnableSecureGroupWhispers)
    {
        ctx.channel->addSystemMessage(
            "Secure group whispers are disabled in settings.");
        return {};
    }

    if (ctx.words.size() < 2)
    {
        ctx.channel->addSystemMessage(groupWhisperUsage());
        return {};
    }

    auto currentUser = getApp()->getAccounts()->twitch.getCurrent();
    if (currentUser->isAnon())
    {
        ctx.channel->addSystemMessage(
            "You must be logged in to use group whispers.");
        return {};
    }

    auto groups = loadGroupDefinitions();
    const auto action = ctx.words.at(1).trimmed().toLower();

    if (action == "list")
    {
        if (groups.isEmpty())
        {
            ctx.channel->addSystemMessage("No secure groups configured.");
            return {};
        }

        for (auto it = groups.begin(); it != groups.end(); ++it)
        {
            ctx.channel->addSystemMessage(
                QStringLiteral("🔒 %1 [%2] members=%3")
                    .arg(it.key(), it->channel,
                         QString::number(it->members.size())));
        }
        return {};
    }

    if (action == "create")
    {
        if (ctx.words.size() < 5)
        {
            ctx.channel->addSystemMessage(
                "Usage: /gw create <group> <members_csv> <secret>");
            return {};
        }

        auto group = normalizeGroupName(ctx.words.at(2));
        if (group.isEmpty())
        {
            ctx.channel->addSystemMessage(
                "Invalid group name. Allowed: a-z 0-9 _ -");
            return {};
        }

        GroupDefinition def;
        def.name = group;
        def.channel =
            ctx.channel->isTwitchChannel() ? ctx.channel->getName().toLower()
                                           : QString();
        if (def.channel.startsWith('#'))
        {
            def.channel.remove(0, 1);
        }

        for (const auto &member :
             ctx.words.at(3).split(',', Qt::SkipEmptyParts))
        {
            auto normalized = normalizeMember(member);
            if (!normalized.isEmpty() && !def.members.contains(normalized))
            {
                def.members.push_back(normalized);
            }
        }

        auto self = currentUser->getUserName().trimmed().toLower();
        if (!self.isEmpty() && !def.members.contains(self))
        {
            def.members.push_back(self);
        }

        auto secret = ctx.words.mid(4).join(' ').trimmed();
        if (secret.isEmpty())
        {
            ctx.channel->addSystemMessage("Secret cannot be empty.");
            return {};
        }

        Credentials::instance().set("openemote", credentialNameForGroup(group),
                                    secret);
        groups[group] = def;
        saveGroupDefinitions(groups);

        ctx.channel->addSystemMessage(
            QStringLiteral("Created secure group \"%1\" with %2 member(s).")
                .arg(group, QString::number(def.members.size())));
        return {};
    }

    if (action == "key")
    {
        if (ctx.words.size() < 4)
        {
            ctx.channel->addSystemMessage("Usage: /gw key <group> <secret>");
            return {};
        }

        auto group = normalizeGroupName(ctx.words.at(2));
        if (group.isEmpty() || !groups.contains(group))
        {
            ctx.channel->addSystemMessage("Unknown group.");
            return {};
        }

        auto secret = ctx.words.mid(3).join(' ').trimmed();
        if (secret.isEmpty())
        {
            ctx.channel->addSystemMessage("Secret cannot be empty.");
            return {};
        }

        Credentials::instance().set("openemote", credentialNameForGroup(group),
                                    secret);
        ctx.channel->addSystemMessage(
            QStringLiteral("Updated secret for \"%1\".").arg(group));
        return {};
    }

    if (action == "add" || action == "remove")
    {
        if (ctx.words.size() < 4)
        {
            ctx.channel->addSystemMessage(
                QStringLiteral("Usage: /gw %1 <group> <member>").arg(action));
            return {};
        }

        auto group = normalizeGroupName(ctx.words.at(2));
        if (group.isEmpty() || !groups.contains(group))
        {
            ctx.channel->addSystemMessage("Unknown group.");
            return {};
        }

        auto member = normalizeMember(ctx.words.at(3));
        if (member.isEmpty())
        {
            ctx.channel->addSystemMessage("Invalid member.");
            return {};
        }

        auto &members = groups[group].members;
        if (action == "add")
        {
            if (!members.contains(member))
            {
                members.push_back(member);
            }
        }
        else
        {
            members.removeAll(member);
        }

        saveGroupDefinitions(groups);
        ctx.channel->addSystemMessage(
            QStringLiteral("%1 member \"%2\" in group \"%3\".")
                .arg(action == "add" ? "Updated" : "Removed", member, group));
        return {};
    }

    if (action == "delete")
    {
        if (ctx.words.size() < 3)
        {
            ctx.channel->addSystemMessage("Usage: /gw delete <group>");
            return {};
        }
        auto group = normalizeGroupName(ctx.words.at(2));
        if (group.isEmpty() || !groups.remove(group))
        {
            ctx.channel->addSystemMessage("Unknown group.");
            return {};
        }

        saveGroupDefinitions(groups);
        Credentials::instance().erase("openemote", credentialNameForGroup(group));
        ctx.channel->addSystemMessage(
            QStringLiteral("Deleted secure group \"%1\".").arg(group));
        return {};
    }

    if (action == "send")
    {
        if (ctx.words.size() < 4)
        {
            ctx.channel->addSystemMessage("Usage: /gw send <group> <message>");
            return {};
        }

        auto group = normalizeGroupName(ctx.words.at(2));
        if (group.isEmpty() || !groups.contains(group))
        {
            ctx.channel->addSystemMessage("Unknown group.");
            return {};
        }

        auto def = groups.value(group);
        auto anchorChannel = def.channel;
        if (anchorChannel.isEmpty() && ctx.channel->isTwitchChannel())
        {
            anchorChannel = ctx.channel->getName().toLower();
            if (anchorChannel.startsWith('#'))
            {
                anchorChannel.remove(0, 1);
            }
            groups[group].channel = anchorChannel;
            saveGroupDefinitions(groups);
        }
        if (anchorChannel.isEmpty())
        {
            ctx.channel->addSystemMessage(
                "This group has no anchor channel. Run /gw create from a Twitch channel or set one via /gw create.");
            return {};
        }

        auto plaintext = ctx.words.mid(3).join(' ');
        Credentials::instance().get(
            "openemote", credentialNameForGroup(group), QApplication::instance(),
            [feedback = ctx.channel, def = std::move(def), group,
             plaintext = std::move(plaintext),
             sender = currentUser->getUserName(),
             anchorChannel](const QString &secret) {
                if (secret.isEmpty())
                {
                    feedback->addSystemMessage(
                        QStringLiteral(
                            "Missing key for \"%1\". Use /gw key %1 <secret>.")
                            .arg(group));
                    return;
                }

                const auto payload = encodeEnvelope(group, anchorChannel,
                                                    plaintext, secret);
                if (payload.isEmpty())
                {
                    feedback->addSystemMessage("Failed to encode group whisper.");
                    return;
                }

                auto uniqueMembers = def.members;
                uniqueMembers.removeDuplicates();
                for (const auto &member : uniqueMembers)
                {
                    if (member.compare(sender, Qt::CaseInsensitive) == 0)
                    {
                        continue;
                    }
                    sendOneGroupWhisper(feedback, member, payload);
                }

                auto target = feedback;
                if (!target->isTwitchChannel())
                {
                    auto resolved =
                        getApp()->getTwitch()->getChannelOrEmpty(anchorChannel);
                    if (!resolved->isEmpty())
                    {
                        target = resolved;
                    }
                }
                appendThreadMessage(target, group, sender, plaintext, true);
            });

        return {};
    }

    ctx.channel->addSystemMessage(groupWhisperUsage());
    return {};
}

}  // namespace chatterino::commands
