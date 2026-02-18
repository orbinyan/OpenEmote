// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Aliases.hpp"
#include "common/Atomic.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QString>
#include <QStringList>

#include <memory>
#include <mutex>
#include <optional>
#include <unordered_map>

namespace chatterino {

class Channel;
class EmoteMap;

struct Emote;
using EmotePtr = std::shared_ptr<const Emote>;

class CustomEmotes final
{
public:
    CustomEmotes();

    void loadGlobalEmotes();
    void loadChannelEmotes(std::weak_ptr<Channel> channel,
                           const QString &channelId,
                           const QString &channelDisplayName,
                           bool manualRefresh);

    std::shared_ptr<const EmoteMap> globalEmotes() const;
    std::shared_ptr<const EmoteMap> channelEmotes(
        const QString &channelId) const;

    std::optional<EmotePtr> globalEmote(const EmoteName &name) const;
    std::optional<EmotePtr> channelEmote(const QString &channelId,
                                         const EmoteName &name) const;

private:
    Atomic<std::shared_ptr<const EmoteMap>> global_;

    mutable std::mutex channelMutex_;
    std::unordered_map<QString, Atomic<std::shared_ptr<const EmoteMap>>>
        channelEmotes_;

    static QStringList normalizedBaseUrls();
    static QString cacheProviderKeyForBase(const QString &baseUrl);

    static QJsonArray extractEmotesArray(const QJsonDocument &doc);
    static std::shared_ptr<const EmoteMap> parseEmotes(const QJsonArray &arr,
                                                       const EmoteMap &cache);
};

}  // namespace chatterino
