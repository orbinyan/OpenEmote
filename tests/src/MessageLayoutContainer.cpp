// SPDX-FileCopyrightText: 2024 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "messages/layouts/MessageLayoutContainer.hpp"

#include "common/Literals.hpp"
#include "messages/Emote.hpp"
#include "messages/layouts/MessageLayoutContext.hpp"
#include "messages/layouts/MessageLayoutElement.hpp"
#include "messages/Message.hpp"
#include "messages/MessageElement.hpp"
#include "mocks/BaseApplication.hpp"
#include "singletons/Fonts.hpp"
#include "singletons/Resources.hpp"
#include "singletons/Theme.hpp"
#include "Test.hpp"

#include <memory>
#include <algorithm>
#include <QTime>
#include <vector>

using namespace chatterino;
using namespace literals;

namespace {

class MockApplication : mock::BaseApplication
{
public:
    MockApplication()
        : theme(this->paths_)
        , fonts(this->settings)
    {
    }
    Theme *getThemes() override
    {
        return &this->theme;
    }

    Fonts *getFonts() override
    {
        return &this->fonts;
    }

    Theme theme;
    Fonts fonts;
};

std::vector<std::shared_ptr<MessageElement>> makeElements(const QString &text)
{
    std::vector<std::shared_ptr<MessageElement>> elements;
    bool seenUsername = false;
    for (const auto &word : text.split(' '))
    {
        if (word.startsWith('@'))
        {
            if (seenUsername)
            {
                elements.emplace_back(std::make_shared<MentionElement>(
                    word, word, MessageColor{}, MessageColor{}));
            }
            else
            {
                elements.emplace_back(std::make_shared<TextElement>(
                    word, MessageElementFlag::Username, MessageColor{},
                    FontStyle::ChatMediumBold));
                seenUsername = true;
            }
            continue;
        }

        if (word.startsWith('!'))
        {
            auto emote = std::make_shared<Emote>(Emote{
                .name = EmoteName{word},
                .images = ImageSet{Image::fromResourcePixmap(
                    getResources().twitch.automod)},
                .tooltip = {},
                .homePage = {},
                .id = {},
                .author = {},
                .baseName = {},
            });
            elements.emplace_back(std::make_shared<EmoteElement>(
                emote, MessageElementFlag::Emote));
            continue;
        }

        elements.emplace_back(std::make_shared<TextElement>(
            word, MessageElementFlag::Text, MessageColor{},
            FontStyle::ChatMedium));
    }

    return elements;
}

using TestParam = std::tuple<QString, QString, TextDirection>;

}  // namespace

namespace chatterino {

class MessageLayoutContainerTest : public ::testing::TestWithParam<TestParam>
{
public:
    MessageLayoutContainerTest() = default;

    MockApplication mockApplication;
};

TEST_P(MessageLayoutContainerTest, RtlReordering)
{
    auto [inputText, expected, expectedDirection] = GetParam();
    MessageLayoutContainer container;
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags =
            {
                MessageElementFlag::Text,
                MessageElementFlag::Username,
                MessageElementFlag::Emote,
            },
        .width = 10000,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };
    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale,
                          {MessageFlag::Collapsed});

    auto elements = makeElements(inputText);
    for (const auto &element : elements)
    {
        element->addToContainer(container, ctx);
    }
    container.endLayout();
    ASSERT_EQ(container.line_, 1) << "unexpected linebreak";

    int x = -1;
    for (const auto &el : container.elements_)
    {
        ASSERT_LT(x, el->getRect().x());
        x = el->getRect().x();
    }

    QString got;
    for (const auto &el : container.elements_)
    {
        if (!got.isNull())
        {
            got.append(' ');
        }

        if (dynamic_cast<ImageLayoutElement *>(el.get()))
        {
            el->addCopyTextToString(got);
            if (el->hasTrailingSpace())
            {
                got.chop(1);
            }
        }
        else
        {
            got.append(el->getText());
        }
    }

    ASSERT_EQ(got, expected) << got;
    ASSERT_EQ(container.textDirection_, expectedDirection) << got;
}

TEST(MessageLayoutContainer, OpenEmoteTimestampRightClamp)
{
    MockApplication mockApplication;
    getSettings()->openEmoteCompactAuthorAvatar = true;

    MessageLayoutContainer container;
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags =
            {
                MessageElementFlag::Text,
                MessageElementFlag::Username,
                MessageElementFlag::Timestamp,
            },
        .width = 420,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };
    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale,
                          {MessageFlag::Collapsed});

    TextElement user("orbinyan:", MessageElementFlag::Username, MessageColor{},
                     FontStyle::ChatMediumBold);
    user.addToContainer(container, ctx);

    TimestampElement timestamp(QTime(12, 34, 56));
    timestamp.addToContainer(container, ctx);

    container.endLayout();
    ASSERT_GT(container.getHeight(), 0);

    const MessageLayoutElement *timestampElement = nullptr;
    for (int x = int(ctx.width) - 1; x >= 0; x--)
    {
        const auto *element =
            container.getElementAt(QPointF(x, container.getHeight() / 2.0));
        if (element != nullptr)
        {
            timestampElement = element;
            break;
        }
    }
    ASSERT_NE(timestampElement, nullptr);
    EXPECT_TRUE(timestampElement->getFlags().has(MessageElementFlag::Timestamp));
}

TEST(MessageLayoutContainer, OpenEmoteTimestampRightClampWithoutCompactIdentity)
{
    MockApplication mockApplication;
    getSettings()->openEmoteCompactAuthorAvatar = false;

    MessageLayoutContainer container;
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags =
            {
                MessageElementFlag::Text,
                MessageElementFlag::Username,
                MessageElementFlag::Timestamp,
            },
        .width = 420,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };
    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale,
                          {MessageFlag::Collapsed});

    TextElement user("orbinyan:", MessageElementFlag::Username, MessageColor{},
                     FontStyle::ChatMediumBold);
    user.addToContainer(container, ctx);

    TimestampElement timestamp(QTime(12, 34, 56));
    timestamp.addToContainer(container, ctx);

    container.endLayout();
    ASSERT_GT(container.getHeight(), 0);

    const MessageLayoutElement *timestampElement = nullptr;
    for (int x = int(ctx.width) - 1; x >= 0; x--)
    {
        const auto *element =
            container.getElementAt(QPointF(x, container.getHeight() / 2.0));
        if (element != nullptr)
        {
            timestampElement = element;
            break;
        }
    }
    ASSERT_NE(timestampElement, nullptr);
    EXPECT_TRUE(timestampElement->getFlags().has(MessageElementFlag::Timestamp));
}

TEST(MessageLayoutContainer, OpenEmoteTimestampRightClampWithLongHeader)
{
    MockApplication mockApplication;
    getSettings()->openEmoteCompactAuthorAvatar = false;

    MessageLayoutContainer container;
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags =
            {
                MessageElementFlag::Text,
                MessageElementFlag::Username,
                MessageElementFlag::ReplyButton,
                MessageElementFlag::Timestamp,
            },
        .width = 480,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };
    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale,
                          {MessageFlag::Collapsed});

    TextElement user("very_long_username_for_timestamp_layout_check:",
                     MessageElementFlag::Username, MessageColor{},
                     FontStyle::ChatMediumBold);
    user.addToContainer(container, ctx);

    TextElement reply("↩", MessageElementFlag::ReplyButton, MessageColor{},
                      FontStyle::ChatMedium);
    reply.addToContainer(container, ctx);

    TimestampElement timestamp(QTime(12, 34, 56));
    timestamp.addToContainer(container, ctx);

    container.endLayout();
    ASSERT_GT(container.getHeight(), 0);

    const MessageLayoutElement *timestampElement = nullptr;
    const auto y = container.getHeight() / 2.0;
    for (int x = int(ctx.width) - 1; x >= 0; x--)
    {
        const auto *element = container.getElementAt(QPointF(x, y));
        if (element != nullptr &&
            element->getFlags().has(MessageElementFlag::Timestamp))
        {
            timestampElement = element;
            break;
        }
    }
    ASSERT_NE(timestampElement, nullptr);
    EXPECT_GE(timestampElement->getRect().right(), int(ctx.width) - 10);
}

TEST(MessageLayoutContainer, OpenEmoteTimestampRightClampWithReplyElement)
{
    MockApplication mockApplication;
    getSettings()->openEmoteCompactAuthorAvatar = false;

    MessageLayoutContainer container;
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags =
            {
                MessageElementFlag::Text,
                MessageElementFlag::Username,
                MessageElementFlag::ReplyButton,
                MessageElementFlag::Timestamp,
            },
        .width = 420,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };
    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale,
                          {MessageFlag::Collapsed});

    TextElement user("orbinyan:", MessageElementFlag::Username, MessageColor{},
                     FontStyle::ChatMediumBold);
    user.addToContainer(container, ctx);

    TextElement reply("↩", MessageElementFlag::ReplyButton, MessageColor{},
                      FontStyle::ChatMedium);
    reply.addToContainer(container, ctx);

    TimestampElement timestamp(QTime(12, 34, 56));
    timestamp.addToContainer(container, ctx);

    container.endLayout();
    ASSERT_GT(container.getHeight(), 0);

    const MessageLayoutElement *timestampElement = nullptr;
    for (int x = int(ctx.width) - 1; x >= 0; x--)
    {
        const auto *element =
            container.getElementAt(QPointF(x, container.getHeight() / 2.0));
        if (element != nullptr)
        {
            timestampElement = element;
            break;
        }
    }
    ASSERT_NE(timestampElement, nullptr);
    EXPECT_TRUE(timestampElement->getFlags().has(MessageElementFlag::Timestamp));
}

TEST(MessageLayoutContainer, OpenEmoteTimestampRightClampNarrowWidth)
{
    MockApplication mockApplication;
    getSettings()->openEmoteCompactAuthorAvatar = false;

    MessageLayoutContainer container;
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags =
            {
                MessageElementFlag::Text,
                MessageElementFlag::Username,
                MessageElementFlag::ReplyButton,
                MessageElementFlag::Timestamp,
            },
        .width = 300,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };
    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale,
                          {MessageFlag::Collapsed});

    TextElement user("longish_user_name:", MessageElementFlag::Username,
                     MessageColor{}, FontStyle::ChatMediumBold);
    user.addToContainer(container, ctx);

    TextElement reply("↩", MessageElementFlag::ReplyButton, MessageColor{},
                      FontStyle::ChatMedium);
    reply.addToContainer(container, ctx);

    TimestampElement timestamp(QTime(12, 34, 56));
    timestamp.addToContainer(container, ctx);

    container.endLayout();
    ASSERT_GT(container.getHeight(), 0);

    const MessageLayoutElement *timestampElement = nullptr;
    for (int x = int(ctx.width) - 1; x >= 0; x--)
    {
        for (int y = 0; y < container.getHeight(); y++)
        {
            const auto *element = container.getElementAt(QPointF(x, y));
            if (element != nullptr &&
                element->getFlags().has(MessageElementFlag::Timestamp))
            {
                timestampElement = element;
                break;
            }
        }
        if (timestampElement != nullptr)
        {
            break;
        }
    }

    ASSERT_NE(timestampElement, nullptr);
    EXPECT_GE(timestampElement->getRect().right(), int(ctx.width) - 10);
}

TEST(MessageLayoutContainer, OpenEmoteBadgesRenderBeforeUsername)
{
    MockApplication mockApplication;
    getSettings()->openEmoteCompactAuthorAvatar = false;

    MessageLayoutContainer container;
    MessageLayoutContext ctx{
        .messageColors = {},
        .flags =
            {
                MessageElementFlag::Text,
                MessageElementFlag::Username,
                MessageElementFlag::BadgeVanity,
                MessageElementFlag::Timestamp,
            },
        .width = 520,
        .scale = 1.0F,
        .imageScale = 1.0F,
    };
    container.beginLayout(ctx.width, ctx.scale, ctx.imageScale,
                          {MessageFlag::Collapsed});

    TextElement badgeVip("VIP", MessageElementFlag::BadgeVanity, MessageColor{},
                         FontStyle::ChatMediumSmall);
    badgeVip.addToContainer(container, ctx);

    TextElement badgeDev("DEV", MessageElementFlag::BadgeVanity, MessageColor{},
                         FontStyle::ChatMediumSmall);
    badgeDev.addToContainer(container, ctx);

    TextElement user("orbinyan:", MessageElementFlag::Username, MessageColor{},
                     FontStyle::ChatMediumBold);
    user.addToContainer(container, ctx);

    TextElement text("hello", MessageElementFlag::Text, MessageColor{},
                     FontStyle::ChatMedium);
    text.addToContainer(container, ctx);

    TimestampElement timestamp(QTime(12, 34, 56));
    timestamp.addToContainer(container, ctx);

    container.endLayout();
    ASSERT_GT(container.getHeight(), 0);

    const MessageLayoutElement *usernameElement = nullptr;
    for (int y = 0; y < int(container.getHeight()) && usernameElement == nullptr;
         y++)
    {
        for (int x = 0; x < int(ctx.width); x++)
        {
            const auto *element = container.getElementAt(QPointF(x, y));
            if (element != nullptr &&
                element->getFlags().has(MessageElementFlag::Username))
            {
                usernameElement = element;
                break;
            }
        }
    }

    ASSERT_NE(usernameElement, nullptr);
    qreal rightMostBadge = -1;
    for (int y = 0; y < int(container.getHeight()); y++)
    {
        for (int x = 0; x < int(usernameElement->getRect().x()); x++)
        {
            const auto *element = container.getElementAt(QPointF(x, y));
            if (element != nullptr &&
                element->getFlags().has(MessageElementFlag::BadgeVanity))
            {
                rightMostBadge = std::max(rightMostBadge, element->getRect().right());
            }
        }
    }
    ASSERT_GE(rightMostBadge, 0);

    const MessageLayoutElement *timestampElement = nullptr;
    for (int x = int(ctx.width) - 1; x >= 0; x--)
    {
        for (int y = 0; y < int(container.getHeight()); y++)
        {
            const auto *element = container.getElementAt(QPointF(x, y));
            if (element != nullptr &&
                element->getFlags().has(MessageElementFlag::Timestamp))
            {
                timestampElement = element;
                break;
            }
        }
        if (timestampElement != nullptr)
        {
            break;
        }
    }

    ASSERT_NE(timestampElement, nullptr);
    EXPECT_LT(rightMostBadge, usernameElement->getRect().x());
    EXPECT_GE(timestampElement->getRect().right(), int(ctx.width) - 10);
}

INSTANTIATE_TEST_SUITE_P(
    MessageLayoutContainer, MessageLayoutContainerTest,
    testing::Values(
        TestParam{
            u"@aliens foo bar baz @foo qox !emote1 !emote2"_s,
            u"@aliens foo bar baz @foo qox !emote1 !emote2"_s,
            TextDirection::LTR,
        },
        TestParam{
            u"@aliens ! foo bar baz @foo qox !emote1 !emote2"_s,
            u"@aliens ! foo bar baz @foo qox !emote1 !emote2"_s,
            TextDirection::LTR,
        },
        TestParam{
            u"@aliens ."_s,
            u"@aliens ."_s,
            TextDirection::Neutral,
        },
        // RTL
        TestParam{
            u"@aliens و غير دارت إعادة, بل كما وقام قُدُماً. قام تم الجوي بوابة, خلاف أراض هو بلا. عن وحتّى ميناء غير"_s,
            u"@aliens غير ميناء وحتّى عن بلا. هو أراض خلاف بوابة, الجوي تم قام قُدُماً. وقام كما بل إعادة, دارت غير و"_s,
            TextDirection::RTL,
        },
        TestParam{
            u"@aliens و غير دارت إعادة, بل ض هو my LTR 123 بلا. عن 123 456 وحتّى ميناء غير"_s,
            u"@aliens غير ميناء وحتّى 456 123 عن بلا. my LTR 123 هو ض بل إعادة, دارت غير و"_s,
            TextDirection::RTL,
        },
        TestParam{
            u"@aliens ور دارت إ @user baz bar عاد هو my LTR 123 بلا. عن 123 456 وحتّ غير"_s,
            u"@aliens غير وحتّ 456 123 عن بلا. my LTR 123 هو عاد baz bar @user إ دارت ور"_s,
            TextDirection::RTL,
        },
        TestParam{
            u"@aliens ور !emote1 !emote2 !emote3 دارت إ @user baz bar عاد هو my LTR 123 بلا. عن 123 456 وحتّ غير"_s,
            u"@aliens غير وحتّ 456 123 عن بلا. my LTR 123 هو عاد baz bar @user إ دارت !emote3 !emote2 !emote1 ور"_s,
            TextDirection::RTL,
        },
        TestParam{
            u"@aliens ور !emote1 !emote2 LTR text !emote3 !emote4 غير"_s,
            u"@aliens غير LTR text !emote3 !emote4 !emote2 !emote1 ور"_s,
            TextDirection::RTL,
        },

        TestParam{
            u"@aliens !!! ور !emote1 !emote2 LTR text !emote3 !emote4 غير"_s,
            u"@aliens غير LTR text !emote3 !emote4 !emote2 !emote1 ور !!!"_s,
            TextDirection::RTL,
        },
        // LTR
        TestParam{
            u"@aliens LTR و غير دا ميناء غير"_s,
            u"@aliens LTR غير ميناء دا غير و"_s,
            TextDirection::LTR,
        },
        TestParam{
            u"@aliens LTR و غير د ض هو my LTR 123 بلا. عن 123 456 وحتّى مير"_s,
            u"@aliens LTR هو ض د غير و my LTR 123 مير وحتّى 456 123 عن بلا."_s,
            TextDirection::LTR,
        },
        TestParam{
            u"@aliens LTR ور دارت إ @user baz bar عاد هو my LTR 123 بلا. عن 123 456 وحتّ غير"_s,
            u"@aliens LTR @user إ دارت ور baz bar هو عاد my LTR 123 غير وحتّ 456 123 عن بلا."_s,
            TextDirection::LTR,
        },
        TestParam{
            u"@aliens LTR ور !emote1 !emote2 !emote3 دارت إ @user baz bar عاد هو my LTR 123 بلا. عن 123 456 وحتّ غير"_s,
            u"@aliens LTR @user إ دارت !emote3 !emote2 !emote1 ور baz bar هو عاد my LTR 123 غير وحتّ 456 123 عن بلا."_s,
            TextDirection::LTR,
        },
        TestParam{
            u"@aliens LTR غير وحتّ !emote1 !emote2 LTR text !emote3 !emote4 عاد هو"_s,
            u"@aliens LTR !emote2 !emote1 وحتّ غير LTR text !emote3 !emote4 هو عاد"_s,
            TextDirection::LTR,
        }));

}  // namespace chatterino
