// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#pragma once

#include "common/Channel.hpp"
#include "common/FlagsEnum.hpp"
#include "messages/Message.hpp"
#include "messages/MessageBuilder.hpp"
#include "messages/MessageColor.hpp"
#include "messages/MessageElement.hpp"
#include "messages/MessageThread.hpp"
#include "providers/colors/ColorProvider.hpp"
#include "util/Helpers.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QHash>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <mutex>
#include <utility>

namespace chatterino::openemote::groupwhisper {

struct EnvelopeParts {
    QString group;
    QString channel;
    QByteArray nonce;
    QByteArray cipher;
    QByteArray mac;
};

inline QString normalizeGroupName(QStringView input)
{
    QString out = input.toString().trimmed().toLower();
    if (out.isEmpty())
    {
        return {};
    }

    static const QRegularExpression allowed(
        QStringLiteral("^[a-z0-9][a-z0-9_-]{0,63}$"));
    if (!allowed.match(out).hasMatch())
    {
        return {};
    }

    return out;
}

inline QString credentialNameForGroup(QStringView groupName)
{
    return QStringLiteral("groupwhisper/%1")
        .arg(normalizeGroupName(groupName));
}

inline QByteArray toBase64UrlNoPad(const QByteArray &bytes)
{
    return bytes.toBase64(QByteArray::Base64UrlEncoding |
                          QByteArray::OmitTrailingEquals);
}

inline QByteArray fromBase64UrlNoPad(QStringView text)
{
    return QByteArray::fromBase64(text.toString().toUtf8(),
                                  QByteArray::Base64UrlEncoding);
}

inline QByteArray deriveKey(QStringView secret)
{
    return QCryptographicHash::hash(secret.toString().toUtf8(),
                                    QCryptographicHash::Sha256);
}

inline QByteArray makeNonce()
{
    QByteArray nonce;
    nonce.reserve(16);
    auto *rng = QRandomGenerator::global();
    for (int i = 0; i < 16; i++)
    {
        nonce.append(static_cast<char>(rng->bounded(256)));
    }
    return nonce;
}

inline QByteArray keystream(const QByteArray &key, const QByteArray &nonce,
                            int length)
{
    QByteArray out;
    out.reserve(length);

    quint32 counter = 0;
    while (out.size() < length)
    {
        QByteArray blockInput = key;
        blockInput += nonce;
        blockInput += QByteArray::number(counter++);
        out += QCryptographicHash::hash(blockInput, QCryptographicHash::Sha256);
    }

    out.resize(length);
    return out;
}

inline QByteArray xorBytes(const QByteArray &lhs, const QByteArray &rhs)
{
    QByteArray out = lhs;
    for (int i = 0; i < out.size(); i++)
    {
        out[i] = static_cast<char>(lhs[i] ^ rhs[i]);
    }
    return out;
}

inline QByteArray computeMac(const QByteArray &key, const QString &group,
                             const QString &channel, const QByteArray &nonce,
                             const QByteArray &cipher)
{
    QByteArray payload;
    payload.reserve(key.size() + group.size() + channel.size() + nonce.size() +
                    cipher.size() + 8);
    payload += key;
    payload += group.toUtf8();
    payload += '\n';
    payload += channel.toUtf8();
    payload += '\n';
    payload += nonce;
    payload += cipher;

    return QCryptographicHash::hash(payload, QCryptographicHash::Sha256);
}

inline bool constantTimeEqual(const QByteArray &lhs, const QByteArray &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    unsigned char diff = 0;
    for (int i = 0; i < lhs.size(); i++)
    {
        diff |= static_cast<unsigned char>(lhs[i] ^ rhs[i]);
    }
    return diff == 0;
}

inline QString encodeEnvelope(QStringView group, QStringView channel,
                              QStringView plaintext, QStringView secret)
{
    const auto normalized = normalizeGroupName(group);
    if (normalized.isEmpty() || channel.isEmpty() || plaintext.isEmpty() ||
        secret.isEmpty())
    {
        return {};
    }

    const auto key = deriveKey(secret);
    const auto nonce = makeNonce();
    const auto plain = plaintext.toString().toUtf8();
    const auto stream = keystream(key, nonce, plain.size());
    const auto cipher = xorBytes(plain, stream);
    const auto mac = computeMac(key, normalized, channel.toString(), nonce,
                                cipher);

    return QStringLiteral("oegw1:%1:%2:%3:%4:%5")
        .arg(QString::fromUtf8(toBase64UrlNoPad(normalized.toUtf8())),
             QString::fromUtf8(toBase64UrlNoPad(channel.toString().toUtf8())),
             QString::fromUtf8(toBase64UrlNoPad(nonce)),
             QString::fromUtf8(toBase64UrlNoPad(cipher)),
             QString::fromUtf8(toBase64UrlNoPad(mac.left(16))));
}

inline bool parseEnvelope(QStringView payload, EnvelopeParts *out)
{
    auto parts = payload.toString().split(':', Qt::KeepEmptyParts);
    if (parts.size() != 6 || parts[0] != QLatin1String("oegw1"))
    {
        return false;
    }

    auto group = QString::fromUtf8(fromBase64UrlNoPad(parts[1]));
    auto channel = QString::fromUtf8(fromBase64UrlNoPad(parts[2]));
    auto nonce = fromBase64UrlNoPad(parts[3]);
    auto cipher = fromBase64UrlNoPad(parts[4]);
    auto mac = fromBase64UrlNoPad(parts[5]);

    group = normalizeGroupName(group);
    if (group.isEmpty() || channel.trimmed().isEmpty() || nonce.size() != 16 ||
        cipher.isEmpty() || mac.size() != 16)
    {
        return false;
    }

    out->group = group;
    out->channel = channel.trimmed();
    out->nonce = nonce;
    out->cipher = cipher;
    out->mac = mac;
    return true;
}

inline bool decodeEnvelope(const EnvelopeParts &envelope, QStringView secret,
                           QString *outPlaintext)
{
    if (secret.isEmpty())
    {
        return false;
    }

    const auto key = deriveKey(secret);
    const auto expectedMac =
        computeMac(key, envelope.group, envelope.channel, envelope.nonce,
                   envelope.cipher)
            .left(16);
    if (!constantTimeEqual(expectedMac, envelope.mac))
    {
        return false;
    }

    const auto stream = keystream(key, envelope.nonce, envelope.cipher.size());
    const auto plain = xorBytes(envelope.cipher, stream);
    *outPlaintext = QString::fromUtf8(plain);
    return !outPlaintext->isEmpty();
}

struct GroupThreadState {
    std::weak_ptr<const Message> root;
    std::weak_ptr<MessageThread> thread;
};

inline std::mutex &threadStateMutex()
{
    static std::mutex mutex;
    return mutex;
}

inline QHash<QString, GroupThreadState> &
threadState()
{
    static QHash<QString, GroupThreadState> state;
    return state;
}

inline std::pair<MessagePtr, std::shared_ptr<MessageThread>> ensureThread(
    const ChannelPtr &channel, QStringView group)
{
    const auto key = channel->getName() + "|" + normalizeGroupName(group);
    {
        std::lock_guard<std::mutex> guard(threadStateMutex());
        auto &map = threadState();
        auto it = map.find(key);
        if (it != map.end())
        {
            if (auto root = it.value().root.lock())
            {
                if (auto thread = it.value().thread.lock())
                {
                    return {root, thread};
                }
            }
            map.erase(it);
        }
    }

    MessageBuilder rootBuilder(systemMessage,
                               QStringLiteral("🔒 VIP thread: %1")
                                   .arg(normalizeGroupName(group)));
    rootBuilder->id = QStringLiteral("openemote-gw-root-%1")
                          .arg(generateUuid().remove('-'));
    rootBuilder->flags.set(MessageFlag::System,
                           MessageFlag::DoNotTriggerNotification);
    auto root = rootBuilder.release();
    channel->addMessage(root, MessageContext::Original);

    auto thread = std::make_shared<MessageThread>(root);
    {
        std::lock_guard<std::mutex> guard(threadStateMutex());
        threadState()[key] = GroupThreadState{
            .root = root,
            .thread = thread,
        };
    }

    return {root, thread};
}

inline void appendThreadMessage(const ChannelPtr &channel, QStringView group,
                                QStringView sender, QStringView content,
                                bool outgoing)
{
    if (!channel || content.isEmpty())
    {
        return;
    }

    const auto [root, thread] = ensureThread(channel, group);

    MessageBuilder builder;
    builder->id =
        QStringLiteral("openemote-gw-msg-%1").arg(generateUuid().remove('-'));
    builder->channelName = channel->getName();
    builder->loginName = sender.toString();
    builder->displayName = sender.toString();
    builder->messageText = QStringLiteral("%1: %2").arg(sender, content);
    builder->searchText = builder->messageText;
    builder->replyParent = root;
    builder->replyThread = thread;
    builder->flags.set(MessageFlag::Whisper, MessageFlag::ReplyMessage,
                       MessageFlag::DoNotTriggerNotification,
                       MessageFlag::SubscribedThread);
    if (outgoing)
    {
        builder->flags.set(MessageFlag::DoNotLog);
    }

    builder.emplace<TimestampElement>();
    builder.emplace<TextElement>(QStringLiteral("🔒"),
                                 MessageElementFlag::Text,
                                 MessageColor::System);
    builder.emplace<TextElement>(QStringLiteral(" %1")
                                     .arg(sender.toString()),
                                 MessageElementFlag::Text, MessageColor::Text,
                                 FontStyle::ChatMediumBold);
    builder.emplace<TextElement>(QStringLiteral(": "),
                                 MessageElementFlag::Text,
                                 MessageColor::Text);
    builder.emplace<TextElement>(content.toString(), MessageElementFlag::Text,
                                 MessageColor::Text);

    auto msg = builder.release();
    thread->addToThread(std::static_pointer_cast<const Message>(msg));
    channel->addMessage(msg, MessageContext::Original);
}

}  // namespace chatterino::openemote::groupwhisper
