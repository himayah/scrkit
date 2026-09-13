#include "test_framework.h"

#include "../src/core/WallpaperFit.h"

using core::CompositeWallpaper;
using core::WallpaperFitMode;

namespace {
std::vector<uint8_t> SolidBuffer(int width, int height, uint8_t r, uint8_t g, uint8_t b) {
    std::vector<uint8_t> buf(static_cast<size_t>(width) * height * 4);
    for (size_t i = 0; i < buf.size(); i += 4) {
        buf[i + 0] = r;
        buf[i + 1] = g;
        buf[i + 2] = b;
        buf[i + 3] = 255;
    }
    return buf;
}

const uint8_t* PixelAt(const std::vector<uint8_t>& buf, int width, int x, int y) {
    return &buf[(static_cast<size_t>(y) * width + x) * 4];
}
} // namespace

TEST_CASE(CompositeWallpaper_TileRepeatsFromTopLeft) {
    // 2x2 source: distinct colors per quadrant-ish corner.
    std::vector<uint8_t> src = {
        255, 0, 0, 255,   0, 255, 0, 255,  // row 0: red, green
        0, 0, 255, 255,   255, 255, 0, 255, // row 1: blue, yellow
    };
    std::vector<uint8_t> dst(4 * 4 * 4, 0);
    CompositeWallpaper(src.data(), 2, 2, dst.data(), 4, 4, WallpaperFitMode::Tile, 0, 0, 0);

    // (2,0) should repeat the source's (0,0) = red.
    const uint8_t* p = PixelAt(dst, 4, 2, 0);
    CHECK_EQ(p[0], static_cast<uint8_t>(255));
    CHECK_EQ(p[1], static_cast<uint8_t>(0));
    // (0,2) should repeat the source's (0,0) = red too (wraps in y).
    const uint8_t* p2 = PixelAt(dst, 4, 0, 2);
    CHECK_EQ(p2[0], static_cast<uint8_t>(255));
    CHECK_EQ(p2[1], static_cast<uint8_t>(0));
}

TEST_CASE(CompositeWallpaper_CenterLetterboxesSmallerImage) {
    auto src = SolidBuffer(2, 2, 255, 0, 0);
    std::vector<uint8_t> dst(6 * 6 * 4, 0);
    CompositeWallpaper(src.data(), 2, 2, dst.data(), 6, 6, WallpaperFitMode::Center, 9, 9, 9);

    // Corner should be letterbox color (outside the centered 2x2 image).
    const uint8_t* corner = PixelAt(dst, 6, 0, 0);
    CHECK_EQ(corner[0], static_cast<uint8_t>(9));
    CHECK_EQ(corner[1], static_cast<uint8_t>(9));
    // Center pixel (2,2) should be the source color.
    const uint8_t* center = PixelAt(dst, 6, 2, 2);
    CHECK_EQ(center[0], static_cast<uint8_t>(255));
    CHECK_EQ(center[1], static_cast<uint8_t>(0));
}

TEST_CASE(CompositeWallpaper_FitLettersboxesNonMatchingAspect) {
    // Wide 4x1 source into a 4x4 (square) canvas under Fit: scale = min(4/4,4/1) = 1,
    // image occupies full width but only the middle row height-wise.
    auto src = SolidBuffer(4, 1, 0, 255, 0);
    std::vector<uint8_t> dst(4 * 4 * 4, 0);
    CompositeWallpaper(src.data(), 4, 1, dst.data(), 4, 4, WallpaperFitMode::Fit, 1, 2, 3);

    // Top row should be letterbox (uncovered).
    const uint8_t* top = PixelAt(dst, 4, 0, 0);
    CHECK_EQ(top[0], static_cast<uint8_t>(1));
    CHECK_EQ(top[1], static_cast<uint8_t>(2));
    // Row 1 (0-indexed) should be inside the placed image (rows 1..1 for a
    // 1px-tall scaled image centered in 4 rows: offsetY=(4-1)/2=1).
    const uint8_t* placed = PixelAt(dst, 4, 0, 1);
    CHECK_EQ(placed[0], static_cast<uint8_t>(0));
    CHECK_EQ(placed[1], static_cast<uint8_t>(255));
}

TEST_CASE(CompositeWallpaper_FillCoversEntireCanvasWithNoLetterbox) {
    // Wide 4x1 source into a 4x4 canvas under Fill: scale = max(4/4,4/1) = 4,
    // scaled image is 16x4 -- covers the whole 4x4 canvas (cropped left/right).
    auto src = SolidBuffer(4, 1, 10, 20, 30);
    std::vector<uint8_t> dst(4 * 4 * 4, 255); // pre-fill with a sentinel to prove full coverage
    CompositeWallpaper(src.data(), 4, 1, dst.data(), 4, 4, WallpaperFitMode::Fill, 1, 2, 3);

    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint8_t* p = PixelAt(dst, 4, x, y);
            CHECK_EQ(p[0], static_cast<uint8_t>(10));
            CHECK_EQ(p[1], static_cast<uint8_t>(20));
            CHECK_EQ(p[2], static_cast<uint8_t>(30));
        }
    }
}

TEST_CASE(CompositeWallpaper_StretchIgnoresAspectAndFillsWholeCanvas) {
    std::vector<uint8_t> src = {255, 0, 0, 255, 0, 0, 255, 255}; // 2x1: red, blue
    std::vector<uint8_t> dst(4 * 2 * 4, 0);
    CompositeWallpaper(src.data(), 2, 1, dst.data(), 4, 2, WallpaperFitMode::Stretch, 0, 0, 0);
    // Every row should show the same left-red/right-blue split.
    for (int y = 0; y < 2; ++y) {
        CHECK_EQ(PixelAt(dst, 4, 0, y)[0], static_cast<uint8_t>(255));
        CHECK_EQ(PixelAt(dst, 4, 3, y)[2], static_cast<uint8_t>(255));
    }
}
