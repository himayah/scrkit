#include "test_framework.h"

#include "../src/core/ContentMask.h"

using core::ComputeContentMask;
using core::ContentMaskConfig;
using core::ResampleRgba;

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

void FillRect(std::vector<uint8_t>& buf, int width, int x0, int y0, int x1, int y1, uint8_t r, uint8_t g,
              uint8_t b) {
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            uint8_t* p = &buf[(static_cast<size_t>(y) * width + x) * 4];
            p[0] = r;
            p[1] = g;
            p[2] = b;
            p[3] = 255;
        }
    }
}
} // namespace

TEST_CASE(ContentMask_IdenticalBuffersProduceNoContent) {
    auto capture = SolidBuffer(64, 64, 10, 20, 30);
    auto wallpaper = SolidBuffer(64, 64, 10, 20, 30);
    ContentMaskConfig config;
    config.screenWidth = 64;
    config.screenHeight = 64;
    config.gridN = 4;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    CHECK_EQ(mask.size(), static_cast<size_t>(16));
    for (bool cell : mask) CHECK(!cell);
}

TEST_CASE(ContentMask_OnlyDifferingCellIsFlagged) {
    const int width = 64, height = 64, gridN = 4; // 16x16 cells
    auto capture = SolidBuffer(width, height, 0, 0, 0);
    auto wallpaper = SolidBuffer(width, height, 0, 0, 0);
    // Cell (row=1, col=2) covers x in [32,48), y in [16,32).
    FillRect(capture, width, 32, 16, 48, 32, 255, 255, 255);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);

    const size_t flaggedIndex = static_cast<size_t>(1) * gridN + 2;
    for (size_t i = 0; i < mask.size(); ++i) {
        CHECK_EQ(mask[i], i == flaggedIndex);
    }
}

TEST_CASE(ContentMask_BelowFractionThresholdStaysFalse) {
    const int width = 64, height = 64, gridN = 4; // each cell is 16x16 = 256 px
    auto capture = SolidBuffer(width, height, 0, 0, 0);
    auto wallpaper = SolidBuffer(width, height, 0, 0, 0);
    // Only 2 of 256 pixels in cell (0,0) differ -- well under the default 3%.
    FillRect(capture, width, 0, 0, 2, 1, 255, 255, 255);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    for (bool cell : mask) CHECK(!cell);
}

TEST_CASE(ContentMask_MinorPerPixelNoiseStaysBelowPixelThreshold) {
    const int width = 32, height = 32, gridN = 2;
    auto capture = SolidBuffer(width, height, 100, 100, 100);
    auto wallpaper = SolidBuffer(width, height, 100, 100, 100);
    // Every pixel differs by a tiny amount (well under pixelDiffThreshold),
    // simulating compression/resample noise rather than real content.
    for (size_t i = 0; i < capture.size(); i += 4) {
        capture[i + 0] = 102;
    }
    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    for (bool cell : mask) CHECK(!cell);
}

TEST_CASE(ResampleRgba_NearestNeighborUpscalePicksSourcePixels) {
    // 2x1 source: left pixel red, right pixel blue.
    std::vector<uint8_t> src = {255, 0, 0, 255, 0, 0, 255, 255};
    std::vector<uint8_t> dst(4 * 1 * 4, 0);
    ResampleRgba(src.data(), 2, 1, dst.data(), 4, 1);
    // First half of destination should sample the red source pixel, second
    // half the blue one.
    CHECK_EQ(dst[0], static_cast<uint8_t>(255));
    CHECK_EQ(dst[1], static_cast<uint8_t>(0));
    CHECK_EQ(dst[4 + 0], static_cast<uint8_t>(255));
    CHECK_EQ(dst[8 + 2], static_cast<uint8_t>(255));
    CHECK_EQ(dst[12 + 2], static_cast<uint8_t>(255));
}

TEST_CASE(ResampleRgba_DownscaleProducesRequestedSize) {
    auto src = SolidBuffer(8, 8, 5, 6, 7);
    std::vector<uint8_t> dst(2 * 2 * 4, 0);
    ResampleRgba(src.data(), 8, 8, dst.data(), 2, 2);
    for (size_t i = 0; i < dst.size(); i += 4) {
        CHECK_EQ(dst[i + 0], static_cast<uint8_t>(5));
        CHECK_EQ(dst[i + 1], static_cast<uint8_t>(6));
        CHECK_EQ(dst[i + 2], static_cast<uint8_t>(7));
    }
}
