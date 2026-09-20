#include "test_framework.h"

#include <algorithm>
#include <cstdlib>

#include "../src/core/ContentMask.h"
#include "../src/core/WallpaperFit.h"

using core::CompositeWallpaper;
using core::CompositeWallpaperAligned;
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

TEST_CASE(CompositeWallpaperAligned_FindsOffCenterCropMatchingReference) {
    // 100x200 source (tall), 100x50 canvas under Fill: scale = max(100/100,
    // 50/200) = 1, so scaledW=100 (no horizontal slack) but scaledH=200
    // against a 50px-tall canvas -- a big vertical crop, same shape of
    // mismatch as the real ultrawide-screen/4K-wallpaper case that motivated
    // this function. Each source row gets a distinct color (row index as
    // red channel) so any crop position is unambiguous to check.
    const int srcW = 100, srcH = 200, dstW = 100, dstH = 50;
    std::vector<uint8_t> src(static_cast<size_t>(srcW) * srcH * 4);
    for (int y = 0; y < srcH; ++y) {
        for (int x = 0; x < srcW; ++x) {
            uint8_t* p = &src[(static_cast<size_t>(y) * srcW + x) * 4];
            p[0] = static_cast<uint8_t>(y);
            p[1] = 0;
            p[2] = 0;
            p[3] = 255;
        }
    }

    // The "real capture" is rows [122, 172) of the source -- deliberately
    // not centered (a naive centered crop would instead pick [75, 125)).
    // 122 is chosen to land exactly on the search's coarse stride (see
    // kOffsetStep in CompositeWallpaperAligned) so this test isn't sensitive
    // to that implementation detail.
    constexpr int kTrueOffsetRow = 122;
    std::vector<uint8_t> reference(static_cast<size_t>(dstW) * dstH * 4);
    for (int y = 0; y < dstH; ++y) {
        std::copy(src.begin() + (static_cast<size_t>(kTrueOffsetRow + y) * srcW) * 4,
                  src.begin() + (static_cast<size_t>(kTrueOffsetRow + y) * srcW + srcW) * 4,
                  reference.begin() + static_cast<size_t>(y) * dstW * 4);
    }

    std::vector<uint8_t> dst(static_cast<size_t>(dstW) * dstH * 4, 0);
    CompositeWallpaperAligned(src.data(), srcW, srcH, dst.data(), dstW, dstH, WallpaperFitMode::Fill, 0, 0, 0,
                               reference.data());

    // The aligned composite should match the true (off-center) crop, not
    // the naive centered one.
    CHECK_EQ(PixelAt(dst, dstW, 0, 0)[0], static_cast<uint8_t>(kTrueOffsetRow));
    CHECK_EQ(PixelAt(dst, dstW, 0, dstH - 1)[0], static_cast<uint8_t>(kTrueOffsetRow + dstH - 1));
}

TEST_CASE(CompositeWallpaperAligned_FallsBackWhenAspectRatiosAlreadyMatch) {
    // No crop slack on either axis -- should behave exactly like plain
    // CompositeWallpaper regardless of what `referenceRgba` says.
    auto src = SolidBuffer(4, 4, 5, 6, 7);
    auto reference = SolidBuffer(4, 4, 200, 200, 200); // deliberately different, should be ignored
    std::vector<uint8_t> dst(4 * 4 * 4, 0);
    CompositeWallpaperAligned(src.data(), 4, 4, dst.data(), 4, 4, WallpaperFitMode::Fill, 0, 0, 0,
                               reference.data());
    CHECK_EQ(PixelAt(dst, 4, 2, 2)[0], static_cast<uint8_t>(5));
    CHECK_EQ(PixelAt(dst, 4, 2, 2)[1], static_cast<uint8_t>(6));
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

// ---------------------------------------------------------------------------
// Smooth resampling and 1px alignment (a finely textured wallpaper used to make the diff flag
// nearly the whole screen, because nearest-neighbor scaling aliases it and the crop offset was only
// searched in steps of 4px).

namespace {
std::vector<uint8_t> NoiseImage(int w, int h, uint32_t seed) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    uint32_t s = seed;
    for (size_t i = 0; i < px.size(); i += 4) {
        s = s * 1664525u + 1013904223u;
        px[i] = static_cast<uint8_t>(s >> 24);
        s = s * 1664525u + 1013904223u;
        px[i + 1] = static_cast<uint8_t>(s >> 24);
        s = s * 1664525u + 1013904223u;
        px[i + 2] = static_cast<uint8_t>(s >> 24);
        px[i + 3] = 255;
    }
    return px;
}
// High-contrast blobs of `block` px (like a photo's speckle): neighboring pixels correlate, so being
// 1-3px off the right alignment scores better than being far off -- a real texture's landscape.
std::vector<uint8_t> BlobImage(int w, int h, int block, uint32_t seed) {
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);
    const int bw = (w + block - 1) / block;
    std::vector<uint8_t> level(static_cast<size_t>(bw) * ((h + block - 1) / block));
    uint32_t s = seed;
    for (auto& l : level) {
        s = s * 1664525u + 1013904223u;
        l = (s >> 24) & 1 ? 235 : 20;
    }
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
            p[0] = p[1] = p[2] = level[static_cast<size_t>(y / block) * bw + x / block];
            p[3] = 255;
        }
    }
    return px;
}
} // namespace

TEST_CASE(ResampleRgbaSmooth_DownscaleAveragesAndUpscaleInterpolates) {
    // 4x4 checkerboard of 0/254 downscaled to 2x2: every output pixel is the average of a 2x2 block.
    std::vector<uint8_t> src(4 * 4 * 4);
    for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
            const uint8_t v = ((x + y) % 2) ? 254 : 0;
            uint8_t* p = &src[(static_cast<size_t>(y) * 4 + x) * 4];
            p[0] = p[1] = p[2] = v;
            p[3] = 255;
        }
    }
    std::vector<uint8_t> dst(2 * 2 * 4);
    core::ResampleRgbaSmooth(src.data(), 4, 4, dst.data(), 2, 2);
    for (int i = 0; i < 4; ++i) CHECK_EQ(static_cast<int>(dst[i * 4]), 127);

    // A constant image stays constant under any resize, and identity keeps every pixel.
    std::vector<uint8_t> flat(5 * 3 * 4, 90);
    std::vector<uint8_t> big(11 * 7 * 4);
    core::ResampleRgbaSmooth(flat.data(), 5, 3, big.data(), 11, 7);
    for (size_t i = 0; i < big.size(); i += 4) CHECK_EQ(static_cast<int>(big[i]), 90);
    const auto noise = NoiseImage(16, 9, 5);
    std::vector<uint8_t> same(noise.size());
    core::ResampleRgbaSmooth(noise.data(), 16, 9, same.data(), 16, 9);
    CHECK(same == noise);
}

TEST_CASE(CompositeWallpaper_FillDownscaleIsSmoothNotAliased) {
    // A noisy 800x600 image filled into 400x300 (exactly half): each output pixel must be the average
    // of a 2x2 source block, not one picked pixel.
    const auto src = NoiseImage(800, 600, 11);
    std::vector<uint8_t> dst(400 * 300 * 4);
    core::CompositeWallpaper(src.data(), 800, 600, dst.data(), 400, 300, core::WallpaperFitMode::Fill, 0, 0, 0);
    int maxErr = 0;
    for (int y = 0; y < 300; y += 7) {
        for (int x = 0; x < 400; x += 7) {
            int sum = 0;
            for (int dy = 0; dy < 2; ++dy) {
                for (int dx = 0; dx < 2; ++dx) sum += src[(static_cast<size_t>(2 * y + dy) * 800 + 2 * x + dx) * 4];
            }
            maxErr = std::max(maxErr, std::abs(sum / 4 - static_cast<int>(dst[(static_cast<size_t>(y) * 400 + x) * 4])));
        }
    }
    CHECK(maxErr <= 1);
}

TEST_CASE(CompositeWallpaperAligned_RecoversAnOffsetThatIsNotAMultipleOfTheCoarseStep) {
    // Source 300x150 filled into 200x100 -> scaled 200x100? use an aspect mismatch so there is crop slack.
    const int srcW = 600, srcH = 300, dstW = 200, dstH = 100;
    const auto src = BlobImage(srcW, srcH, 9, 21);
    // Cover-scale = max(200/600, 100/300) = 1/3 -> scaled 200x100: no slack. Make the canvas narrower.
    const int dW = 150;
    const float scale = std::max(static_cast<float>(dW) / srcW, static_cast<float>(dstH) / srcH);
    const int scaledW = static_cast<int>(srcW * scale + 0.5f), scaledH = static_cast<int>(srcH * scale + 0.5f);
    std::vector<uint8_t> scaled(static_cast<size_t>(scaledW) * scaledH * 4);
    core::ResampleRgbaSmooth(src.data(), srcW, srcH, scaled.data(), scaledW, scaledH);
    (void)dstW;
    const int cropX = 7; // 7 is not a multiple of the coarse step (4)
    std::vector<uint8_t> reference(static_cast<size_t>(dW) * dstH * 4);
    for (int y = 0; y < dstH; ++y) {
        for (int x = 0; x < dW; ++x) {
            const uint8_t* p = &scaled[(static_cast<size_t>(y) * scaledW + x + cropX) * 4];
            std::copy(p, p + 4, &reference[(static_cast<size_t>(y) * dW + x) * 4]);
        }
    }
    std::vector<uint8_t> out(reference.size());
    core::CompositeWallpaperAligned(src.data(), srcW, srcH, out.data(), dW, dstH, core::WallpaperFitMode::Fill, 0, 0, 0,
                                     reference.data());
    CHECK(out == reference); // exact: the search found offset -7, not the nearest multiple of 4
}
