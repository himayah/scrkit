#include "test_framework.h"

#include "../src/core/ContentMask.h"
#include "../src/core/SampleDesktop.h"

using namespace core;

namespace {
std::vector<uint8_t> Wallpaper(int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> buf(static_cast<size_t>(w) * h * 4);
    for (size_t i = 0; i < buf.size(); i += 4) {
        buf[i] = r;
        buf[i + 1] = g;
        buf[i + 2] = b;
        buf[i + 3] = 255;
    }
    return buf;
}
const uint8_t* Px(const std::vector<uint8_t>& v, int w, int x, int y) { return &v[(static_cast<size_t>(y) * w + x) * 4]; }
} // namespace

TEST_CASE(SampleDesktop_HasTheRightSizeAndThreeInBoundsWindows) {
    const int w = 640, h = 360;
    const auto wall = Wallpaper(w, h, 30, 60, 90);
    const SampleDesktop s = MakeSampleDesktop(wall.data(), w, h);
    CHECK_EQ(s.rgba.size(), wall.size());
    CHECK_EQ(s.windows.size(), static_cast<size_t>(3));
    for (const PixelRect& r : s.windows) {
        CHECK(r.left >= 0 && r.top >= 0 && r.right <= w && r.bottom <= h);
        CHECK(r.right > r.left && r.bottom > r.top);
    }
    for (size_t i = 3; i < s.rgba.size(); i += 4) {
        if (s.rgba[i] != 255) {
            CHECK(false);
            break;
        }
    }
}

TEST_CASE(SampleDesktop_IsDeterministicAndLeavesUntouchedWallpaperAlone) {
    const int w = 640, h = 360;
    const auto wall = Wallpaper(w, h, 30, 60, 90);
    const SampleDesktop a = MakeSampleDesktop(wall.data(), w, h);
    const SampleDesktop b = MakeSampleDesktop(wall.data(), w, h);
    CHECK(a.rgba == b.rgba);
    // A point in the top-right corner touches no drawn object.
    const uint8_t* p = Px(a.rgba, w, w - 10, 10);
    CHECK(p[0] == 30 && p[1] == 60 && p[2] == 90);
    // Inside the light window's body the pixels are near white, not wallpaper.
    const PixelRect win = a.windows[0];
    const uint8_t* q = Px(a.rgba, w, win.right - 5, win.bottom - 5);
    CHECK(q[0] > 240 && q[1] > 240);
}

TEST_CASE(SampleDesktop_NullOrEmptyInputGivesAnEmptyResult) {
    CHECK(MakeSampleDesktop(nullptr, 100, 100).rgba.empty());
    const auto wall = Wallpaper(10, 10, 0, 0, 0);
    CHECK(MakeSampleDesktop(wall.data(), 0, 10).rgba.empty());
}

TEST_CASE(SampleDesktop_OnAWhiteWallpaperOnlyTheWindowRectanglesRecoverTheLightWindow) {
    // The point of the light window: over a white wallpaper its body has no pixel evidence.
    const int w = 1280, h = 720;
    const auto wall = Wallpaper(w, h, 250, 250, 250);
    const SampleDesktop s = MakeSampleDesktop(wall.data(), w, h);
    ContentMaskConfig cfg;
    cfg.screenWidth = w;
    cfg.screenHeight = h;
    cfg.gridN = 64;

    std::vector<float> diff;
    std::vector<bool> raw = ComputeContentMask(s.rgba.data(), wall.data(), cfg, &diff);

    const PixelRect win = s.windows[0];
    auto covered = [&](const std::vector<bool>& mask) {
        int total = 0, on = 0;
        for (int row = 0; row < cfg.gridN; ++row) {
            for (int col = 0; col < cfg.gridN; ++col) {
                const int cx = (col * w) / cfg.gridN + (w / cfg.gridN) / 2, cy = (row * h) / cfg.gridN + (h / cfg.gridN) / 2;
                if (cx < win.left || cx >= win.right || cy < win.top || cy >= win.bottom) continue;
                ++total;
                if (mask[static_cast<size_t>(row) * cfg.gridN + col]) ++on;
            }
        }
        return static_cast<double>(on) / total;
    };
    CHECK(covered(raw) < 0.5); // most of the light window is invisible to the diff

    std::vector<bool> finished = raw;
    std::vector<PixelRect> used;
    FinishContentMask(s.rgba.data(), wall.data(), finished, diff, s.windows, cfg, &used);
    CHECK(used.size() >= 2);
    CHECK_NEAR(covered(finished), 1.0, 1e-9); // with the rectangles, the whole window is content
}

TEST_CASE(FinishContentMask_WithoutCandidatesStillRunsThePixelOnlyChain) {
    const int w = 320, h = 180;
    const auto wall = Wallpaper(w, h, 20, 20, 20);
    std::vector<uint8_t> cap = wall;
    for (int y = 40; y < 120; ++y) {
        for (int x = 60; x < 220; ++x) {
            uint8_t* p = &cap[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = 240;
        }
    }
    ContentMaskConfig cfg;
    cfg.screenWidth = w;
    cfg.screenHeight = h;
    cfg.gridN = 16;
    std::vector<float> diff;
    std::vector<bool> mask = ComputeContentMask(cap.data(), wall.data(), cfg, &diff);
    std::vector<PixelRect> used;
    FinishContentMask(cap.data(), wall.data(), mask, diff, {}, cfg, &used);
    CHECK(used.empty());
    int on = 0;
    for (bool b : mask) on += b ? 1 : 0;
    CHECK(on > 10);
}
