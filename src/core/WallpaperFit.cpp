#include "WallpaperFit.h"

#include <algorithm>
#include <cstddef>

#include "ContentMask.h" // for ResampleRgba, reused for the Stretch case

namespace core {

namespace {

void FillSolid(uint8_t* dst, int dstW, int dstH, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < dstW * dstH; ++i) {
        dst[i * 4 + 0] = r;
        dst[i * 4 + 1] = g;
        dst[i * 4 + 2] = b;
        dst[i * 4 + 3] = 255;
    }
}

void CopyPixel(const uint8_t* src, int srcX, int srcY, int srcW, uint8_t* dst, size_t dstOffset) {
    const uint8_t* s = src + (static_cast<size_t>(srcY) * srcW + srcX) * 4;
    dst[dstOffset + 0] = s[0];
    dst[dstOffset + 1] = s[1];
    dst[dstOffset + 2] = s[2];
    dst[dstOffset + 3] = 255;
}

} // namespace

void CompositeWallpaper(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH,
                         WallpaperFitMode mode, uint8_t letterboxR, uint8_t letterboxG,
                         uint8_t letterboxB) {
    if (dstW <= 0 || dstH <= 0) return;
    if (srcW <= 0 || srcH <= 0) {
        FillSolid(dst, dstW, dstH, letterboxR, letterboxG, letterboxB);
        return;
    }

    if (mode == WallpaperFitMode::Stretch) {
        ResampleRgba(src, srcW, srcH, dst, dstW, dstH); // whole-canvas stretch, aspect ignored
        return;
    }

    if (mode == WallpaperFitMode::Tile) {
        for (int y = 0; y < dstH; ++y) {
            const int sy = y % srcH;
            for (int x = 0; x < dstW; ++x) {
                const int sx = x % srcW;
                CopyPixel(src, sx, sy, srcW, dst, (static_cast<size_t>(y) * dstW + x) * 4);
            }
        }
        return;
    }

    // Center / Fit / Fill / Span (Span treated as Fill for one screen) all
    // scale-preserving-aspect (Center = scale exactly 1) then place the
    // result centered on the canvas, filling any uncovered border with the
    // letterbox color.
    float scale;
    if (mode == WallpaperFitMode::Center) {
        scale = 1.0f;
    } else if (mode == WallpaperFitMode::Fit) {
        scale = std::min(static_cast<float>(dstW) / srcW, static_cast<float>(dstH) / srcH);
    } else { // Fill, Span
        scale = std::max(static_cast<float>(dstW) / srcW, static_cast<float>(dstH) / srcH);
    }

    const int scaledW = std::max(1, static_cast<int>(srcW * scale + 0.5f));
    const int scaledH = std::max(1, static_cast<int>(srcH * scale + 0.5f));
    const int offsetX = (dstW - scaledW) / 2; // negative when scaledW > dstW (Fill crops)
    const int offsetY = (dstH - scaledH) / 2;

    FillSolid(dst, dstW, dstH, letterboxR, letterboxG, letterboxB);

    const int xStart = std::max(0, offsetX);
    const int xEnd = std::min(dstW, offsetX + scaledW);
    const int yStart = std::max(0, offsetY);
    const int yEnd = std::min(dstH, offsetY + scaledH);
    for (int y = yStart; y < yEnd; ++y) {
        const int sy = std::min(srcH - 1, static_cast<int>((y - offsetY) / scale));
        for (int x = xStart; x < xEnd; ++x) {
            const int sx = std::min(srcW - 1, static_cast<int>((x - offsetX) / scale));
            CopyPixel(src, sx, sy, srcW, dst, (static_cast<size_t>(y) * dstW + x) * 4);
        }
    }
}

} // namespace core
