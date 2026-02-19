// SPDX-FileCopyrightText: 2023 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "controllers/completion/sources/EmoteSource.hpp"

#include "Application.hpp"
#include "controllers/accounts/AccountController.hpp"
#include "controllers/completion/sources/Helpers.hpp"
#include "controllers/emotes/EmoteController.hpp"
#include "providers/bttv/BttvEmotes.hpp"
#include "providers/emoji/Emojis.hpp"
#include "providers/ffz/FfzEmotes.hpp"
#include "providers/seventv/SeventvEmotes.hpp"
#include "providers/twitch/TwitchAccount.hpp"
#include "providers/twitch/TwitchChannel.hpp"
#include "providers/twitch/TwitchIrcServer.hpp"
#include "singletons/Settings.hpp"
#include "widgets/splits/InputCompletionItem.hpp"

#include <QSet>

namespace chatterino::completion {

namespace {

void addEmotes(std::vector<EmoteItem> &out, const EmoteMap &map,
               const QString &providerName)
{
    for (auto &&emote : map)
    {
        out.push_back({.emote = emote.second,
                       .searchName = emote.first.string,
                       .tabCompletionName = emote.first.string,
                       .displayName = emote.second->name.string,
                       .providerName = providerName,
                       .isEmoji = false});
    }
}

void addEmojis(std::vector<EmoteItem> &out, const std::vector<EmojiPtr> &map)
{
    for (const auto &emoji : map)
    {
        for (auto &&shortCode : emoji->shortCodes)
        {
            out.push_back(
                {.emote = emoji->emote,
                 .searchName = shortCode,
                 .tabCompletionName = QStringLiteral(":%1:").arg(shortCode),
                 .displayName = shortCode,
                 .providerName = "Emoji",
                 .isEmoji = true});
        }
    };
}

QString normalizeChannelName(QString name)
{
    name = name.trimmed().toLower();
    while (name.startsWith('#'))
    {
        name.remove(0, 1);
    }
    return name;
}

QSet<QString> parseChannelSet(const QString &csv)
{
    QSet<QString> set;
    for (const auto &entry : csv.split(',', Qt::SkipEmptyParts))
    {
        auto normalized = normalizeChannelName(entry);
        if (!normalized.isEmpty())
        {
            set.insert(normalized);
        }
    }
    return set;
}

bool isAllowedCrossChannel(const QString &sourceChannelName,
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

}  // namespace

EmoteSource::EmoteSource(const Channel *channel,
                         std::unique_ptr<EmoteStrategy> strategy,
                         ActionCallback callback)
    : strategy_(std::move(strategy))
    , callback_(std::move(callback))
{
    this->initializeFromChannel(channel);
}

void EmoteSource::update(const QString &query)
{
    this->output_.clear();
    if (this->strategy_)
    {
        this->strategy_->apply(this->items_, this->output_, query);
    }
}

void EmoteSource::addToListModel(GenericListModel &model, size_t maxCount) const
{
    addVecToListModel(this->output_, model, maxCount,
                      [this](const EmoteItem &e) {
                          return std::make_unique<InputCompletionItem>(
                              e.emote, e.displayName + " - " + e.providerName,
                              this->callback_);
                      });
}

void EmoteSource::addToStringList(QStringList &list, size_t maxCount,
                                  bool /* isFirstWord */) const
{
    addVecToStringList(this->output_, list, maxCount, [](const EmoteItem &e) {
        return e.tabCompletionName + " ";
    });
}

void EmoteSource::initializeFromChannel(const Channel *channel)
{
    auto *app = getApp();

    std::vector<EmoteItem> emotes;
    const auto *tc = dynamic_cast<const TwitchChannel *>(channel);
    // returns true also for special Twitch channels (/live, /mentions, /whispers, etc.)
    if (channel->isTwitchChannel())
    {
        if (tc)
        {
            if (auto twitch = tc->localTwitchEmotes())
            {
                addEmotes(emotes, *twitch, "Local Twitch Emotes");
            }

            auto user = getApp()->getAccounts()->twitch.getCurrent();
            addEmotes(emotes, **user->accessEmotes(), "Twitch Emote");

            // TODO extract "Channel {BetterTTV,7TV,FrankerFaceZ}" text into a #define.
            if (auto bttv = tc->bttvEmotes())
            {
                addEmotes(emotes, *bttv, "Channel BetterTTV");
            }
            if (auto ffz = tc->ffzEmotes())
            {
                addEmotes(emotes, *ffz, "Channel FrankerFaceZ");
            }
            if (auto seventv = tc->seventvEmotes())
            {
                addEmotes(emotes, *seventv, "Channel 7TV");
            }
        }

        if (getSettings()->openEmoteEnableCrossChannelEmotes.getValue())
        {
            const bool allowlistOnly =
                getSettings()
                    ->openEmoteCrossChannelEmotesAllowlistMode.getValue();
            const auto allowChannels = parseChannelSet(
                getSettings()->openEmoteCrossChannelEmotesAllowChannels
                    .getValue());
            const auto blockChannels = parseChannelSet(
                getSettings()->openEmoteCrossChannelEmotesBlockChannels
                    .getValue());
            const auto currentChannelName =
                tc ? normalizeChannelName(tc->getName()) : QString{};

            app->getTwitch()->forEachChannel([&](const auto &c) {
                auto *other = dynamic_cast<TwitchChannel *>(c.get());
                if (other == nullptr)
                {
                    return;
                }

                const auto sourceChannelName =
                    normalizeChannelName(other->getName());
                if (sourceChannelName.isEmpty() ||
                    sourceChannelName == currentChannelName)
                {
                    return;
                }

                if (!isAllowedCrossChannel(sourceChannelName, allowChannels,
                                           blockChannels, allowlistOnly))
                {
                    return;
                }

                if (auto bttv = other->bttvEmotes())
                {
                    addEmotes(emotes, *bttv,
                              QString("Cross-channel BetterTTV (%1)")
                                  .arg(sourceChannelName));
                }
                if (auto ffz = other->ffzEmotes())
                {
                    addEmotes(emotes, *ffz,
                              QString("Cross-channel FrankerFaceZ (%1)")
                                  .arg(sourceChannelName));
                }
                if (auto seventv = other->seventvEmotes())
                {
                    addEmotes(emotes, *seventv,
                              QString("Cross-channel 7TV (%1)")
                                  .arg(sourceChannelName));
                }
            });
        }

        if (auto bttvG = app->getBttvEmotes()->emotes())
        {
            addEmotes(emotes, *bttvG, "Global BetterTTV");
        }
        if (auto ffzG = app->getFfzEmotes()->emotes())
        {
            addEmotes(emotes, *ffzG, "Global FrankerFaceZ");
        }
        if (auto seventvG = app->getSeventvEmotes()->globalEmotes())
        {
            addEmotes(emotes, *seventvG, "Global 7TV");
        }
    }

    addEmojis(emotes, app->getEmotes()->getEmojis()->getEmojis());

    this->items_ = std::move(emotes);
}

const std::vector<EmoteItem> &EmoteSource::output() const
{
    return this->output_;
}

}  // namespace chatterino::completion
