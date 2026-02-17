// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "providers/platform/PlatformRegistry.hpp"

namespace chatterino::platform {

bool PlatformRegistry::registerAdapter(std::unique_ptr<IAdapter> adapter)
{
    if (!adapter)
    {
        return false;
    }

    if (this->findById(adapter->id()) != nullptr)
    {
        return false;
    }

    this->adapters_.emplace_back(std::move(adapter));
    return true;
}

const std::vector<std::unique_ptr<IAdapter>> &PlatformRegistry::all() const
{
    return this->adapters_;
}

IAdapter *PlatformRegistry::findById(QStringView id) const
{
    for (const auto &adapter : this->adapters_)
    {
        if (adapter->id().compare(id, Qt::CaseInsensitive) == 0)
        {
            return adapter.get();
        }
    }
    return nullptr;
}

IAdapter *PlatformRegistry::findByKind(Kind kind) const
{
    for (const auto &adapter : this->adapters_)
    {
        if (adapter->kind() == kind)
        {
            return adapter.get();
        }
    }
    return nullptr;
}

void PlatformRegistry::initializeAll()
{
    for (const auto &adapter : this->adapters_)
    {
        adapter->initialize();
    }
}

void PlatformRegistry::connectAll()
{
    for (const auto &adapter : this->adapters_)
    {
        adapter->connect();
    }
}

void PlatformRegistry::aboutToQuitAll()
{
    for (const auto &adapter : this->adapters_)
    {
        adapter->aboutToQuit();
    }
}

}  // namespace chatterino::platform
