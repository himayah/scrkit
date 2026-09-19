#include "test_framework.h"

#include "../src/core/ContentMask.h"

using core::BoundaryRefinement;
using core::ComputeContentMask;
using core::ContentMaskConfig;
using core::FillBoundaryStraddlingCells;
using core::FillEnclosedMaskHoles;
using core::FillMajorityNeighborCells;
using core::IsContentMaskSuspicious;
using core::PixelToGridIndex;
using core::RefineBoundaryMask;
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

TEST_CASE(ComputeContentMask_OutDifferingFractionReportsTheRawPerCellRatio) {
    // Same setup as ContentMask_OnlyDifferingCellIsFlagged: a single fully-
    // differing cell among otherwise-identical solid buffers. The optional
    // out-param should report ~1.0 for that cell, independent of the
    // cellDifferingFraction cutoff itself, and exactly 0 for a cell far
    // enough away that the box blur's radius-2 window can't bleed the
    // differing block's brightness into it (immediately-adjacent cells are
    // deliberately not checked here -- some blur bleed into their shared
    // border is expected and is exactly what stays under
    // cellDifferingFraction's 30% cutoff without tripping it).
    const int width = 64, height = 64, gridN = 4;
    auto capture = SolidBuffer(width, height, 0, 0, 0);
    auto wallpaper = SolidBuffer(width, height, 0, 0, 0);
    FillRect(capture, width, 32, 16, 48, 32, 255, 255, 255); // cell (row=1, col=2)

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    std::vector<float> fraction;
    ComputeContentMask(capture.data(), wallpaper.data(), config, &fraction);

    CHECK_EQ(fraction.size(), static_cast<size_t>(16));
    const size_t flaggedIndex = static_cast<size_t>(1) * gridN + 2;
    CHECK(fraction[flaggedIndex] > 0.99f);
    const size_t farIndex = static_cast<size_t>(3) * gridN + 0; // opposite corner
    CHECK_EQ(fraction[farIndex], 0.0f);
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

namespace {
// A flat `base` color with a sparse, evenly-spaced grid (every 3rd pixel on
// both axes, ~11% of pixels) of near-black specks -- real per-pixel std
// well above textureFlatnessMargin's default, but sparse and regular enough
// that the box blur averages it down to a small, uniform offset from
// `base` (well under pixelDiffThreshold) and the near-black speck pixels'
// low luminance excludes them from ComputeRobustBrightnessGain's ratio
// (keeping the gain at a neutral 1.0x) -- unlike a plain 50/50 checkerboard,
// which pushes the gain to an extreme and contaminates these tests with the
// *ordinary* diff-fraction check firing instead of isolating the texture-
// flatness fallback this file wants to test. Models a dappled, detailed
// wallpaper patch (leaf-light speckle, gravel, etc.).
std::vector<uint8_t> SparseSpeckledBuffer(int width, int height, uint8_t base) {
    auto buf = SolidBuffer(width, height, base, base, base);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if (x % 3 == 0 && y % 3 == 0) {
                uint8_t* p = &buf[(static_cast<size_t>(y) * width + x) * 4];
                p[0] = p[1] = p[2] = 0;
            }
        }
    }
    return buf;
}
} // namespace

TEST_CASE(ContentMask_TextureFlatnessCatchesAFlatWindowOnATexturedWallpaperPatch) {
    // 64x64, gridN=1 (single cell). The wallpaper is speckled (real texture,
    // std well above the default margin) while the capture is perfectly
    // flat at the same base color -- a plain dark UI background
    // coincidentally landing on a textured wallpaper patch's average
    // brightness. The box blur washes the speckle down to a small, near-
    // uniform offset, so the ordinary diff-fraction check alone stays well
    // under threshold -- only the texture-flatness fallback should catch
    // this.
    const int width = 64, height = 64, gridN = 1;
    auto wallpaper = SparseSpeckledBuffer(width, height, 100);
    auto capture = SolidBuffer(width, height, 100, 100, 100);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    CHECK(mask[0]);
}

TEST_CASE(ContentMask_TextureFlatnessDoesNotTriggerWhenCaptureIsTheMoreTexturedSide) {
    // Same speckle-vs-flat setup, mirrored: the *capture* is the textured
    // one and the wallpaper is flat. The margin (wallpaper std minus
    // capture std) is then negative, so the texture-flatness fallback must
    // not fire -- it only ever adds content, and only for the "suspiciously
    // flatter than the wallpaper" direction.
    const int width = 64, height = 64, gridN = 1;
    auto capture = SparseSpeckledBuffer(width, height, 100);
    auto wallpaper = SolidBuffer(width, height, 100, 100, 100);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    CHECK(!mask[0]);
}

TEST_CASE(ContentMask_TextureFlatnessMarginIsConfigurable) {
    auto capture = SolidBuffer(64, 64, 100, 100, 100);
    auto wallpaper = SparseSpeckledBuffer(64, 64, 100);

    ContentMaskConfig config;
    config.screenWidth = 64;
    config.screenHeight = 64;
    config.gridN = 1;
    config.textureFlatnessMargin = 200.0f; // unreachably high
    CHECK(!ComputeContentMask(capture.data(), wallpaper.data(), config)[0]);

    config.textureFlatnessMargin = 5.0f; // comfortably below the speckle's actual std gap
    CHECK(ComputeContentMask(capture.data(), wallpaper.data(), config)[0]);
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

TEST_CASE(IsContentMaskSuspicious_EmptyMaskIsNotSuspicious) {
    std::vector<bool> mask;
    CHECK(!IsContentMaskSuspicious(mask));
}

TEST_CASE(IsContentMaskSuspicious_AllFlaggedIsSuspicious) {
    std::vector<bool> mask(100, true);
    CHECK(IsContentMaskSuspicious(mask));
}

TEST_CASE(IsContentMaskSuspicious_BelowDefaultThresholdIsNotSuspicious) {
    std::vector<bool> mask(100, false);
    for (int i = 0; i < 89; ++i) mask[static_cast<size_t>(i)] = true; // 89%, just under the 90% default
    CHECK(!IsContentMaskSuspicious(mask));
}

TEST_CASE(IsContentMaskSuspicious_AtDefaultThresholdIsSuspicious) {
    std::vector<bool> mask(100, false);
    for (int i = 0; i < 90; ++i) mask[static_cast<size_t>(i)] = true; // exactly 90%
    CHECK(IsContentMaskSuspicious(mask));
}

TEST_CASE(IsContentMaskSuspicious_CustomThresholdIsRespected) {
    std::vector<bool> mask(10, false);
    mask[0] = true; // 10%
    CHECK(!IsContentMaskSuspicious(mask, 0.5));
    CHECK(IsContentMaskSuspicious(mask, 0.1));
}

TEST_CASE(FillEnclosedMaskHoles_SingleEnclosedCellGetsFilled) {
    // 3x3 grid, all true except the center -- the center can't reach any
    // edge without crossing a true cell.
    std::vector<bool> mask(9, true);
    mask[4] = false; // (row=1, col=1)
    FillEnclosedMaskHoles(mask, 3);
    CHECK(mask[4]);
}

TEST_CASE(FillEnclosedMaskHoles_BorderTouchingFalseRegionStaysFalse) {
    // 3x3 grid, entire left column false -- each of those cells already sits
    // on the grid edge, so none of them is a "hole" to fill.
    std::vector<bool> mask = {false, true, true, false, true, true, false, true, true};
    FillEnclosedMaskHoles(mask, 3);
    CHECK(!mask[0]);
    CHECK(!mask[3]);
    CHECK(!mask[6]);
}

TEST_CASE(FillEnclosedMaskHoles_DiagonalAdjacencyDoesNotCountAsReachingTheEdge) {
    // 3x3 grid: (0,0) is false and touches the border; (1,1) is false too,
    // but only *diagonally* adjacent to (0,0) -- all 4 of its orthogonal
    // neighbors are true. 4-connectivity must not treat the diagonal
    // neighbor as a path out, so (1,1) still counts as enclosed.
    std::vector<bool> mask = {false, true, true, true, false, true, true, true, true};
    FillEnclosedMaskHoles(mask, 3);
    CHECK(!mask[0]);  // (0,0): reaches the edge directly, stays false
    CHECK(mask[4]);   // (1,1): enclosed, gets filled
}

TEST_CASE(FillEnclosedMaskHoles_MultiCellEnclosedRegionIsFullyFilled) {
    // 5x5 grid: a solid true ring around the border, false everywhere in the
    // 3x3 interior -- models a large coincidental-color miss spanning many
    // cells inside one window (real-machine feedback: over 1000 cells at
    // once). All 9 interior cells should be promoted together.
    std::vector<bool> mask(25, true);
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 3; ++col) {
            mask[static_cast<size_t>(row) * 5 + col] = false;
        }
    }
    FillEnclosedMaskHoles(mask, 5);
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 3; ++col) {
            CHECK(mask[static_cast<size_t>(row) * 5 + col]);
        }
    }
}

TEST_CASE(FillMajorityNeighborCells_ThreeOfFourNeighborsFillsTheCell) {
    // 3x3 grid, center false with 3 of its 4 orthogonal neighbors true.
    std::vector<bool> mask = {false, true, false, true, false, true, false, true, false};
    FillMajorityNeighborCells(mask, 3);
    CHECK(mask[4]); // center
}

TEST_CASE(FillMajorityNeighborCells_TwoOfFourNeighborsStaysFalse) {
    // 3x3 grid, center false with only 2 of its 4 orthogonal neighbors true
    // (top and left) -- below the 3-neighbor bar.
    std::vector<bool> mask = {false, true, false, true, false, false, false, false, false};
    FillMajorityNeighborCells(mask, 3);
    CHECK(!mask[4]);
}

TEST_CASE(FillMajorityNeighborCells_CornerCellNeverFillsRegardlessOfNeighbors) {
    // A corner cell has only 2 possible orthogonal neighbors, so it can
    // never reach the 3-neighbor bar even when both are true.
    std::vector<bool> mask = {false, true, true, true, true, true, true, true, true};
    FillMajorityNeighborCells(mask, 3);
    CHECK(!mask[0]); // top-left corner
}

TEST_CASE(FillMajorityNeighborCells_CascadesAlongAChainToConvergence) {
    // 4x4 grid: rows 0, 2, and 3 fully true; row 1 is true only at its left
    // end. Each false cell in row 1 reaches 3 true neighbors (up, down, and
    // its now-filled left neighbor) only after the one before it has
    // already been promoted -- this only fully resolves if the fill
    // iterates to a fixed point rather than a single pass.
    // clang-format off
    std::vector<bool> mask = {
        true,  true,  true,  true,
        true,  false, false, false,
        true,  true,  true,  true,
        true,  true,  true,  true,
    };
    // clang-format on
    FillMajorityNeighborCells(mask, 4);
    for (bool cell : mask) CHECK(cell);
}

TEST_CASE(FillBoundaryStraddlingCells_PromotesWithTwoNeighborsAndNonzeroFraction) {
    // 3x3 grid, center false with 2 true neighbors (top, left) and a small
    // but nonzero own fraction -- models a real window-edge cell.
    std::vector<bool> mask = {false, true, false, true, false, false, false, false, false};
    std::vector<float> fraction = {0, 0, 0, 0, 0.05f, 0, 0, 0, 0};
    FillBoundaryStraddlingCells(mask, fraction, 3);
    CHECK(mask[4]);
}

TEST_CASE(FillBoundaryStraddlingCells_DoesNotPromoteWithExactlyZeroFraction) {
    // Same 2-true-neighbor geometry, but the center's own fraction is
    // exactly zero -- no color evidence at all, so it's left alone (it's
    // exactly as likely to be genuine adjacent background).
    std::vector<bool> mask = {false, true, false, true, false, false, false, false, false};
    std::vector<float> fraction = {0, 0, 0, 0, 0.0f, 0, 0, 0, 0};
    FillBoundaryStraddlingCells(mask, fraction, 3);
    CHECK(!mask[4]);
}

TEST_CASE(FillBoundaryStraddlingCells_DoesNotPromoteWithOnlyOneNeighborAtDefaultRequirement) {
    // Only 1 true neighbor (top) -- below the default requiredNeighbors=2.
    std::vector<bool> mask = {false, true, false, false, false, false, false, false, false};
    std::vector<float> fraction = {0, 0, 0, 0, 0.20f, 0, 0, 0, 0};
    FillBoundaryStraddlingCells(mask, fraction, 3);
    CHECK(!mask[4]);
}

TEST_CASE(FillBoundaryStraddlingCells_RequiredNeighborsIsConfigurable) {
    std::vector<bool> mask = {false, true, false, false, false, false, false, false, false};
    std::vector<float> fraction = {0, 0, 0, 0, 0.20f, 0, 0, 0, 0};
    FillBoundaryStraddlingCells(mask, fraction, 3, /*requiredNeighbors=*/1);
    CHECK(mask[4]);
}

TEST_CASE(FillBoundaryStraddlingCells_MismatchedFractionSizeIsNoop) {
    std::vector<bool> mask = {false, true, false, true, false, false, false, false, false};
    std::vector<float> fraction = {0, 0, 0.05f}; // wrong size
    FillBoundaryStraddlingCells(mask, fraction, 3);
    CHECK(!mask[4]);
}

TEST_CASE(FillEnclosedMaskHoles_AllFalseGridHasNothingEnclosed) {
    // No content at all: every false cell can reach the edge through its
    // all-false neighbors, so nothing changes.
    std::vector<bool> mask(16, false);
    FillEnclosedMaskHoles(mask, 4);
    for (bool cell : mask) CHECK(!cell);
}

namespace {
// 200x200 screen, gridN=5 (40x40px cells -- large relative to the box
// blur's radius-2 window, so only a thin ~19% edge ring of any cell bleeds
// into a differently-colored neighbor, comfortably under
// cellDifferingFraction on its own). Wallpaper and background are both
// plain white -- the clear pixel-count majority, so the robust brightness
// gain stays ~1.0 -- with a solid black 3x3-cell "window" (rows/cols 1-3)
// in the middle, except its dead-center cell (2,2), which happens to be
// white too: a plain dialog background landing on a similarly-colored
// wallpaper patch, fully enclosed by the rest of the window on every side.
std::vector<uint8_t> MakeCoincidentalColorMatchCapture(int width, int height) {
    auto capture = SolidBuffer(width, height, 255, 255, 255);
    FillRect(capture, width, 40, 40, 160, 160, 0, 0, 0);      // 3x3 black window block
    FillRect(capture, width, 80, 80, 120, 120, 255, 255, 255); // center cell (2,2) stays white
    return capture;
}
} // namespace

TEST_CASE(ComputeContentMask_RawResultStillMissesACoincidentalColorMatch) {
    // ComputeContentMask alone is just the raw per-cell diff, so the
    // coincidentally-white center cell is still missed here --
    // FillEnclosedMaskHoles is a separate, explicit step callers chain on
    // afterward (see ComputeContentMask_ThenFillEnclosedMaskHoles_
    // RecoversTheCoincidentalColorMatch below).
    const int width = 200, height = 200, gridN = 5;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = MakeCoincidentalColorMatchCapture(width, height);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);

    CHECK(!mask[2 * gridN + 2]);
    // The other 8 cells of the 3x3 window block are genuinely black -- real
    // content -- and should already be flagged without any hole-filling.
    for (int row = 1; row <= 3; ++row) {
        for (int col = 1; col <= 3; ++col) {
            if (row == 2 && col == 2) continue;
            CHECK(mask[static_cast<size_t>(row) * gridN + col]);
        }
    }
}

TEST_CASE(ComputeContentMask_ThenFillEnclosedMaskHoles_RecoversTheCoincidentalColorMatch) {
    // Same setup as above, but chaining FillEnclosedMaskHoles afterward (the
    // way AppController::Initialize does once it's done using the raw mask
    // for the Spotlight/slideshow suspicion check) recovers the missed
    // center cell too, since it's fully enclosed by flagged neighbors.
    const int width = 200, height = 200, gridN = 5;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = MakeCoincidentalColorMatchCapture(width, height);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    FillEnclosedMaskHoles(mask, gridN);

    CHECK(mask[2 * gridN + 2]);
}

TEST_CASE(RefineBoundaryMask_SkipsCellsWithNoDifferingNeighbor) {
    // A uniform, all-background grid has no boundary cells at all (no cell
    // has a differently-flagged neighbor) -- nothing should be examined, so
    // the result is empty and the mask is untouched.
    const int width = 160, height = 160, gridN = 2;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = SolidBuffer(width, height, 255, 255, 255);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    const auto original = mask;
    auto refinement = RefineBoundaryMask(capture.data(), wallpaper.data(), mask, config);

    CHECK(refinement.cells.empty());
    CHECK(mask == original);
}

TEST_CASE(RefineBoundaryMask_ZeroMaxDepthIsANoop) {
    const int width = 160, height = 160, gridN = 2;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = SolidBuffer(width, height, 255, 255, 255);
    FillRect(capture, width, 0, 0, 80, 80, 0, 0, 0); // cell(0,0) fully content -> cell(0,1) is a boundary cell

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    config.boundaryRefineMaxDepth = 0;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    const auto original = mask;
    auto refinement = RefineBoundaryMask(capture.data(), wallpaper.data(), mask, config);

    CHECK(refinement.cells.empty());
    CHECK(mask == original);
}

TEST_CASE(RefineBoundaryMask_RecoversContentConcentratedInOneQuadrant) {
    // 2x2 grid, 80x80px cells. cell(0,0) is fully black content -> makes
    // cell(0,1) a boundary cell. Within cell(0,1) (otherwise plain
    // wallpaper-matching white), a 20px-wide strip along its full left edge
    // is real content: 1600/6400 = 25% of the whole cell (under the 30%
    // cellDifferingFraction), so the coarse pass alone must miss it -- but
    // that same strip covers half the area of each of the two depth-1
    // quadrants it passes through (800/1600 = 50% each), which clears the
    // 30% bar comfortably at the quadrant level.
    const int width = 160, height = 160, gridN = 2;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = SolidBuffer(width, height, 255, 255, 255);
    FillRect(capture, width, 0, 0, 80, 80, 0, 0, 0);   // cell(0,0): fully content
    FillRect(capture, width, 80, 0, 100, 80, 0, 0, 0); // cell(0,1): left 20px-wide strip only

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    CHECK(mask[0]);       // cell(0,0)
    CHECK(!mask[1]);      // cell(0,1): coarse pass misses the diluted strip

    auto refinement = RefineBoundaryMask(capture.data(), wallpaper.data(), mask, config);
    CHECK(mask[1]); // recovered by refinement
    CHECK(refinement.cells.count(1) == 1);
}

TEST_CASE(RefineBoundaryMask_RecoversContentVisibleOnlyAtTheDeepestLevel) {
    // Same boundary setup, but the real content this time is a single
    // 10x10px patch (one leaf at the default depth-3 resolution, 80/8=10px
    // per leaf) tucked in cell(0,1)'s far corner. Its coverage reads under
    // 30% at *every* coarser level on the way down (whole cell: 100/6400 =
    // 1.6%; its depth-1 quadrant: 100/1600 = 6.25%; its depth-2 sub-quadrant:
    // 100/400 = 25%) and only clears the bar at the leaf itself (100/100 =
    // 100%) -- proving refinement doesn't stop just because an intermediate
    // level's own verdict already happens to agree with a coarser one.
    const int width = 160, height = 160, gridN = 2;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = SolidBuffer(width, height, 255, 255, 255);
    FillRect(capture, width, 0, 0, 80, 80, 0, 0, 0);       // cell(0,0): fully content
    FillRect(capture, width, 150, 0, 160, 10, 0, 0, 0);    // cell(0,1): one far-corner 10x10 leaf

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    CHECK(!mask[1]); // coarse pass misses the tiny, deeply-diluted patch

    auto refinement = RefineBoundaryMask(capture.data(), wallpaper.data(), mask, config);
    CHECK(mask[1]); // recovered anyway -- full-depth recursion, not adaptive early-stopping
}

TEST_CASE(RefineBoundaryMask_NeverDemotesAnAlreadyFlaggedCell) {
    // cell(0,1) is forced true in the mask despite its actual pixels being
    // plain, unremarkable background (every leaf would read "not content" if
    // re-evaluated) -- refinement must leave it true regardless, since it
    // only ever adds detected content (see the function's own doc comment on
    // why: an over-included cell is harmless, the background layer shows the
    // same pixels underneath anyway).
    const int width = 160, height = 160, gridN = 2;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = SolidBuffer(width, height, 255, 255, 255);
    FillRect(capture, width, 0, 0, 80, 80, 0, 0, 0); // cell(0,0): real content, makes cell(0,1) a boundary cell

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    CHECK(!mask[1]);
    mask[1] = true; // force it, as if some earlier pass had already promoted it

    RefineBoundaryMask(capture.data(), wallpaper.data(), mask, config);
    CHECK(mask[1]);
}

TEST_CASE(RefineBoundaryMask_LeafGridMatchesConfiguredDepth) {
    const int width = 160, height = 160, gridN = 2;
    auto wallpaper = SolidBuffer(width, height, 255, 255, 255);
    auto capture = SolidBuffer(width, height, 255, 255, 255);
    FillRect(capture, width, 0, 0, 80, 80, 0, 0, 0);

    ContentMaskConfig config;
    config.screenWidth = width;
    config.screenHeight = height;
    config.gridN = gridN;
    config.boundaryRefineMaxDepth = 2; // 1/16, not the default 1/64
    auto mask = ComputeContentMask(capture.data(), wallpaper.data(), config);
    auto refinement = RefineBoundaryMask(capture.data(), wallpaper.data(), mask, config);

    CHECK_EQ(refinement.leafGrid, 4); // 1 << 2
    CHECK(refinement.cells.count(1) == 1);
    CHECK_EQ(refinement.cells.at(1).size(), static_cast<size_t>(4 * 4));
}
