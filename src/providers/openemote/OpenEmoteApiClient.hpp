// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QList>
#include <QMap>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <optional>

namespace chatterino::openemote {

struct OpenEmoteBootstrapPolicy {
    QMap<QString, int> competitorFreeLimits;
    int hostedFreeEmoteLimit = 0;
    QString selfHostEmoteLimit;
    QString pricing;
    QString channelId;
    bool oauthConnected = false;
};

struct OpenEmoteSetItem {
    QString linkId;
    QString emoteId;
    QString aliasName;
    QString canonicalName;
    int position = 0;
};

struct OpenEmoteChannelSet {
    QString id;
    QString channelId;
    QString name;
    QString description;
    bool isDefault = false;
    int emoteCount = 0;
    QList<OpenEmoteSetItem> items;
    QString createdAt;
    QString updatedAt;
};

struct OpenEmotePackExport {
    QString channelId;
    QString defaultSetId;
    qint64 packRevision = 0;
    QList<OpenEmoteChannelSet> sets;
};

// Parser helpers are exposed for deterministic unit tests.
bool parseBootstrapPolicy(const QJsonObject &root,
                          OpenEmoteBootstrapPolicy &out, QString &error);
bool parsePackExport(const QJsonObject &root, OpenEmotePackExport &out,
                     QString &error);

class OpenEmoteApiClient
{
public:
    using Ok = std::function<void()>;
    using Fail = std::function<void(const QString &)>;

    void fetchBootstrap(const QString &baseUrl,
                        std::function<void(OpenEmoteBootstrapPolicy)> ok,
                        Fail fail) const;

    void fetchPackExport(const QString &baseUrl, const QString &channelId,
                         std::optional<qint64> knownRevision,
                         std::function<void(OpenEmotePackExport)> ok,
                         std::function<void()> notModified, Fail fail) const;

    void redeemOauthTicket(const QString &baseUrl, const QString &ticket, Ok ok,
                           Fail fail) const;
};

}  // namespace chatterino::openemote

