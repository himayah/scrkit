#include "test_framework.h"

#include "../src/core/ContentMask.h"

using core::ComputeContentMask;
using core::ContentMaskConfig;
using core::PixelToGridIndex;
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
    // Only 2 of 256 pixels in cell (0,0) differ -- well under the default fraction.
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

TEST_CASE(ContentMask_ModeratePerPixelDiffBelowNewThresholdStaysFalse) {
    // Models the scattered edge/anti-aliasing noise a detailed photographic
    // wallpaper shows once decoded and scaled by a different pipeline than
    // whatever rendered the real screen (real-machine feedback: analysis of
    // debug_capture.bmp/debug_wallpaper.bmp found this kind of moderate,
    // diffuse per-pixel diff across a wide swath of a mountain-photo
    // wallpaper, well after the brightness/crop fixes). Every pixel differs
    // by a moderate amount (diff=80, comfortably below pixelDiffThreshold's
    // 90) -- none of them should even count as "differing" pixels, let
    // alone flag the cell. Split across two channels in opposite directions
    // (+40/-40) so the R+G+B sum -- and so the brightness-gain correction's
    // computed gain -- stays neutral, matching realistic texture noise
    // (which isn't isolated to a single channel) rather than accidentally
    // exercising the gain correction.
    const int width = 32, height = 32, gridN = 2;
    auto capture = SolidBuffer(width, height, 100, 100, 100);
    auto wallpaper = SolidBuffer(width, height, 100, 100, 100);
    for (size_t i = 0; i < capture.size(); i += 4) {
        capture[i + 0] = 140; // |100-140| = 40
        capture[i + 1] = 60;  // |100-60| = 40 (opposite direction keeps the sum unchanged)
    }
    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    for (bool cell : mask) CHECK(!cell);
}

TEST_CASE(ContentMask_ScatteredDiffBelowNewFractionStaysFalse) {
    // A minority of a cell's pixels (25%, below the new 30% fraction) show a
    // stark diff -- modeling the ~17-19% real-machine noise ceiling measured
    // in a detailed textured background (see cellDifferingFraction's doc
    // comment) plus some margin -- shouldn't be enough to flag the whole
    // cell as content on its own.
    const int width = 100, height = 10, gridN = 1; // single 1000px cell
    auto capture = SolidBuffer(width, height, 0, 0, 0);
    auto wallpaper = SolidBuffer(width, height, 0, 0, 0);
    FillRect(capture, width, 0, 0, 25, 10, 255, 255, 255); // 250/1000 = 25%
    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    for (bool cell : mask) CHECK(!cell);
}

TEST_CASE(ContentMask_ScatteredDiffAboveNewFractionIsFlagged) {
    // Same setup as above but with enough genuinely differing pixels (35%,
    // above the new 30% fraction) to still be recognized as real content --
    // the widened threshold isn't a free pass for anything short of a
    // completely different image.
    const int width = 100, height = 10, gridN = 1; // single 1000px cell
    auto capture = SolidBuffer(width, height, 0, 0, 0);
    auto wallpaper = SolidBuffer(width, height, 0, 0, 0);
    FillRect(capture, width, 0, 0, 35, 10, 255, 255, 255); // 350/1000 = 35%
    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    for (bool cell : mask) CHECK(cell);
}

TEST_CASE(ContentMask_MedianGainIgnoresLargeContentOutlier) {
    // Real-machine feedback: with a large window genuinely open (darker
    // than the wallpaper, since the wallpaper reference never has windows
    // in it), the *old* sum-of-sums gain got dragged down by that window
    // and computed a gain that then wrongly distorted the *background*
    // pixels elsewhere on screen -- background that needed no correction
    // at all got misflagged as content because of it. The median-based
    // gain (see ComputeRobustBrightnessGain) should stay close to 1.0 here
    // since matching background pixels are still the majority (60%),
    // leaving that background correctly unflagged while the dark "window"
    // region (40%) still is.
    const int width = 100, height = 100, gridN = 10; // 10x10 cells, 10x10px each
    auto wallpaper = SolidBuffer(width, height, 150, 150, 150);
    auto capture = SolidBuffer(width, height, 150, 150, 150);
    FillRect(capture, width, 0, 0, 40, height, 10, 10, 10); // 40% dark "window"

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);

    // Bottom-right cell (col=9, row=9) is far from the window's edge (well
    // outside the box blur's radius), so it should see a clean, unflagged
    // background match.
    CHECK(!mask[9 * gridN + 9]);
    // The window region itself should still be flagged as real content.
    CHECK(mask[0 * gridN + 0]);
}

TEST_CASE(ContentMask_BoxBlurSuppressesPeriodicPixelNoise) {
    // Models the fine, high-frequency edge/anti-aliasing noise a detailed
    // photographic wallpaper shows once decoded and scaled by a different
    // pipeline than whatever rendered the real screen (see
    // ComputeContentMask's doc comment): every 5th pair of columns (2 out
    // of every 5 -- 40% of pixels, comfortably above cellDifferingFraction)
    // spikes to a starkly different value. Pre-blur that alone would
    // exceed both pixelDiffThreshold and cellDifferingFraction and flag the
    // cell; post-blur, a (2*radius+1)=5-wide box averages each 5-column
    // period down to the same blended value everywhere, well under
    // pixelDiffThreshold. The clear 60% matching majority also keeps the
    // median-based gain at 1.0, isolating this test to the blur behavior
    // specifically (see ContentMask_MedianGainIgnoresLargeContentOutlier
    // for the gain side).
    const int width = 20, height = 20, gridN = 1; // single 400px cell
    auto wallpaper = SolidBuffer(width, height, 100, 100, 100);
    auto capture = SolidBuffer(width, height, 100, 100, 100);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x % 5 < 2) {
                uint8_t* p = &capture[(static_cast<size_t>(y) * width + x) * 4];
                p[0] = 250; // |250-100| = 150, comfortably over pixelDiffThreshold
            }
        }
    }

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    for (bool cell : mask) CHECK(!cell);
}

TEST_CASE(ContentMask_UniformBrightnessOffsetDoesNotFlagPlainBackground) {
    // Simulates Windows tone-mapping the whole desktop brighter than the
    // wallpaper file's raw pixels when HDR/"Advanced color" is enabled
    // (real-machine feedback: capture sampled ~1.7x brighter than the
    // composited wallpaper reference, which without gain correction flagged
    // ~97% of the screen as "content"). Every capture pixel here is exactly
    // wallpaper*1.7 (clamped), so the two should be recognized as the "same"
    // background once gain-corrected -- only the genuinely different patch
    // (unrelated to the scaling, e.g. a real icon) should be flagged.
    const int width = 64, height = 64, gridN = 4; // 16x16 cells
    auto wallpaper = SolidBuffer(width, height, 60, 80, 100);
    auto capture = SolidBuffer(width, height, 102, 136, 170); // 60/80/100 * 1.7
    // Cell (row=1, col=2) covers x in [32,48), y in [16,32) -- a genuinely
    // different color, not just the uniform brightness offset above.
    FillRect(capture, width, 32, 16, 48, 32, 0, 200, 0);

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

// PixelToGridIndex must agree exactly with ComputeContentMask's own
// [k*totalSize/gridN, (k+1)*totalSize/gridN) cell boundaries -- this is the
// property platform::CreateMaskedTextureFromImage depends on to zero alpha
// on exactly the same cells ComputeContentMask flagged (a real bug: a fixed
// truncated cellW = totalSize/gridN drifted from these boundaries by a
// growing number of pixels whenever totalSize wasn't an exact multiple of
// gridN -- true for almost every real screen width/gridN combination).
TEST_CASE(PixelToGridIndex_AgreesWithComputeContentMaskCellBoundariesForEveryPixel) {
    const int widths[] = {1920, 2560, 3840, 1366};
    const int gridNs[] = {31, 54, 77, 109}; // sqrt(1000/3000/6000/12000) particle presets
    for (int width : widths) {
        for (int gridN : gridNs) {
            for (int x = 0; x < width; ++x) {
                const int col = PixelToGridIndex(x, gridN, width);
                const int x0 = (col * width) / gridN;         // ComputeContentMask's own cell start
                const int x1 = ((col + 1) * width) / gridN;    // ComputeContentMask's own cell end
                CHECK(x >= x0);
                CHECK(x < x1);
            }
        }
    }
}

TEST_CASE(PixelToGridIndex_CoversEveryCellAcrossTheFullRange) {
    // No cell should be unreachable (e.g. the old fixed-cellW bug dumped the
    // whole remainder into the last cell instead of distributing it).
    const int width = 1920, gridN = 54;
    std::vector<bool> hit(static_cast<size_t>(gridN), false);
    for (int x = 0; x < width; ++x) hit[static_cast<size_t>(PixelToGridIndex(x, gridN, width))] = true;
    for (bool h : hit) CHECK(h);
}

TEST_CASE(PixelToGridIndex_LastCellIsNotAbnormallyWide) {
    // Regression check for the specific old bug: the last column absorbing
    // the entire width%gridN remainder (e.g. 30px too wide at 1920/54)
    // instead of the remainder being spread one extra pixel per cell.
    const int width = 1920, gridN = 54;
    int lastCellWidth = 0;
    for (int x = 0; x < width; ++x) {
        if (PixelToGridIndex(x, gridN, width) == gridN - 1) ++lastCellWidth;
    }
    const int nominalCellWidth = width / gridN; // 35
    CHECK(lastCellWidth <= nominalCellWidth + 1);
}
