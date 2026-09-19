#include "WallpaperFit.h"

#include <algorithm>
#include <cstddef>
#include <cstdlib>
#include <vector>

#include "ContentMask.h" // for ResampleRgbaSmooth

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

// Sparse-sampled sum of absolute per-channel differences between a dstW x
// dstH window (top-left at offsetX/offsetY) of `scaled` (scaledW x scaledH)
// and `referenceRgba` (dstW x dstH) -- used only to rank crop-offset
// candidates in CompositeWallpaperAligned below, so it doesn't need to be
// exact, just consistent enough to compare candidates against each other.
long long ScoreCropOffset(const std::vector<uint8_t>& scaled, int scaledW, int offsetX, int offsetY,
                           const uint8_t* referenceRgba, int dstW, int dstH) {
    long long score = 0;
    constexpr int kStep = 7; // coarse stride keeps this cheap; only relative ranking matters
    for (int y = 0; y < dstH; y += kStep) {
        const int sy = y - offsetY;
        const uint8_t* scaledRow = scaled.data() + static_cast<size_t>(sy) * scaledW * 4;
        const uint8_t* refRow = referenceRgba + static_cast<size_t>(y) * dstW * 4;
        for (int x = 0; x < dstW; x += kStep) {
            const int sx = x - offsetX;
            const uint8_t* ps = scaledRow + sx * 4;
            const uint8_t* pr = refRow + x * 4;
            score += std::abs(ps[0] - pr[0]) + std::abs(ps[1] - pr[1]) + std::abs(ps[2] - pr[2]);
        }
    }
    return score;
}

// All integer crop offsets from `minOffset` to `maxOffset` inclusive, at
// `step` apart, always including `maxOffset` itself even if it doesn't fall
// on the stride -- CompositeWallpaperAligned's search needs both extremes
// covered (an off-center crop can sit right at the edge, e.g. a smart-crop
// focus point near the top or bottom of the image) as well as the points in
// between.
std::vector<int> CropOffsetCandidates(int minOffset, int maxOffset, int step) {
    std::vector<int> offsets;
    for (int o = minOffset; o < maxOffset; o += step) offsets.push_back(o);
    offsets.push_back(maxOffset);
    return offsets;
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
        ResampleRgbaSmooth(src, srcW, srcH, dst, dstW, dstH); // whole-canvas stretch, aspect ignored
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

    // Scale the whole image once, smoothly (area average down, linear up), exactly as an image is
    // normally rendered -- picking single source pixels (nearest neighbor) aliases a large, finely
    // textured wallpaper into noise a real screen doesn't show. Scale 1 (Center) needs no resampling.
    std::vector<uint8_t> scaledBuf;
    const uint8_t* scaledPx = src;
    if (scaledW != srcW || scaledH != srcH) {
        scaledBuf.resize(static_cast<size_t>(scaledW) * scaledH * 4);
        ResampleRgbaSmooth(src, srcW, srcH, scaledBuf.data(), scaledW, scaledH);
        scaledPx = scaledBuf.data();
    }

    const int xStart = std::max(0, offsetX);
    const int xEnd = std::min(dstW, offsetX + scaledW);
    const int yStart = std::max(0, offsetY);
    const int yEnd = std::min(dstH, offsetY + scaledH);
    for (int y = yStart; y < yEnd; ++y) {
        for (int x = xStart; x < xEnd; ++x) {
            CopyPixel(scaledPx, x - offsetX, y - offsetY, scaledW, dst, (static_cast<size_t>(y) * dstW + x) * 4);
        }
    }
}

void CompositeWallpaperAligned(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH,
                                WallpaperFitMode mode, uint8_t letterboxR, uint8_t letterboxG,
                                uint8_t letterboxB, const uint8_t* referenceRgba) {
    const bool fillLike = (mode == WallpaperFitMode::Fill || mode == WallpaperFitMode::Span);
    if (!fillLike || !referenceRgba || dstW <= 0 || dstH <= 0 || srcW <= 0 || srcH <= 0) {
        CompositeWallpaper(src, srcW, srcH, dst, dstW, dstH, mode, letterboxR, letterboxG, letterboxB);
        return;
    }

    const float scale = std::max(static_cast<float>(dstW) / srcW, static_cast<float>(dstH) / srcH);
    const int scaledW = std::max(1, static_cast<int>(srcW * scale + 0.5f));
    const int scaledH = std::max(1, static_cast<int>(srcH * scale + 0.5f));

    // Cover-scaling guarantees scaledW >= dstW and scaledH >= dstH, with
    // equality on at least one axis -- if both are already exact, the
    // aspect ratios match and there's no crop position to search.
    if (scaledW <= dstW && scaledH <= dstH) {
        CompositeWallpaper(src, srcW, srcH, dst, dstW, dstH, mode, letterboxR, letterboxG, letterboxB);
        return;
    }

    std::vector<uint8_t> scaled(static_cast<size_t>(scaledW) * scaledH * 4);
    ResampleRgbaSmooth(src, srcW, srcH, scaled.data(), scaledW, scaledH);

    // Valid crop offset range on each axis: offset <= 0 (else the window's
    // left/top edge would read before the scaled image starts) and
    // offset >= dst - scaled (else its right/bottom edge would read past
    // the end). When an axis has no slack (scaled == dst) this collapses to
    // a single valid offset (0), so the search below still runs correctly
    // -- centered isn't assumed, it's just the only option left.
    const int minOffsetX = dstW - scaledW;
    const int minOffsetY = dstH - scaledH;
    constexpr int kOffsetStep = 4; // coarse; content-mask grid cells are much wider than this
    const std::vector<int> offsetsX = CropOffsetCandidates(minOffsetX, 0, kOffsetStep);
    const std::vector<int> offsetsY = CropOffsetCandidates(minOffsetY, 0, kOffsetStep);

    int bestOffsetX = minOffsetX / 2; // centered, used only if nothing scores better
    int bestOffsetY = minOffsetY / 2;
    long long bestScore = -1;
    for (int offsetY : offsetsY) {
        for (int offsetX : offsetsX) {
            const long long score = ScoreCropOffset(scaled, scaledW, offsetX, offsetY, referenceRgba, dstW, dstH);
            if (bestScore < 0 || score < bestScore) {
                bestScore = score;
                bestOffsetX = offsetX;
                bestOffsetY = offsetY;
            }
        }
    }

    // Refine to the exact pixel: the coarse search moved in steps of kOffsetStep, and on a finely
    // textured wallpaper even a 1-2px misalignment makes the reference differ from the real screen
    // almost everywhere.
    {
        long long refined = bestScore;
        const int cx = bestOffsetX, cy = bestOffsetY;
        for (int oy = std::max(minOffsetY, cy - (kOffsetStep - 1)); oy <= std::min(0, cy + (kOffsetStep - 1)); ++oy) {
            for (int ox = std::max(minOffsetX, cx - (kOffsetStep - 1)); ox <= std::min(0, cx + (kOffsetStep - 1)); ++ox) {
                const long long score = ScoreCropOffset(scaled, scaledW, ox, oy, referenceRgba, dstW, dstH);
                if (score < refined) {
                    refined = score;
                    bestOffsetX = ox;
                    bestOffsetY = oy;
                }
            }
        }
    }

    for (int y = 0; y < dstH; ++y) {
        const int sy = y - bestOffsetY;
        const uint8_t* scaledRow = scaled.data() + static_cast<size_t>(sy) * scaledW * 4;
        uint8_t* dstRow = dst + static_cast<size_t>(y) * dstW * 4;
        for (int x = 0; x < dstW; ++x) {
            const int sx = x - bestOffsetX;
            std::copy(scaledRow + sx * 4, scaledRow + sx * 4 + 4, dstRow + x * 4);
        }
    }
}

} // namespace core
