// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/platform/KickPlatformAdapter.hpp"

namespace chatterino::platform {

QString KickPlatformAdapter::id() const
{
    return "kick";
}

QString KickPlatformAdapter::displayName() const
{
    return "Kick";
}

Kind KickPlatformAdapter::kind() const
{
    return Kind::Kick;
}

Capabilities KickPlatformAdapter::capabilities() const
{
    return {
        .readChat = true,
        .sendChat = true,
        .emotes = true,
        .badges = true,
        .paints = true,
        .whispers = false,
        .moderation = true,
    };
}

void KickPlatformAdapter::initialize()
{
    // Placeholder adapter: concrete Kick runtime wiring is a follow-up slice.
}

void KickPlatformAdapter::connect()
{
    // Placeholder adapter: concrete Kick runtime wiring is a follow-up slice.
}

void KickPlatformAdapter::aboutToQuit()
{
    // Placeholder adapter: concrete Kick runtime wiring is a follow-up slice.
}

}  // namespace chatterino::platform
