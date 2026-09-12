#pragma once
// Abstracted, simulated desktop elements (要件.txt §3).
// IMPORTANT: this never touches the real desktop; it only invents fake
// rectangles/labels to animate. Nothing here reads real icon or window state.

#include <cstdint>
#include <string>
#include <vector>

namespace core {

struct IconElement {
    float x = 0.0f;
    float y = 0.0f;
    float width = 48.0f;
    float height = 48.0f;
    std::string label;
};

struct WindowElement {
    float x = 0.0f;
    float y = 0.0f;
    float width = 200.0f;
    float height = 150.0f;
    float titleBarHeight = 24.0f;
    std::string title;
};

struct DesktopLayoutConfig {
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;
    int minIcons = 20;
    int maxIcons = 40;
    int minWindows = 5;
    int maxWindows = 10;
    uint32_t seed = 1;
};

// Generates a deterministic (same seed -> same output) fake desktop layout.
// This is generated exactly once at startup / config change and reused on
// every suction loop so elements return to their "original" position
// (要件.txt §4 step 6: 「アイコンとウィンドウを元の位置に再描画」).
struct DesktopLayout {
    std::vector<IconElement> icons;
    std::vector<WindowElement> windows;
};

DesktopLayout GenerateDesktopLayout(const DesktopLayoutConfig& config);

} // namespace core
