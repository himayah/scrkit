#include "ContentMask.h"

#include <algorithm>
#include <cstdlib>

namespace core {

namespace {

// Bounds how far the brightness-gain correction (see ContentMask.h) is
// allowed to shift the wallpaper reference -- wide enough to cover real
// HDR tone-mapping offsets (~1.7x observed), narrow enough that a
// pathological input (e.g. a near-black wallpaper) can't blow the scaled
// values up into meaningless noise.
constexpr float kMinBrightnessGain = 0.25f;
constexpr float kMaxBrightnessGain = 4.0f;

// Mean R+G+B (not divided by pixel count) over the whole buffer, used only
// to derive the gain ratio -- the absolute scale cancels out in that ratio,
// so there's no need to finish the division into a true per-pixel mean.
unsigned long long SumRgb(const uint8_t* rgba, size_t pixelCount) {
    unsigned long long sum = 0;
    for (size_t i = 0; i < pixelCount; ++i) {
        const uint8_t* p = rgba + i * 4;
        sum += p[0] + p[1] + p[2];
    }
    return sum;
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
    const unsigned long long wallpaperSum = SumRgb(wallpaperRgba, pixelCount);
    float gain = 1.0f;
    if (wallpaperSum > 0) {
        const unsigned long long captureSum = SumRgb(captureRgba, pixelCount);
        gain = std::clamp(static_cast<float>(captureSum) / static_cast<float>(wallpaperSum), kMinBrightnessGain,
                           kMaxBrightnessGain);
    }

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
                const uint8_t* captureRow = captureRgba + static_cast<size_t>(y) * width * 4;
                const uint8_t* wallpaperRow = wallpaperRgba + static_cast<size_t>(y) * width * 4;
                for (int x = x0; x < x1; ++x) {
                    const uint8_t* c = captureRow + x * 4;
                    const uint8_t* w = wallpaperRow + x * 4;
                    const int wr = std::clamp(static_cast<int>(w[0] * gain + 0.5f), 0, 255);
                    const int wg = std::clamp(static_cast<int>(w[1] * gain + 0.5f), 0, 255);
                    const int wb = std::clamp(static_cast<int>(w[2] * gain + 0.5f), 0, 255);
                    const int diff = std::abs(c[0] - wr) + std::abs(c[1] - wg) + std::abs(c[2] - wb);
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
