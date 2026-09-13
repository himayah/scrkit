#include "ContentMask.h"

#include <algorithm>
#include <cstdlib>
#include <vector>

namespace core {

namespace {

// Bounds how far the brightness-gain correction (see ContentMask.h) is
// allowed to shift the wallpaper reference -- wide enough to cover real
// HDR tone-mapping offsets (~1.7x observed), narrow enough that a
// pathological input (e.g. a near-black wallpaper) can't blow the scaled
// values up into meaningless noise.
constexpr float kMinBrightnessGain = 0.25f;
constexpr float kMaxBrightnessGain = 4.0f;

// Box-blur radius applied to both images before diffing (see
// ComputeContentMask's doc comment) -- a (2*kBoxBlurRadius+1) square
// window. Real-machine testing (comparing several radii, and a true
// Gaussian blur, against saved debug_capture.bmp/debug_wallpaper.bmp) found
// 3x3 through 9x9 boxes all gave essentially the same result, so this isn't
// a sensitive knob -- 5x5 is a reasonable middle ground.
constexpr int kBoxBlurRadius = 2;

// Per-pixel luminance ratio (capture/wallpaper) at one pixel, used to
// derive the brightness gain below via its *median* across the whole
// image rather than a single ratio-of-sums -- real-machine feedback found
// the sum-based ratio skewed badly whenever a large chunk of the screen
// was genuinely different content: with a large window open, capture's
// overall sum came out darker than the wallpaper's, so the ratio-of-sums
// gain (1.20x) *overcorrected* the wallpaper -- including the parts of it
// that were plain background and needed no correction -- and nearly
// doubled the number of falsely-flagged cells (49.3% vs. 24.3% without any
// gain at all) instead of helping. The median of a large number of
// per-pixel ratios is robust to that as long as background pixels remain
// the majority of the screen, which they normally are; a real global
// offset (e.g. HDR tone-mapping) still shows up in the median as usual
// since it shifts the vast majority of those per-pixel ratios together.
float ComputeRobustBrightnessGain(const uint8_t* captureRgba, const uint8_t* wallpaperRgba, size_t pixelCount) {
    std::vector<float> ratios;
    ratios.reserve(pixelCount);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* c = captureRgba + i * 4;
        const uint8_t* w = wallpaperRgba + i * 4;
        const int wLum = w[0] + w[1] + w[2];
        if (wLum < 30) continue; // near-black denominators make the ratio meaningless noise
        const int cLum = c[0] + c[1] + c[2];
        ratios.push_back(static_cast<float>(cLum) / static_cast<float>(wLum));
    }
    if (ratios.empty()) return 1.0f;
    auto mid = ratios.begin() + ratios.size() / 2;
    std::nth_element(ratios.begin(), mid, ratios.end());
    return std::clamp(*mid, kMinBrightnessGain, kMaxBrightnessGain);
}

// Separable box blur (a (2*radius+1) square window, edge-clamped) over the
// RGB channels of an RGBA8 buffer -- see ComputeContentMask's doc comment
// for why this runs before diffing. Alpha is always written opaque;
// nothing downstream of the diff below reads it.
void BoxBlurRgb(const uint8_t* src, int width, int height, int radius, std::vector<uint8_t>& out) {
    out.assign(static_cast<size_t>(width) * height * 4, 255);
    std::vector<float> horizontal(static_cast<size_t>(width) * height * 3);
    const int windowSize = radius * 2 + 1;

    for (int y = 0; y < height; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f;
            for (int dx = -radius; dx <= radius; ++dx) {
                const int sx = std::clamp(x + dx, 0, width - 1);
                const uint8_t* p = row + sx * 4;
                sum0 += p[0];
                sum1 += p[1];
                sum2 += p[2];
            }
            float* h = &horizontal[(static_cast<size_t>(y) * width + x) * 3];
            h[0] = sum0 / windowSize;
            h[1] = sum1 / windowSize;
            h[2] = sum2 / windowSize;
        }
    }

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f;
            for (int dy = -radius; dy <= radius; ++dy) {
                const int sy = std::clamp(y + dy, 0, height - 1);
                const float* h = &horizontal[(static_cast<size_t>(sy) * width + x) * 3];
                sum0 += h[0];
                sum1 += h[1];
                sum2 += h[2];
            }
            uint8_t* o = &out[(static_cast<size_t>(y) * width + x) * 4];
            o[0] = static_cast<uint8_t>(sum0 / windowSize + 0.5f);
            o[1] = static_cast<uint8_t>(sum1 / windowSize + 0.5f);
            o[2] = static_cast<uint8_t>(sum2 / windowSize + 0.5f);
        }
    }
}

} // namespace

void ResampleRgba(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH) {
    if (srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0) return;

    for (int y = 0; y < dstH; ++y) {
        const int srcY = std::min(srcH - 1, (y * srcH) / dstH);
        const uint8_t* srcRow = src + static_cast<size_t>(srcY) * srcW * 4;
        uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
        for (int x = 0; x < dstW; ++x) {
            const int srcX = std::min(srcW - 1, (x * srcW) / dstW);
            std::copy(srcRow + srcX * 4, srcRow + srcX * 4 + 4, dstRow + x * 4);
        }
    }
}

std::vector<bool> ComputeContentMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba,
                                      const ContentMaskConfig& config) {
    const int n = std::max(1, config.gridN);
    const int width = config.screenWidth;
    const int height = config.screenHeight;
    std::vector<bool> mask(static_cast<size_t>(n) * n, false);
    if (width <= 0 || height <= 0) return mask;

    const size_t pixelCount = static_cast<size_t>(width) * height;
    const float gain = ComputeRobustBrightnessGain(captureRgba, wallpaperRgba, pixelCount);

    std::vector<uint8_t> gainedWallpaper(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* w = wallpaperRgba + i * 4;
        uint8_t* g = &gainedWallpaper[i * 4];
        g[0] = static_cast<uint8_t>(std::clamp(w[0] * gain + 0.5f, 0.0f, 255.0f));
        g[1] = static_cast<uint8_t>(std::clamp(w[1] * gain + 0.5f, 0.0f, 255.0f));
        g[2] = static_cast<uint8_t>(std::clamp(w[2] * gain + 0.5f, 0.0f, 255.0f));
        g[3] = 255;
    }

    // Blur both sides equally before diffing: an independently decoded and
    // resized wallpaper can never reproduce the real screen's own rendering
    // pixel-for-pixel, and the resulting edge/anti-aliasing noise
    // concentrates in detailed textures -- real-machine feedback: a
    // snow-capped mountain wallpaper kept getting swept into the suction
    // effect even after the brightness-gain and crop-alignment fixes, while
    // smooth areas like sky were already fine ("空の部分は対象外になって
    // いるが山の部分は対象になってしまっている"). That noise is inherently
    // a sub-pixel-scale disagreement, so blurring both images by the same
    // amount suppresses it on both sides equally while leaving real content
    // (icons, taskbar, open windows -- all much larger and starkly
    // different) essentially untouched.
    std::vector<uint8_t> blurredCapture;
    std::vector<uint8_t> blurredWallpaper;
    BoxBlurRgb(captureRgba, width, height, kBoxBlurRadius, blurredCapture);
    BoxBlurRgb(gainedWallpaper.data(), width, height, kBoxBlurRadius, blurredWallpaper);

    size_t cellIndex = 0;
    for (int row = 0; row < n; ++row) {
        const int y0 = (row * height) / n;
        const int y1 = ((row + 1) * height) / n;
        for (int col = 0; col < n; ++col, ++cellIndex) {
            const int x0 = (col * width) / n;
            const int x1 = ((col + 1) * width) / n;

            long long differing = 0;
            long long total = 0;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* captureRow = blurredCapture.data() + static_cast<size_t>(y) * width * 4;
                const uint8_t* wallpaperRow = blurredWallpaper.data() + static_cast<size_t>(y) * width * 4;
                for (int x = x0; x < x1; ++x) {
                    const uint8_t* c = captureRow + x * 4;
                    const uint8_t* w = wallpaperRow + x * 4;
                    const int diff = std::abs(c[0] - w[0]) + std::abs(c[1] - w[1]) + std::abs(c[2] - w[2]);
                    if (diff > config.pixelDiffThreshold) ++differing;
                    ++total;
                }
            }

            if (total > 0 &&
                static_cast<float>(differing) / static_cast<float>(total) >= config.cellDifferingFraction) {
                mask[cellIndex] = true;
            }
        }
    }
    return mask;
}

} // namespace core
