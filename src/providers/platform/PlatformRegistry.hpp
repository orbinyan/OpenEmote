// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "providers/platform/PlatformAdapter.hpp"

#include <QStringView>

#include <memory>
#include <vector>

namespace chatterino::platform {

class PlatformRegistry
{
public:
    bool registerAdapter(std::unique_ptr<IAdapter> adapter);

    const std::vector<std::unique_ptr<IAdapter>> &all() const;
    IAdapter *findById(QStringView id) const;
    IAdapter *findByKind(Kind kind) const;

    void initializeAll();
    void connectAll();
    void aboutToQuitAll();

private:
    std::vector<std::unique_ptr<IAdapter>> adapters_;
};

}  // namespace chatterino::platform
