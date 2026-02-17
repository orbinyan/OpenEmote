// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include <QString>

#include <cstdint>

namespace chatterino::platform {

enum class Kind : std::uint8_t {
    Twitch,
    Kick,
    OpenStreaming,
};

struct Capabilities {
    bool readChat{};
    bool sendChat{};
    bool emotes{};
    bool badges{};
    bool paints{};
    bool whispers{};
    bool moderation{};
};

class IAdapter
{
public:
    virtual ~IAdapter() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;
    virtual Kind kind() const = 0;
    virtual Capabilities capabilities() const = 0;

    virtual void initialize() = 0;
    virtual void connect() = 0;
    virtual void aboutToQuit() = 0;
};

}  // namespace chatterino::platform
