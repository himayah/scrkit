#include "DesktopElements.h"

#include <random>

namespace core {

namespace {

int RandRange(std::mt19937& rng, int lo, int hi) {
    std::uniform_int_distribution<int> dist(lo, hi);
    return dist(rng);
}

float RandRangeF(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

} // namespace

DesktopLayout GenerateDesktopLayout(const DesktopLayoutConfig& config) {
    std::mt19937 rng(config.seed);
    DesktopLayout layout;

    const int iconCount = RandRange(rng, config.minIcons, config.maxIcons);
    layout.icons.reserve(static_cast<size_t>(iconCount));
    for (int i = 0; i < iconCount; ++i) {
        IconElement icon;
        icon.width = 48.0f;
        icon.height = 48.0f;
        icon.x = RandRangeF(rng, 0.0f, config.screenWidth - icon.width);
        icon.y = RandRangeF(rng, 0.0f, config.screenHeight - icon.height);
        icon.label = "Icon " + std::to_string(i + 1);
        layout.icons.push_back(icon);
    }

    const int windowCount = RandRange(rng, config.minWindows, config.maxWindows);
    layout.windows.reserve(static_cast<size_t>(windowCount));
    for (int i = 0; i < windowCount; ++i) {
        WindowElement win;
        win.width = RandRangeF(rng, 200.0f, 480.0f);
        win.height = RandRangeF(rng, 150.0f, 360.0f);
        win.titleBarHeight = 24.0f;
        win.x = RandRangeF(rng, 0.0f, config.screenWidth - win.width);
        win.y = RandRangeF(rng, 0.0f, config.screenHeight - win.height);
        win.title = "Window " + std::to_string(i + 1);
        layout.windows.push_back(win);
    }

    return layout;
}

} // namespace core
