// SPDX-FileCopyrightText: 2026 Contributors to Chatterino <https://chatterino.com>
//
// SPDX-License-Identifier: MIT

#include "util/WidgetHelpers.hpp"

#include "Test.hpp"

#include <QGuiApplication>
#include <QScreen>

#include <algorithm>

using namespace chatterino::widgets;

TEST(WidgetHelpers, BoundsOffReturnsOriginalGeometry)
{
    const QRect input(25, 50, 320, 180);
    EXPECT_EQ(checkInitialBounds(input, BoundsChecking::Off), input);
}

TEST(WidgetHelpers, DesiredBoundsClampsNegativeCoordinatesToScreenOrigin)
{
    auto *screen = QGuiApplication::primaryScreen();
    ASSERT_NE(screen, nullptr);

    const QRect available = screen->availableGeometry();
    if (available.width() <= 0 || available.height() <= 0)
    {
        GTEST_SKIP() << "Primary screen has no available geometry";
    }

    const int width = std::max(1, std::min(available.width(), 180));
    const int height = std::max(1, std::min(available.height(), 120));
    const QRect input(available.left() - 900, available.top() - 900, width,
                      height);

    const QRect bounded =
        checkInitialBounds(input, BoundsChecking::DesiredPosition);

    EXPECT_EQ(bounded.size(), input.size());
    EXPECT_GE(bounded.left(), available.left());
    EXPECT_GE(bounded.top(), available.top());
}
