// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/custom/CustomEmotes.hpp"

#include "common/Channel.hpp"
#include "common/Literals.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"
#include "common/QLogging.hpp"
#include "messages/Emote.hpp"
#include "messages/Image.hpp"
#include "messages/ImageSet.hpp"
#include "singletons/Settings.hpp"
#include "util/Helpers.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QSet>
#include <QUrl>

#include <atomic>

namespace chatterino {

namespace {

using namespace literals;

constexpr QSize EMOTE_BASE_SIZE(28, 28);

QString normalizeBaseUrl(QString value)
{
    auto base = value.trimmed();
    while (base.endsWith('/'))
    {
        base.chop(1);
    }
    return base;
}

QString sanitizeCacheToken(const QString &value)
{
    QString out;
    out.reserve(value.size());
    for (const auto &ch : value)
    {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '.' || ch == '-' || ch == '_' ||
            ch == '@')
        {
            out.push_back(ch);
        }
        else
        {
            out.push_back('_');
        }
    }
    return out;
}

QString stringFromObject(const QJsonObject &obj, const QString &key)
{
    auto it = obj.find(key);
    if (it == obj.end())
    {
        return {};
    }
    if (it->isString())
    {
        return it->toString();
    }
    return {};
}

QString urlFromUrlsObject(const QJsonObject &urls, const QString &key)
{
    auto it = urls.find(key);
    if (it != urls.end() && it->isString())
    {
        return it->toString();
    }
    return {};
}

QString urlBestEffort(const QJsonObject &urls, const QStringList &keys)
{
    for (const auto &k : keys)
    {
        auto v = urlFromUrlsObject(urls, k);
        if (!v.isEmpty())
        {
            return v;
        }
    }
    return {};
}

std::shared_ptr<const EmoteMap> mergeEmoteMaps(
    const std::shared_ptr<const EmoteMap> &base,
    const std::shared_ptr<const EmoteMap> &overlay)
{
    if (!overlay || overlay->empty())
    {
        return base ? base : EMPTY_EMOTE_MAP;
    }

    auto merged = base ? *base : EmoteMap{};
    for (const auto &[name, emote] : *overlay)
    {
        merged[name] = emote;
    }
    return std::make_shared<const EmoteMap>(std::move(merged));
}

}  // namespace

CustomEmotes::CustomEmotes()
    : global_(std::make_shared<EmoteMap>())
{
    getSettings()->enableCustomEmoteGlobalEmotes.connect(
        [this] {
            this->loadGlobalEmotes();
        },
        false);
    getSettings()->customEmoteApiBaseUrl.connect(
        [this] {
            this->loadGlobalEmotes();
        },
        false);
    getSettings()->customEmoteApiBaseUrls.connect(
        [this] {
            this->loadGlobalEmotes();
        },
        false);
}

QStringList CustomEmotes::normalizedBaseUrls()
{
    QStringList out;
    QSet<QString> seen;
    auto addIfValid = [&out, &seen](const QString &candidate) {
        const auto normalized = normalizeBaseUrl(candidate);
        if (normalized.isEmpty() || seen.contains(normalized))
        {
            return;
        }
        seen.insert(normalized);
        out.push_back(normalized);
    };

    QString merged = getSettings()->customEmoteApiBaseUrls.getValue();
    merged.replace('\n', ',');
    merged.replace(';', ',');

    for (const auto &item : merged.split(',', Qt::SkipEmptyParts))
    {
        addIfValid(item);
    }

    // Backward-compatible fallback for existing installs.
    if (out.isEmpty())
    {
        addIfValid(getSettings()->customEmoteApiBaseUrl.getValue());
    }

    return out;
}

QString CustomEmotes::cacheProviderKeyForBase(const QString &baseUrl)
{
    const auto base = normalizeBaseUrl(baseUrl);
    if (base.isEmpty())
    {
        return QStringLiteral("customemotes@disabled");
    }
    const auto url = QUrl(base);
    const auto host = url.host();
    const auto token = sanitizeCacheToken(host.isEmpty() ? base : host);
    return QStringLiteral("customemotes@") + token;
}

QJsonArray CustomEmotes::extractEmotesArray(const QJsonDocument &doc)
{
    if (doc.isArray())
    {
        return doc.array();
    }
    if (doc.isObject())
    {
        auto obj = doc.object();
        auto it = obj.find("emotes");
        if (it != obj.end() && it->isArray())
        {
            return it->toArray();
        }
    }
    return {};
}

std::shared_ptr<const EmoteMap> CustomEmotes::parseEmotes(const QJsonArray &arr,
                                                          const EmoteMap &cache)
{
    auto out = EmoteMap();

    for (const auto &value : arr)
    {
        if (!value.isObject())
        {
            continue;
        }

        const auto obj = value.toObject();
        const auto code = stringFromObject(obj, "code");
        if (code.isEmpty())
        {
            continue;
        }

        const auto urlsObj = obj.value("urls").toObject();
        const auto url1x = urlBestEffort(urlsObj, {"1x", "1", "small", "url"});
        const auto url2x = urlBestEffort(urlsObj, {"2x", "2", "medium"});
        const auto url4x = urlBestEffort(urlsObj, {"4x", "4", "large"});

        if (url1x.isEmpty())
        {
            continue;
        }

        const auto tooltip = stringFromObject(obj, "tooltip");
        const auto homepage = stringFromObject(obj, "homepage");

        bool zeroWidth = false;
        if (obj.value("zero_width").isBool())
        {
            zeroWidth = obj.value("zero_width").toBool();
        }

        auto emote = Emote({
            .name = EmoteName{code},
            .images =
                ImageSet{
                    Image::fromUrl(Url{url1x}, 1, EMOTE_BASE_SIZE),
                    Image::fromUrl(Url{url2x.isEmpty() ? url1x : url2x}, 0.5,
                                   EMOTE_BASE_SIZE * 2),
                    Image::fromUrl(Url{url4x.isEmpty() ? url1x : url4x}, 0.25,
                                   EMOTE_BASE_SIZE * 4),
                },
            .tooltip = Tooltip{tooltip.isEmpty() ? (code + "<br>Custom Emote")
                                                 : tooltip},
            .homePage = homepage.isEmpty() ? Url{} : Url{homepage},
            .zeroWidth = zeroWidth,
        });

        out[EmoteName{code}] = cachedOrMakeEmotePtr(std::move(emote), cache);
    }

    return std::make_shared<const EmoteMap>(std::move(out));
}

void CustomEmotes::loadGlobalEmotes()
{
    if (!getSettings()->enableCustomEmoteGlobalEmotes)
    {
        this->global_.set(EMPTY_EMOTE_MAP);
        return;
    }

    const auto baseUrls = normalizedBaseUrls();
    if (baseUrls.isEmpty())
    {
        this->global_.set(EMPTY_EMOTE_MAP);
        return;
    }

    this->global_.set(EMPTY_EMOTE_MAP);

    for (const auto &baseUrl : baseUrls)
    {
        const auto providerKey = cacheProviderKeyForBase(baseUrl);

        readProviderEmotesCache("global", providerKey, [this](auto jsonDoc) {
            auto current = this->global_.get();
            const auto parsed =
                parseEmotes(extractEmotesArray(jsonDoc), *current);
            this->global_.set(mergeEmoteMaps(current, parsed));
        });

        NetworkRequest(baseUrl + "/v1/emotes/global", NetworkRequestType::Get)
            .timeout(30000)
            .onSuccess([this, providerKey](const NetworkResult &result) {
                writeProviderEmotesCache("global", providerKey,
                                         result.getData());
                auto current = this->global_.get();
                const auto parsed =
                    parseEmotes(extractEmotesArray(
                                    QJsonDocument::fromJson(result.getData())),
                                *current);
                this->global_.set(mergeEmoteMaps(current, parsed));
            })
            .onError([baseUrl](const NetworkResult &result) {
                qCWarning(chatterinoApp)
                    << "Failed to fetch custom global emotes from" << baseUrl
                    << ":" << result.formatError();
            })
            .execute();
    }
}

void CustomEmotes::loadChannelEmotes(std::weak_ptr<Channel> channel,
                                     const QString &channelId,
                                     const QString &channelDisplayName,
                                     bool manualRefresh)
{
    (void)channelDisplayName;

    if (!getSettings()->enableCustomEmoteChannelEmotes)
    {
        std::scoped_lock lock(this->channelMutex_);
        this->channelEmotes_[channelId].set(EMPTY_EMOTE_MAP);
        return;
    }

    const auto baseUrls = normalizedBaseUrls();
    if (baseUrls.isEmpty())
    {
        std::scoped_lock lock(this->channelMutex_);
        this->channelEmotes_[channelId].set(EMPTY_EMOTE_MAP);
        return;
    }

    {
        std::scoped_lock lock(this->channelMutex_);
        this->channelEmotes_[channelId].set(EMPTY_EMOTE_MAP);
    }

    bool cacheHit = false;
    for (const auto &baseUrl : baseUrls)
    {
        const auto providerKey = cacheProviderKeyForBase(baseUrl);
        cacheHit =
            readProviderEmotesCache(
                channelId, providerKey,
                [this, channelId](auto jsonDoc) {
                    std::shared_ptr<const EmoteMap> current = EMPTY_EMOTE_MAP;
                    {
                        std::scoped_lock lock(this->channelMutex_);
                        current = this->channelEmotes_[channelId].get();
                        if (!current)
                        {
                            current = EMPTY_EMOTE_MAP;
                        }
                    }

                    const auto parsed =
                        parseEmotes(extractEmotesArray(jsonDoc), *current);
                    std::scoped_lock lock(this->channelMutex_);
                    this->channelEmotes_[channelId].set(
                        mergeEmoteMaps(current, parsed));
                }) ||
            cacheHit;
    }

    auto refreshNotified = std::make_shared<std::atomic_bool>(false);
    auto errorNotified = std::make_shared<std::atomic_bool>(false);

    for (const auto &baseUrl : baseUrls)
    {
        const auto providerKey = cacheProviderKeyForBase(baseUrl);

        NetworkRequest(baseUrl + "/v1/emotes/twitch/" + channelId,
                       NetworkRequestType::Get)
            .timeout(25000)
            .onSuccess([this, providerKey, channelId, channel, manualRefresh,
                        refreshNotified](const NetworkResult &result) {
                writeProviderEmotesCache(channelId, providerKey,
                                         result.getData());

                std::shared_ptr<const EmoteMap> current = EMPTY_EMOTE_MAP;
                {
                    std::scoped_lock lock(this->channelMutex_);
                    current = this->channelEmotes_[channelId].get();
                    if (!current)
                    {
                        current = EMPTY_EMOTE_MAP;
                    }
                }

                const auto parsed =
                    parseEmotes(extractEmotesArray(
                                    QJsonDocument::fromJson(result.getData())),
                                *current);
                {
                    std::scoped_lock lock(this->channelMutex_);
                    this->channelEmotes_[channelId].set(
                        mergeEmoteMaps(current, parsed));
                }

                if (auto shared = channel.lock();
                    manualRefresh && shared && !refreshNotified->exchange(true))
                {
                    shared->addSystemMessage("Custom emotes reloaded.");
                }
            })
            .onError([channel, manualRefresh, cacheHit, errorNotified,
                      baseUrl](const NetworkResult &result) {
                auto shared = channel.lock();
                if (!shared || !manualRefresh || errorNotified->exchange(true))
                {
                    return;
                }

                shared->addSystemMessage(
                    QStringLiteral(
                        "Failed to fetch custom emotes from %1. (Error: %2)")
                        .arg(baseUrl, result.formatError()));
                if (cacheHit)
                {
                    shared->addSystemMessage(
                        "Using cached custom emotes as fallback.");
                }
            })
            .execute();
    }
}

std::shared_ptr<const EmoteMap> CustomEmotes::globalEmotes() const
{
    auto emotes = this->global_.get();
    return emotes ? emotes : EMPTY_EMOTE_MAP;
}

std::shared_ptr<const EmoteMap> CustomEmotes::channelEmotes(
    const QString &channelId) const
{
    std::scoped_lock lock(this->channelMutex_);
    auto it = this->channelEmotes_.find(channelId);
    if (it == this->channelEmotes_.end())
    {
        return EMPTY_EMOTE_MAP;
    }
    auto emotes = it->second.get();
    return emotes ? emotes : EMPTY_EMOTE_MAP;
}

std::optional<EmotePtr> CustomEmotes::globalEmote(const EmoteName &name) const
{
    const auto emotes = this->global_.get();
    const auto it = emotes->find(name);
    if (it == emotes->end())
    {
        return std::nullopt;
    }
    return it->second;
}

std::optional<EmotePtr> CustomEmotes::channelEmote(const QString &channelId,
                                                   const EmoteName &name) const
{
    std::shared_ptr<const EmoteMap> emotes = EMPTY_EMOTE_MAP;
    {
        std::scoped_lock lock(this->channelMutex_);
        auto it = this->channelEmotes_.find(channelId);
        if (it != this->channelEmotes_.end())
        {
            emotes = it->second.get();
        }
    }

    const auto it = emotes->find(name);
    if (it == emotes->end())
    {
        return std::nullopt;
    }
    return it->second;
}

}  // namespace chatterino
