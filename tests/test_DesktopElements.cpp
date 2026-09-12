#include "test_framework.h"

#include "../src/core/DesktopElements.h"

using core::DesktopLayoutConfig;
using core::GenerateDesktopLayout;

TEST_CASE(DesktopElements_IconCountWithinRequiredRange) {
    DesktopLayoutConfig config;
    config.seed = 42;
    auto layout = GenerateDesktopLayout(config);
    CHECK(layout.icons.size() >= 20);
    CHECK(layout.icons.size() <= 40);
}

TEST_CASE(DesktopElements_WindowCountWithinRequiredRange) {
    DesktopLayoutConfig config;
    config.seed = 42;
    auto layout = GenerateDesktopLayout(config);
    CHECK(layout.windows.size() >= 5);
    CHECK(layout.windows.size() <= 10);
}

TEST_CASE(DesktopElements_ElementsStayWithinScreenBounds) {
    DesktopLayoutConfig config;
    config.screenWidth = 1280.0f;
    config.screenHeight = 720.0f;
    config.seed = 7;
    auto layout = GenerateDesktopLayout(config);
    for (const auto& icon : layout.icons) {
        CHECK(icon.x >= 0.0f);
        CHECK(icon.y >= 0.0f);
        CHECK(icon.x + icon.width <= config.screenWidth);
        CHECK(icon.y + icon.height <= config.screenHeight);
    }
    for (const auto& win : layout.windows) {
        CHECK(win.x >= 0.0f);
        CHECK(win.y >= 0.0f);
        CHECK(win.x + win.width <= config.screenWidth);
        CHECK(win.y + win.height <= config.screenHeight);
    }
}

TEST_CASE(DesktopElements_SameSeedProducesSameLayout) {
    DesktopLayoutConfig config;
    config.seed = 123;
    auto a = GenerateDesktopLayout(config);
    auto b = GenerateDesktopLayout(config);
    CHECK_EQ(a.icons.size(), b.icons.size());
    CHECK_EQ(a.windows.size(), b.windows.size());
    for (size_t i = 0; i < a.icons.size(); ++i) {
        CHECK_NEAR(a.icons[i].x, b.icons[i].x, 0.0001f);
        CHECK_NEAR(a.icons[i].y, b.icons[i].y, 0.0001f);
    }
}

TEST_CASE(DesktopElements_DifferentSeedsUsuallyDiffer) {
    DesktopLayoutConfig configA;
    configA.seed = 1;
    DesktopLayoutConfig configB = configA;
    configB.seed = 2;
    auto a = GenerateDesktopLayout(configA);
    auto b = GenerateDesktopLayout(configB);
    CHECK(a.icons.size() != b.icons.size() || a.icons[0].x != b.icons[0].x);
}
