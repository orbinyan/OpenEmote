// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/OpenEmoteSecureGroupWhisper.hpp"

#include "Test.hpp"

using namespace chatterino::openemote::groupwhisper;

TEST(OpenEmoteSecureGroupWhisper, RoundTrip)
{
    const auto payload =
        encodeEnvelope(QStringView{QStringLiteral("vip_mods")},
                      QStringView{QStringLiteral("orbinyan")},
                      QStringView{QStringLiteral("hello secure group")},
                      QStringView{QStringLiteral("secret")});
    ASSERT_FALSE(payload.isEmpty());

    EnvelopeParts envelope;
    ASSERT_TRUE(parseEnvelope(payload, &envelope));
    EXPECT_EQ(envelope.group, "vip_mods");
    EXPECT_EQ(envelope.channel, "orbinyan");

    QString plaintext;
    ASSERT_TRUE(
        decodeEnvelope(envelope, QStringView{QStringLiteral("secret")}, &plaintext));
    EXPECT_EQ(plaintext, QStringLiteral("hello secure group"));
}

TEST(OpenEmoteSecureGroupWhisper, RejectsTamperedPayload)
{
    auto payload = encodeEnvelope(QStringView{QStringLiteral("vip_mods")},
                                 QStringView{QStringLiteral("orbinyan")},
                                 QStringView{QStringLiteral("hello")},
                                 QStringView{QStringLiteral("secret")});
    ASSERT_FALSE(payload.isEmpty());
    payload[payload.size() - 1] = payload.back() == 'A' ? 'B' : 'A';

    EnvelopeParts envelope;
    ASSERT_TRUE(parseEnvelope(payload, &envelope));

    QString plaintext;
    EXPECT_FALSE(
        decodeEnvelope(envelope, QStringView{QStringLiteral("secret")}, &plaintext));
}

TEST(OpenEmoteSecureGroupWhisper, NormalizesGroupNames)
{
    EXPECT_EQ(normalizeGroupName(QStringView{QStringLiteral(" VIP-ModS ")}),
              QStringLiteral("vip-mods"));
    EXPECT_EQ(normalizeGroupName(QStringView{QStringLiteral("bad space")}),
              QString());
    EXPECT_EQ(normalizeGroupName(QStringView{QStringLiteral("")}), QString());
}
