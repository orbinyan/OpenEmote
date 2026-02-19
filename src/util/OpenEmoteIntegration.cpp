// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/OpenEmoteIntegration.hpp"

#include "common/Credentials.hpp"
#include "singletons/Settings.hpp"
#include "util/ImageUploader.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QRegularExpression>
#include <QStringList>

namespace {

using namespace chatterino;

QSet<QString> allowedTopLevelKeys()
{
    return {
        "version",
        "kind",
        "imageUploader",
        "oauth",
        "secrets",
        "metadata",
    };
}

QSet<QString> allowedImageUploaderKeys()
{
    return {
        "Version",
        "Name",
        "RequestMethod",
        "RequestURL",
        "Body",
        "FileFormName",
        "URL",
        "DeletionURL",
        "Headers",
    };
}

QSet<QString> allowedOauthKeys()
{
    return {
        "bridgeUrl",
        "hideManualInStreamerMode",
    };
}

bool validateAllowedKeys(const QJsonObject &obj, const QSet<QString> &allowed,
                         QString &error, QStringView scope)
{
    for (auto it = obj.begin(); it != obj.end(); ++it)
    {
        if (!allowed.contains(it.key()))
        {
            error = QString("%1 contains unsupported key: %2")
                        .arg(scope, it.key());
            return false;
        }
    }
    return true;
}

void resolveSecretPlaceholders(QJsonObject &imageUploader,
                               const QJsonObject &secrets)
{
    if (!imageUploader.value("Headers").isObject() || secrets.isEmpty())
    {
        return;
    }

    static const QRegularExpression secretRegex(
        R"(\$\{secret:([a-zA-Z0-9_.-]{1,64})\})");
    auto headers = imageUploader.value("Headers").toObject();
    for (auto it = headers.begin(); it != headers.end(); ++it)
    {
        if (!it->isString())
        {
            continue;
        }
        auto value = it->toString();
        auto matchIter = secretRegex.globalMatch(value);
        while (matchIter.hasNext())
        {
            const auto match = matchIter.next();
            const auto secretName = match.captured(1);
            const auto secretValue = secrets.value(secretName).toString();
            value.replace(match.captured(0), secretValue);
        }
        headers[it.key()] = value;
    }
    imageUploader["Headers"] = headers;
}

void persistAuthorizationSecret(Settings &settings)
{
    const auto current = settings.imageUploaderHeaders.getValue();
    static const QRegularExpression authRegex(
        R"((^|;)\s*Authorization\s*:\s*Bearer\s+([^;{}]+)\s*(;|$))",
        QRegularExpression::CaseInsensitiveOption);
    const auto match = authRegex.match(current);
    if (!match.hasMatch())
    {
        return;
    }

    const auto token = match.captured(2).trimmed();
    if (token.isEmpty())
    {
        return;
    }

    Credentials::instance().set("openemote", "imageUploaderBearer", token);

    const auto replacement = match.captured(1) +
                             "Authorization: Bearer {secret:openemote:imageUploaderBearer}" +
                             match.captured(3);
    auto sanitized = current;
    sanitized.replace(match.capturedStart(0), match.capturedLength(0),
                      replacement);
    sanitized = sanitized.trimmed();
    if (sanitized.endsWith(';'))
    {
        sanitized.chop(1);
    }
    settings.imageUploaderHeaders = sanitized;
}

}  // namespace

namespace chatterino::openemote::integration {

QString integrationTemplateJson()
{
    QJsonObject root{
        {"version", "1.0.0"},
        {"kind", "openemote.integration-pack"},
        {"imageUploader",
         QJsonObject{
             {"Version", "1.0.0"},
             {"RequestMethod", "POST"},
             {"RequestURL", "https://ayanami.app/api/upload"},
             {"Body", "MultipartFormData"},
             {"FileFormName", "file"},
             {"URL", "{url}"},
             {"DeletionURL", "{delete_url}"},
             {"Headers",
              QJsonObject{{"Authorization",
                           "Bearer ${secret:ayanami_bearer}"}}},
         }},
        {"oauth",
         QJsonObject{
             {"bridgeUrl", "http://127.0.0.1:6137/openemote/oauth/pending"},
             {"hideManualInStreamerMode", true},
         }},
        {"secrets",
         QJsonObject{
             {"ayanami_bearer", "<paste-bearer-token-here>"},
         }},
    };

    return QString::fromUtf8(
        QJsonDocument(root).toJson(QJsonDocument::Indented));
}

bool applyIntegrationPack(const QJsonObject &root, Settings &settings,
                          QString &error)
{
    if (root.isEmpty())
    {
        error = "Integration pack is empty";
        return false;
    }

    const bool looksLikePack = root.contains("imageUploader") ||
                               root.contains("kind") || root.contains("version");
    if (looksLikePack &&
        !validateAllowedKeys(root, allowedTopLevelKeys(), error, u"Root"))
    {
        return false;
    }
    if (looksLikePack)
    {
        if (!root.value("version").isString() || !root.value("kind").isString())
        {
            error = "Integration pack must include string keys: version, kind";
            return false;
        }
        if (root.value("kind").toString() != "openemote.integration-pack")
        {
            error = "Integration pack kind is unsupported";
            return false;
        }
    }

    QJsonObject imageUploader;
    if (root.contains("imageUploader"))
    {
        if (!root.value("imageUploader").isObject())
        {
            error = "imageUploader must be an object";
            return false;
        }
        imageUploader = root.value("imageUploader").toObject();
    }
    else
    {
        // Backward compatibility: plain uploader objects are still accepted.
        imageUploader = root;
    }

    if (!validateAllowedKeys(imageUploader, allowedImageUploaderKeys(), error,
                             u"imageUploader"))
    {
        return false;
    }

    const auto hasRequiredUploaderKeys =
        imageUploader.value("RequestURL").isString() &&
        imageUploader.value("FileFormName").isString() &&
        imageUploader.value("URL").isString();
    if (!hasRequiredUploaderKeys)
    {
        error = "imageUploader is missing required keys";
        return false;
    }

    QJsonObject secrets;
    if (root.contains("secrets"))
    {
        if (!root.value("secrets").isObject())
        {
            error = "secrets must be an object";
            return false;
        }
        secrets = root.value("secrets").toObject();
    }

    resolveSecretPlaceholders(imageUploader, secrets);

    if (!imageuploader::detail::importSettings(imageUploader, settings))
    {
        error = "Failed to import image uploader settings";
        return false;
    }

    if (root.contains("oauth"))
    {
        if (!root.value("oauth").isObject())
        {
            error = "oauth must be an object";
            return false;
        }

        const auto oauth = root.value("oauth").toObject();
        if (!validateAllowedKeys(oauth, allowedOauthKeys(), error, u"oauth"))
        {
            return false;
        }

        if (oauth.contains("bridgeUrl") && oauth.value("bridgeUrl").isString())
        {
            const auto bridge = oauth.value("bridgeUrl").toString().trimmed();
            if (!bridge.isEmpty())
            {
                settings.openEmoteOauthBridgeUrl = bridge;
            }
        }

        if (oauth.contains("hideManualInStreamerMode") &&
            oauth.value("hideManualInStreamerMode").isBool())
        {
            settings.openEmoteHideManualOauthInStreamerMode =
                oauth.value("hideManualInStreamerMode").toBool();
        }
    }

    persistAuthorizationSecret(settings);
    settings.requestSave();
    return true;
}

}  // namespace chatterino::openemote::integration
