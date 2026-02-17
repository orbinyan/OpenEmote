// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/platform/PlatformAdapter.hpp"

namespace chatterino::platform {

class TwitchPlatformAdapter : public IAdapter
{
public:
    QString id() const override;
    QString displayName() const override;
    Kind kind() const override;
    Capabilities capabilities() const override;

    void initialize() override;
    void connect() override;
    void aboutToQuit() override;
};

}  // namespace chatterino::platform
