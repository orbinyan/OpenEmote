// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/platform/TwitchPlatformAdapter.hpp"

namespace chatterino::platform {

QString TwitchPlatformAdapter::id() const
{
    return "twitch";
}

QString TwitchPlatformAdapter::displayName() const
{
    return "Twitch";
}

Kind TwitchPlatformAdapter::kind() const
{
    return Kind::Twitch;
}

Capabilities TwitchPlatformAdapter::capabilities() const
{
    return {
        .readChat = true,
        .sendChat = true,
        .emotes = true,
        .badges = true,
        .paints = true,
        .whispers = true,
        .moderation = true,
    };
}

void TwitchPlatformAdapter::initialize()
{
    // Transitional adapter: lifecycle is currently managed directly in
    // Application/TwitchIrcServer.
}

void TwitchPlatformAdapter::connect()
{
    // Transitional adapter: lifecycle is currently managed directly in
    // Application/TwitchIrcServer.
}

void TwitchPlatformAdapter::aboutToQuit()
{
    // Transitional adapter: lifecycle is currently managed directly in
    // Application/TwitchIrcServer.
}

}  // namespace chatterino::platform
