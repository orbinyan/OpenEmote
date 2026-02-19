// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

class QJsonObject;
class QString;

namespace chatterino {

class Settings;

namespace openemote::integration {

QString integrationTemplateJson();
bool applyIntegrationPack(const QJsonObject &root, Settings &settings,
                          QString &error);

}  // namespace openemote::integration

}  // namespace chatterino
