// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/openemote/OpenEmoteApiClient.hpp"

#include "Application.hpp"
#include "common/network/NetworkRequest.hpp"
#include "common/network/NetworkResult.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QApplication>
#include <QUrl>
#include <QUrlQuery>

#include <memory>

namespace {

using namespace chatterino::openemote;

QString endpoint(const QString &baseUrl, const QString &path)
{
    QUrl base(baseUrl.trimmed());
    if (!base.isValid() || base.scheme().isEmpty() || base.host().isEmpty())
    {
        return {};
    }
    auto normalizedPath = base.path();
    if (!normalizedPath.endsWith('/'))
    {
        normalizedPath += '/';
    }
    const auto suffix = path.startsWith('/') ? path.mid(1) : path;
    base.setPath(normalizedPath + suffix);
    return base.toString();
}

bool requireString(const QJsonObject &obj, const char *key, QString &out,
                   QString &error)
{
    const auto value = obj.value(key);
    if (!value.isString())
    {
        error = QString("Missing string field: %1").arg(key);
        return false;
    }
    out = value.toString();
    return true;
}

bool requireBool(const QJsonObject &obj, const char *key, bool &out,
                 QString &error)
{
    const auto value = obj.value(key);
    if (!value.isBool())
    {
        error = QString("Missing bool field: %1").arg(key);
        return false;
    }
    out = value.toBool();
    return true;
}

bool requireInt(const QJsonObject &obj, const char *key, int &out,
                QString &error)
{
    const auto value = obj.value(key);
    if (!value.isDouble())
    {
        error = QString("Missing integer field: %1").arg(key);
        return false;
    }
    out = value.toInt();
    return true;
}

bool requireInt64(const QJsonObject &obj, const char *key, qint64 &out,
                  QString &error)
{
    const auto value = obj.value(key);
    if (!value.isDouble())
    {
        error = QString("Missing int64 field: %1").arg(key);
        return false;
    }
    out = static_cast<qint64>(value.toDouble());
    return true;
}

bool parseSetItem(const QJsonObject &obj, OpenEmoteSetItem &out, QString &error)
{
    if (!requireString(obj, "link_id", out.linkId, error))
    {
        return false;
    }
    if (!requireString(obj, "emote_id", out.emoteId, error))
    {
        return false;
    }
    if (!requireString(obj, "alias_name", out.aliasName, error))
    {
        return false;
    }
    if (!requireString(obj, "canonical_name", out.canonicalName, error))
    {
        return false;
    }
    if (!requireInt(obj, "position", out.position, error))
    {
        return false;
    }
    return true;
}

bool parseSet(const QJsonObject &obj, OpenEmoteChannelSet &out, QString &error)
{
    if (!requireString(obj, "id", out.id, error))
    {
        return false;
    }
    if (!requireString(obj, "channel_id", out.channelId, error))
    {
        return false;
    }
    if (!requireString(obj, "name", out.name, error))
    {
        return false;
    }
    out.description = obj.value("description").toString();
    if (!requireBool(obj, "is_default", out.isDefault, error))
    {
        return false;
    }
    if (!requireInt(obj, "emote_count", out.emoteCount, error))
    {
        return false;
    }
    if (!requireString(obj, "created_at", out.createdAt, error))
    {
        return false;
    }
    if (!requireString(obj, "updated_at", out.updatedAt, error))
    {
        return false;
    }

    const auto itemsValue = obj.value("items");
    if (!itemsValue.isArray())
    {
        error = "Missing array field: items";
        return false;
    }
    for (const auto &v : itemsValue.toArray())
    {
        if (!v.isObject())
        {
            error = "Invalid item in set.items";
            return false;
        }
        OpenEmoteSetItem item;
        if (!parseSetItem(v.toObject(), item, error))
        {
            return false;
        }
        out.items.push_back(std::move(item));
    }
    return true;
}

}  // namespace

namespace chatterino::openemote {

bool parseBootstrapPolicy(const QJsonObject &root,
                          OpenEmoteBootstrapPolicy &out, QString &error)
{
    if (!requireString(root, "channel_id", out.channelId, error))
    {
        return false;
    }
    if (!requireBool(root, "oauth_connected", out.oauthConnected, error))
    {
        return false;
    }
    if (!requireInt(root, "hosted_free_emote_limit", out.hostedFreeEmoteLimit,
                    error))
    {
        return false;
    }
    if (!requireString(root, "self_host_emote_limit", out.selfHostEmoteLimit,
                       error))
    {
        return false;
    }
    if (!requireString(root, "pricing", out.pricing, error))
    {
        return false;
    }

    const auto limits = root.value("competitor_free_limits");
    if (!limits.isObject())
    {
        error = "Missing object field: competitor_free_limits";
        return false;
    }
    out.competitorFreeLimits.clear();
    const auto limitsObj = limits.toObject();
    for (auto it = limitsObj.begin(); it != limitsObj.end(); ++it)
    {
        if (!it.value().isDouble())
        {
            error = QString("Invalid competitor_free_limits value for key: %1")
                        .arg(it.key());
            return false;
        }
        out.competitorFreeLimits.insert(it.key(), it.value().toInt());
    }

    if (out.competitorFreeLimits.isEmpty())
    {
        error = "competitor_free_limits must not be empty";
        return false;
    }

    return true;
}

bool parsePackExport(const QJsonObject &root, OpenEmotePackExport &out,
                     QString &error)
{
    if (!requireString(root, "channel_id", out.channelId, error))
    {
        return false;
    }
    if (!requireString(root, "default_set_id", out.defaultSetId, error))
    {
        return false;
    }
    if (!requireInt64(root, "pack_revision", out.packRevision, error))
    {
        return false;
    }

    const auto sets = root.value("sets");
    if (!sets.isArray())
    {
        error = "Missing array field: sets";
        return false;
    }

    out.sets.clear();
    for (const auto &v : sets.toArray())
    {
        if (!v.isObject())
        {
            error = "Invalid set entry in sets";
            return false;
        }
        OpenEmoteChannelSet set;
        if (!parseSet(v.toObject(), set, error))
        {
            return false;
        }
        out.sets.push_back(std::move(set));
    }

    if (out.sets.isEmpty())
    {
        error = "Pack export returned no sets";
        return false;
    }
    return true;
}

void OpenEmoteApiClient::fetchBootstrap(
    const QString &baseUrl, std::function<void(OpenEmoteBootstrapPolicy)> ok,
    Fail fail) const
{
    const auto url = endpoint(baseUrl, "/api/account/bootstrap");
    if (url.isEmpty())
    {
        fail("Invalid OpenEmote base URL");
        return;
    }

    auto failCb = std::make_shared<Fail>(std::move(fail));

    NetworkRequest(QUrl(url), NetworkRequestType::Get)
        .caller(QApplication::instance())
        .onSuccess([ok = std::move(ok), failCb](
                       const auto &result) {
            const auto root = result.parseJson();
            if (root.isEmpty())
            {
                if (*failCb)
                {
                    (*failCb)("OpenEmote bootstrap: invalid JSON");
                }
                return;
            }
            OpenEmoteBootstrapPolicy policy;
            QString error;
            if (!parseBootstrapPolicy(root, policy, error))
            {
                if (*failCb)
                {
                    (*failCb)(
                        QString("OpenEmote bootstrap parse error: %1").arg(
                            error));
                }
                return;
            }
            ok(std::move(policy));
        })
        .onError([failCb](const auto &result) {
            if (*failCb)
            {
                (*failCb)(QString("OpenEmote bootstrap request failed: %1")
                              .arg(result.formatError()));
            }
        })
        .execute();
}

void OpenEmoteApiClient::fetchPackExport(
    const QString &baseUrl, const QString &channelId,
    std::optional<qint64> knownRevision,
    std::function<void(OpenEmotePackExport)> ok,
    std::function<void()> notModified, Fail fail) const
{
    const auto encodedChannel = QUrl::toPercentEncoding(channelId);
    auto url = endpoint(baseUrl,
                        QString("/api/channels/%1/pack/export")
                            .arg(QString::fromUtf8(encodedChannel)));
    if (url.isEmpty())
    {
        fail("Invalid OpenEmote base URL");
        return;
    }

    if (knownRevision.has_value())
    {
        QUrl qurl(url);
        QUrlQuery query(qurl);
        query.addQueryItem("known_revision", QString::number(*knownRevision));
        qurl.setQuery(query);
        url = qurl.toString();
    }

    auto failCb = std::make_shared<Fail>(std::move(fail));
    auto notModifiedCb = std::make_shared<std::function<void()>>(
        std::move(notModified));

    NetworkRequest(QUrl(url), NetworkRequestType::Get)
        .caller(QApplication::instance())
        .onSuccess([ok = std::move(ok), notModifiedCb, failCb,
                    knownRevision](const auto &result) {
            const auto status = result.status().value_or(200);
            if (status == 304)
            {
                if (*notModifiedCb)
                {
                    (*notModifiedCb)();
                }
                return;
            }

            const auto root = result.parseJson();
            if (root.isEmpty())
            {
                if (*failCb)
                {
                    (*failCb)("OpenEmote pack export: invalid JSON");
                }
                return;
            }

            OpenEmotePackExport pack;
            QString error;
            if (!parsePackExport(root, pack, error))
            {
                if (*failCb)
                {
                    (*failCb)(
                        QString("OpenEmote pack export parse error: %1")
                            .arg(error));
                }
                return;
            }

            if (knownRevision.has_value() && pack.packRevision == *knownRevision)
            {
                if (*notModifiedCb)
                {
                    (*notModifiedCb)();
                }
                return;
            }

            ok(std::move(pack));
        })
        .onError([failCb](const auto &result) {
            if (*failCb)
            {
                (*failCb)(QString("OpenEmote pack export request failed: %1")
                              .arg(result.formatError()));
            }
        })
        .execute();
}

void OpenEmoteApiClient::redeemOauthTicket(const QString &baseUrl,
                                           const QString &ticket, Ok ok,
                                           Fail fail) const
{
    if (ticket.trimmed().isEmpty())
    {
        fail("Ticket must not be empty");
        return;
    }

    const auto url = endpoint(baseUrl, "/api/integrations/redeem");
    if (url.isEmpty())
    {
        fail("Invalid OpenEmote base URL");
        return;
    }

    const QJsonObject payload{
        {"ticket", ticket.trimmed()},
        {"client", "chatterino-openemote"},
    };

    auto failCb = std::make_shared<Fail>(std::move(fail));

    NetworkRequest(QUrl(url), NetworkRequestType::Post)
        .caller(QApplication::instance())
        .json(payload)
        .onSuccess([ok = std::move(ok)](const auto &) {
            ok();
        })
        .onError([failCb](const auto &result) {
            if (*failCb)
            {
                (*failCb)(QString("OpenEmote ticket redeem failed: %1")
                              .arg(result.formatError()));
            }
        })
        .execute();
}

}  // namespace chatterino::openemote
