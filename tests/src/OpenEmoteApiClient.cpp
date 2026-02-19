// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/openemote/OpenEmoteApiClient.hpp"

#include "Test.hpp"

#include <QJsonArray>
#include <QJsonObject>

using namespace chatterino::openemote;

TEST(OpenEmoteApiClient, ParseBootstrapPolicy)
{
    QJsonObject root{
        {"channel_id", "00000000-0000-0000-0000-000000000201"},
        {"oauth_connected", true},
        {"competitor_free_limits",
         QJsonObject{
             {"7tv", 1000},
             {"bttv", 50},
         }},
        {"hosted_free_emote_limit", 2000},
        {"self_host_emote_limit", "unlimited"},
        {"pricing", "free"},
    };

    OpenEmoteBootstrapPolicy policy;
    QString error;
    ASSERT_TRUE(parseBootstrapPolicy(root, policy, error));
    EXPECT_EQ(policy.channelId, "00000000-0000-0000-0000-000000000201");
    EXPECT_TRUE(policy.oauthConnected);
    EXPECT_EQ(policy.hostedFreeEmoteLimit, 2000);
    EXPECT_EQ(policy.selfHostEmoteLimit, "unlimited");
    EXPECT_EQ(policy.pricing, "free");
    ASSERT_TRUE(policy.competitorFreeLimits.contains("7tv"));
    EXPECT_EQ(policy.competitorFreeLimits.value("7tv"), 1000);
}

TEST(OpenEmoteApiClient, ParsePackExport)
{
    QJsonObject item{
        {"link_id", "11111111-1111-1111-1111-111111111111"},
        {"emote_id", "22222222-2222-2222-2222-222222222222"},
        {"alias_name", "Pog"},
        {"canonical_name", "PogChamp"},
        {"position", 0},
    };

    QJsonObject set{
        {"id", "33333333-3333-3333-3333-333333333333"},
        {"channel_id", "00000000-0000-0000-0000-000000000201"},
        {"name", "default"},
        {"description", "main set"},
        {"is_default", true},
        {"emote_count", 1},
        {"items", QJsonArray{item}},
        {"created_at", "2026-02-18T14:16:29Z"},
        {"updated_at", "2026-02-18T14:16:29Z"},
    };

    QJsonObject root{
        {"channel_id", "00000000-0000-0000-0000-000000000201"},
        {"default_set_id", "33333333-3333-3333-3333-333333333333"},
        {"pack_revision", 42},
        {"sets", QJsonArray{set}},
    };

    OpenEmotePackExport pack;
    QString error;
    ASSERT_TRUE(parsePackExport(root, pack, error));
    EXPECT_EQ(pack.packRevision, 42);
    EXPECT_EQ(pack.defaultSetId, "33333333-3333-3333-3333-333333333333");
    ASSERT_EQ(pack.sets.size(), 1);
    EXPECT_EQ(pack.sets.at(0).items.size(), 1);
    EXPECT_EQ(pack.sets.at(0).items.at(0).aliasName, "Pog");
}

TEST(OpenEmoteApiClient, ParsePackExportFailsClosedWhenMissingRevision)
{
    QJsonObject root{
        {"channel_id", "00000000-0000-0000-0000-000000000201"},
        {"default_set_id", "33333333-3333-3333-3333-333333333333"},
        {"sets", QJsonArray{}},
    };

    OpenEmotePackExport pack;
    QString error;
    EXPECT_FALSE(parsePackExport(root, pack, error));
    EXPECT_FALSE(error.isEmpty());
}

