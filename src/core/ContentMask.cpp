#include "ContentMask.h"

#include <algorithm>
#include <cmath>
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

// Shared tail of the per-cell decision (ComputeContentMask's inner loop and
// RefineBoundaryMask's region evaluation both call this): the color-diff
// fraction check, falling back to the texture-flatness margin check when it
// doesn't clear the bar on its own. Factored out once so the two never drift
// apart -- see ContentMask.h's note on core::PixelToGridIndex for why that
// matters in this codebase specifically.
bool DecideFlagged(float differingFraction, double captureVar, double wallpaperVar,
                    const ContentMaskConfig& config) {
    if (differingFraction >= config.cellDifferingFraction) return true;
    const double margin = std::sqrt(wallpaperVar) - std::sqrt(captureVar);
    return margin >= config.textureFlatnessMargin;
}

// Gain-corrected wallpaper pixel at (x,y), matching ComputeContentMask's
// `gainedWallpaper` construction exactly (gain applied then clamped, per
// pixel, before any blurring) -- order matters since clamping is nonlinear.
inline void GainedWallpaperPixel(const uint8_t* wallpaperRgba, int width, int x, int y, float gain, float out[3]) {
    const uint8_t* w = wallpaperRgba + (static_cast<size_t>(y) * width + x) * 4;
    out[0] = std::clamp(w[0] * gain, 0.0f, 255.0f);
    out[1] = std::clamp(w[1] * gain, 0.0f, 255.0f);
    out[2] = std::clamp(w[2] * gain, 0.0f, 255.0f);
}

// On-demand box-blurred value of one pixel, sampling directly from the
// full-resolution source with edge clamping at the *true* image bounds --
// matches BoxBlurRgb's own clamping exactly, so a refined sub-region's
// decision stays consistent with the coarse pass at a shared cell boundary.
// Used instead of a precomputed full-image blurred buffer because
// RefineBoundaryMask only ever touches a small fraction of the image (the
// cells actually on a content/background boundary), so blurring the whole
// image up front here would be wasted work.
void BlurredPixel(const uint8_t* rgba, int width, int height, int x, int y, int radius, bool applyGain, float gain,
                   float out[3]) {
    float sum[3] = {0.0f, 0.0f, 0.0f};
    int count = 0;
    for (int dy = -radius; dy <= radius; ++dy) {
        const int sy = std::clamp(y + dy, 0, height - 1);
        for (int dx = -radius; dx <= radius; ++dx) {
            const int sx = std::clamp(x + dx, 0, width - 1);
            float p[3];
            if (applyGain) {
                GainedWallpaperPixel(rgba, width, sx, sy, gain, p);
            } else {
                const uint8_t* c = rgba + (static_cast<size_t>(sy) * width + sx) * 4;
                p[0] = c[0];
                p[1] = c[1];
                p[2] = c[2];
            }
            sum[0] += p[0];
            sum[1] += p[1];
            sum[2] += p[2];
            ++count;
        }
    }
    out[0] = sum[0] / count;
    out[1] = sum[1] / count;
    out[2] = sum[2] / count;
}

// Same decision ComputeContentMask makes per grid cell, generalized to an
// arbitrary [x0,x1) x [y0,y1) pixel rectangle (a boundary cell's sub-region
// during recursive refinement).
bool EvaluateRegionFlagged(const uint8_t* captureRgba, const uint8_t* wallpaperRgba, int width, int height,
                            int x0, int x1, int y0, int y1, float gain, const ContentMaskConfig& config) {
    long long differing = 0, total = 0;
    double captureSum = 0.0, captureSumSq = 0.0, wallpaperSum = 0.0, wallpaperSumSq = 0.0;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            float bc[3], bw[3];
            BlurredPixel(captureRgba, width, height, x, y, kBoxBlurRadius, false, gain, bc);
            BlurredPixel(wallpaperRgba, width, height, x, y, kBoxBlurRadius, true, gain, bw);
            const int diff = static_cast<int>(std::abs(bc[0] - bw[0]) + std::abs(bc[1] - bw[1]) + std::abs(bc[2] - bw[2]));
            if (diff > config.pixelDiffThreshold) ++differing;
            ++total;

            const uint8_t* rc = captureRgba + (static_cast<size_t>(y) * width + x) * 4;
            float rw[3];
            GainedWallpaperPixel(wallpaperRgba, width, x, y, gain, rw);
            for (int ch = 0; ch < 3; ++ch) {
                captureSum += rc[ch];
                captureSumSq += static_cast<double>(rc[ch]) * rc[ch];
                wallpaperSum += rw[ch];
                wallpaperSumSq += static_cast<double>(rw[ch]) * rw[ch];
            }
        }
    }
    if (total == 0) return false;
    const float differingFraction = static_cast<float>(differing) / static_cast<float>(total);
    const double sampleCount = static_cast<double>(total) * 3.0;
    const double captureMean = captureSum / sampleCount;
    const double wallpaperMean = wallpaperSum / sampleCount;
    const double captureVar = std::max(0.0, captureSumSq / sampleCount - captureMean * captureMean);
    const double wallpaperVar = std::max(0.0, wallpaperSumSq / sampleCount - wallpaperMean * wallpaperMean);
    return DecideFlagged(differingFraction, captureVar, wallpaperVar, config);
}

// True if the center of the pixel region [x0,x1) x [y0,y1) lies inside any of
// `rects` (half-open). Compared in doubled coordinates so odd-sized regions
// need no rounding. Shared by ForceRectsIntoMask (whole grid cells) and
// RefineQuadrant (sub-regions of a boundary cell) so a rectangle's edge means
// the same thing at every resolution.
bool RegionCenterInAnyRect(const std::vector<PixelRect>& rects, int x0, int x1, int y0, int y1) {
    const int cx2 = x0 + x1;
    const int cy2 = y0 + y1;
    for (const PixelRect& r : rects) {
        if (cx2 >= 2 * r.left && cx2 < 2 * r.right && cy2 >= 2 * r.top && cy2 < 2 * r.bottom) return true;
    }
    return false;
}

// Recursively subdivides one quadrant of a boundary cell down to maxDepth and evaluates only the
// leaves (regions too small to subdivide further count as leaves and fill their whole span).
// Nothing stops early: content diluted at every coarser scale but concentrated at the finest one
// must still be found. Coarser levels are deliberately NOT evaluated: an intermediate region's
// verdict is not needed and, on a textured wallpaper, is noise (a stray 30%-differing quadrant
// says nothing about a real edge). `forcedLeaves` marks leaves that are content because a trusted
// window rectangle covers them, as opposed to leaves that merely *look* different: only the latter
// are subject to the noise filter in RefineBoundaryMask.
void RefineQuadrant(const uint8_t* captureRgba, const uint8_t* wallpaperRgba, int width, int height, float gain,
                     const ContentMaskConfig& config, int x0, int x1, int y0, int y1, int depth, int maxDepth,
                     int leafGrid, int leafX0, int leafY0, int leafSpan, std::vector<bool>& leaves,
                     std::vector<bool>& forcedLeaves, const std::vector<PixelRect>& forcedRects) {
    const bool canSubdivide = depth < maxDepth && (x1 - x0) >= 2 && (y1 - y0) >= 2;
    if (!canSubdivide) {
        const bool forced = RegionCenterInAnyRect(forcedRects, x0, x1, y0, y1);
        const bool flagged =
            forced || EvaluateRegionFlagged(captureRgba, wallpaperRgba, width, height, x0, x1, y0, y1, gain, config);
        for (int ly = leafY0; ly < leafY0 + leafSpan; ++ly) {
            for (int lx = leafX0; lx < leafX0 + leafSpan; ++lx) {
                leaves[static_cast<size_t>(ly) * leafGrid + lx] = flagged;
                forcedLeaves[static_cast<size_t>(ly) * leafGrid + lx] = forced;
            }
        }
        return;
    }

    const int xm = (x0 + x1) / 2;
    const int ym = (y0 + y1) / 2;
    const int childSpan = leafSpan / 2;
    RefineQuadrant(captureRgba, wallpaperRgba, width, height, gain, config, x0, xm, y0, ym, depth + 1, maxDepth,
                   leafGrid, leafX0, leafY0, childSpan, leaves, forcedLeaves, forcedRects);
    RefineQuadrant(captureRgba, wallpaperRgba, width, height, gain, config, xm, x1, y0, ym, depth + 1, maxDepth,
                   leafGrid, leafX0 + childSpan, leafY0, childSpan, leaves, forcedLeaves, forcedRects);
    RefineQuadrant(captureRgba, wallpaperRgba, width, height, gain, config, x0, xm, ym, y1, depth + 1, maxDepth,
                   leafGrid, leafX0, leafY0 + childSpan, childSpan, leaves, forcedLeaves, forcedRects);
    RefineQuadrant(captureRgba, wallpaperRgba, width, height, gain, config, xm, x1, ym, y1, depth + 1, maxDepth,
                   leafGrid, leafX0 + childSpan, leafY0 + childSpan, childSpan, leaves, forcedLeaves, forcedRects);
}

// Clears every 4-connected group of set leaves smaller than `minSize`. A real edge (a window
// border, an icon's outline) lights up a connected run of leaves; a textured wallpaper produces
// isolated random ones, which would otherwise make speckle opaque and, worse, get a whole cell
// promoted to content.
void DropSmallLeafGroups(std::vector<bool>& leaves, int leafGrid, int minSize) {
    if (minSize <= 1) return;
    std::vector<bool> seen(leaves.size(), false);
    std::vector<size_t> group, stack;
    for (size_t start = 0; start < leaves.size(); ++start) {
        if (!leaves[start] || seen[start]) continue;
        group.clear();
        stack.assign(1, start);
        seen[start] = true;
        while (!stack.empty()) {
            const size_t cur = stack.back();
            stack.pop_back();
            group.push_back(cur);
            const int cx = static_cast<int>(cur % leafGrid), cy = static_cast<int>(cur / leafGrid);
            const int nx[4] = {cx - 1, cx + 1, cx, cx};
            const int ny[4] = {cy, cy, cy - 1, cy + 1};
            for (int k = 0; k < 4; ++k) {
                if (nx[k] < 0 || ny[k] < 0 || nx[k] >= leafGrid || ny[k] >= leafGrid) continue;
                const size_t n = static_cast<size_t>(ny[k]) * leafGrid + nx[k];
                if (leaves[n] && !seen[n]) {
                    seen[n] = true;
                    stack.push_back(n);
                }
            }
        }
        if (static_cast<int>(group.size()) < minSize) {
            for (size_t g : group) leaves[g] = false;
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
                                      const ContentMaskConfig& config,
                                      std::vector<float>* outDifferingFraction) {
    const int n = std::max(1, config.gridN);
    const int width = config.screenWidth;
    const int height = config.screenHeight;
    std::vector<bool> mask(static_cast<size_t>(n) * n, false);
    if (outDifferingFraction) outDifferingFraction->assign(static_cast<size_t>(n) * n, 0.0f);
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
            // Pooled (R,G,B all counted as separate samples) sum/sum-of-
            // squares for the *raw* (unblurred) capture and gain-corrected
            // wallpaper -- the textureFlatnessMargin fallback below needs
            // real per-pixel texture, which the blur above deliberately
            // smooths away.
            double captureSum = 0.0, captureSumSq = 0.0;
            double wallpaperSum = 0.0, wallpaperSumSq = 0.0;
            for (int y = y0; y < y1; ++y) {
                const uint8_t* captureRow = blurredCapture.data() + static_cast<size_t>(y) * width * 4;
                const uint8_t* wallpaperRow = blurredWallpaper.data() + static_cast<size_t>(y) * width * 4;
                const uint8_t* rawCaptureRow = captureRgba + static_cast<size_t>(y) * width * 4;
                const uint8_t* rawWallpaperRow = gainedWallpaper.data() + static_cast<size_t>(y) * width * 4;
                for (int x = x0; x < x1; ++x) {
                    const uint8_t* c = captureRow + x * 4;
                    const uint8_t* w = wallpaperRow + x * 4;
                    const int diff = std::abs(c[0] - w[0]) + std::abs(c[1] - w[1]) + std::abs(c[2] - w[2]);
                    if (diff > config.pixelDiffThreshold) ++differing;
                    ++total;

                    const uint8_t* rc = rawCaptureRow + x * 4;
                    const uint8_t* rw = rawWallpaperRow + x * 4;
                    for (int ch = 0; ch < 3; ++ch) {
                        captureSum += rc[ch];
                        captureSumSq += static_cast<double>(rc[ch]) * rc[ch];
                        wallpaperSum += rw[ch];
                        wallpaperSumSq += static_cast<double>(rw[ch]) * rw[ch];
                    }
                }
            }

            const float differingFraction = total > 0 ? static_cast<float>(differing) / static_cast<float>(total) : 0.0f;
            if (outDifferingFraction) (*outDifferingFraction)[cellIndex] = differingFraction;

            bool flagged = false;
            if (total > 0) {
                const double sampleCount = static_cast<double>(total) * 3.0;
                const double captureMean = captureSum / sampleCount;
                const double wallpaperMean = wallpaperSum / sampleCount;
                const double captureVar = std::max(0.0, captureSumSq / sampleCount - captureMean * captureMean);
                const double wallpaperVar =
                    std::max(0.0, wallpaperSumSq / sampleCount - wallpaperMean * wallpaperMean);
                flagged = DecideFlagged(differingFraction, captureVar, wallpaperVar, config);
            }

            mask[cellIndex] = flagged;
        }
    }
    return mask;
}

bool IsContentMaskSuspicious(const std::vector<bool>& mask, double threshold) {
    if (mask.empty()) return false;
    const size_t flagged = static_cast<size_t>(std::count(mask.begin(), mask.end(), true));
    return static_cast<double>(flagged) / static_cast<double>(mask.size()) >= threshold;
}

void FillEnclosedMaskHoles(std::vector<bool>& mask, int gridN) {
    if (gridN <= 0 || mask.size() != static_cast<size_t>(gridN) * gridN) return;

    // Flood fill from every `false` cell on the grid's outer edge, over
    // 4-connected `false` neighbors only (never crossing a `true`/content
    // cell). Anything left unreached afterwards is a `false` cell that's
    // fully boxed in by content -- an enclosed hole -- and gets promoted to
    // `true`.
    std::vector<bool> reachesEdge(mask.size(), false);
    std::vector<int> stack;
    stack.reserve(mask.size());
    auto visit = [&](int row, int col) {
        if (row < 0 || row >= gridN || col < 0 || col >= gridN) return;
        const size_t idx = static_cast<size_t>(row) * gridN + col;
        if (mask[idx] || reachesEdge[idx]) return;
        reachesEdge[idx] = true;
        stack.push_back(static_cast<int>(idx));
    };
    for (int col = 0; col < gridN; ++col) {
        visit(0, col);
        visit(gridN - 1, col);
    }
    for (int row = 0; row < gridN; ++row) {
        visit(row, 0);
        visit(row, gridN - 1);
    }
    while (!stack.empty()) {
        const int idx = stack.back();
        stack.pop_back();
        const int row = idx / gridN;
        const int col = idx % gridN;
        visit(row - 1, col);
        visit(row + 1, col);
        visit(row, col - 1);
        visit(row, col + 1);
    }

    for (size_t i = 0; i < mask.size(); ++i) {
        if (!mask[i] && !reachesEdge[i]) mask[i] = true;
    }
}

void FillMajorityNeighborCells(std::vector<bool>& mask, int gridN) {
    if (gridN <= 0 || mask.size() != static_cast<size_t>(gridN) * gridN) return;

    bool changed = true;
    while (changed) {
        changed = false;
        for (int row = 0; row < gridN; ++row) {
            for (int col = 0; col < gridN; ++col) {
                const size_t idx = static_cast<size_t>(row) * gridN + col;
                if (mask[idx]) continue;
                int trueNeighbors = 0;
                if (row > 0 && mask[idx - static_cast<size_t>(gridN)]) ++trueNeighbors;
                if (row + 1 < gridN && mask[idx + static_cast<size_t>(gridN)]) ++trueNeighbors;
                if (col > 0 && mask[idx - 1]) ++trueNeighbors;
                if (col + 1 < gridN && mask[idx + 1]) ++trueNeighbors;
                if (trueNeighbors >= 3) {
                    mask[idx] = true;
                    changed = true;
                }
            }
        }
    }
}

void FillBoundaryStraddlingCells(std::vector<bool>& mask, const std::vector<float>& differingFraction, int gridN,
                                  int requiredNeighbors) {
    if (gridN <= 0 || mask.size() != static_cast<size_t>(gridN) * gridN ||
        differingFraction.size() != mask.size()) {
        return;
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int row = 0; row < gridN; ++row) {
            for (int col = 0; col < gridN; ++col) {
                const size_t idx = static_cast<size_t>(row) * gridN + col;
                if (mask[idx]) continue;
                if (differingFraction[idx] <= 0.0f) continue;
                int trueNeighbors = 0;
                if (row > 0 && mask[idx - static_cast<size_t>(gridN)]) ++trueNeighbors;
                if (row + 1 < gridN && mask[idx + static_cast<size_t>(gridN)]) ++trueNeighbors;
                if (col > 0 && mask[idx - 1]) ++trueNeighbors;
                if (col + 1 < gridN && mask[idx + 1]) ++trueNeighbors;
                if (trueNeighbors >= requiredNeighbors) {
                    mask[idx] = true;
                    changed = true;
                }
            }
        }
    }
}

std::vector<PixelRect> SelectEvidencedRects(const std::vector<bool>& rawMask, const std::vector<PixelRect>& rects,
                                             const ContentMaskConfig& config, std::vector<bool>* accepted) {
    std::vector<PixelRect> kept;
    if (accepted) accepted->assign(rects.size(), false);
    const int n = config.gridN;
    const int width = config.screenWidth;
    const int height = config.screenHeight;
    if (n <= 0 || width <= 0 || height <= 0 || rawMask.size() != static_cast<size_t>(n) * n) return kept;

    // Clip to the screen, dropping empty rectangles and exact duplicates (the first one stands).
    struct Candidate {
        PixelRect rect;
        size_t index;
    };
    std::vector<Candidate> cands;
    for (size_t i = 0; i < rects.size(); ++i) {
        const PixelRect clipped{std::max(rects[i].left, 0), std::max(rects[i].top, 0), std::min(rects[i].right, width),
                                std::min(rects[i].bottom, height)};
        if (clipped.right <= clipped.left || clipped.bottom <= clipped.top) continue;
        bool duplicate = false;
        for (const Candidate& c : cands) {
            duplicate = duplicate || (c.rect.left == clipped.left && c.rect.top == clipped.top && c.rect.right == clipped.right &&
                                       c.rect.bottom == clipped.bottom);
        }
        if (!duplicate) cands.push_back({clipped, i});
    }

    enum class Verdict { Pending, Accepted, Rejected };
    std::vector<Verdict> verdict(cands.size(), Verdict::Pending);
    for (bool changed = true; changed;) {
        changed = false;
        for (size_t k = 0; k < cands.size(); ++k) {
            if (verdict[k] != Verdict::Pending) continue;
            long long total = 0, flagged = 0;
            for (int row = 0; row < n; ++row) {
                const int y0 = (row * height) / n;
                const int y1 = ((row + 1) * height) / n;
                for (int col = 0; col < n; ++col) {
                    const int x0 = (col * width) / n;
                    const int x1 = ((col + 1) * width) / n;
                    if (!RegionCenterInAnyRect({cands[k].rect}, x0, x1, y0, y1)) continue;
                    bool shared = false;
                    for (size_t other = 0; other < cands.size() && !shared; ++other) {
                        shared = other != k && verdict[other] != Verdict::Rejected &&
                                 RegionCenterInAnyRect({cands[other].rect}, x0, x1, y0, y1);
                    }
                    if (shared) continue; // covered by another live candidate: says nothing about this one
                    ++total;
                    if (rawMask[static_cast<size_t>(row) * n + col]) ++flagged;
                }
            }
            if (total == 0) continue; // undecided for now (it may lie inside a candidate that gets rejected)
            if (static_cast<float>(flagged) / static_cast<float>(total) >= config.rectMinEvidenceFraction) {
                verdict[k] = Verdict::Accepted;
            } else {
                verdict[k] = Verdict::Rejected;
            }
            changed = true;
        }
    }
    for (size_t k = 0; k < cands.size(); ++k) {
        if (verdict[k] != Verdict::Accepted) continue;
        kept.push_back(cands[k].rect);
        if (accepted) (*accepted)[cands[k].index] = true;
    }
    return kept;
}

void ForceRectsIntoMask(std::vector<bool>& mask, const std::vector<PixelRect>& rects,
                         const ContentMaskConfig& config) {
    const int n = config.gridN;
    const int width = config.screenWidth;
    const int height = config.screenHeight;
    if (rects.empty() || n <= 0 || width <= 0 || height <= 0 || mask.size() != static_cast<size_t>(n) * n) return;

    for (int row = 0; row < n; ++row) {
        const int y0 = (row * height) / n;
        const int y1 = ((row + 1) * height) / n;
        for (int col = 0; col < n; ++col) {
            const int x0 = (col * width) / n;
            const int x1 = ((col + 1) * width) / n;
            if (RegionCenterInAnyRect(rects, x0, x1, y0, y1)) mask[static_cast<size_t>(row) * n + col] = true;
        }
    }
}

BoundaryRefinement RefineBoundaryMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba,
                                       std::vector<bool>& mask, const ContentMaskConfig& config,
                                       const std::vector<PixelRect>& forcedRects) {
    BoundaryRefinement result;
    const int gridN = config.gridN;
    const int width = config.screenWidth;
    const int height = config.screenHeight;
    if (config.boundaryRefineMaxDepth <= 0 || gridN <= 0 || width <= 0 || height <= 0 ||
        mask.size() != static_cast<size_t>(gridN) * gridN) {
        return result;
    }

    const size_t pixelCount = static_cast<size_t>(width) * height;
    const float gain = ComputeRobustBrightnessGain(captureRgba, wallpaperRgba, pixelCount);
    const int leafGrid = 1 << config.boundaryRefineMaxDepth;
    result.leafGrid = leafGrid;

    // Which cells are boundary cells is decided from the mask as it was on entry. Deciding from the
    // mask while promoting cells made each promotion turn its neighbors into boundary cells too, and
    // on a textured wallpaper the promotions cascaded across the whole area around an edge.
    const std::vector<bool> original = mask;
    const int minGroup = std::max(1, std::min(3, leafGrid * leafGrid / 4));

    for (int row = 0; row < gridN; ++row) {
        const int y0 = (row * height) / gridN;
        const int y1 = ((row + 1) * height) / gridN;
        for (int col = 0; col < gridN; ++col) {
            const size_t cellIndex = static_cast<size_t>(row) * gridN + col;
            const bool self = original[cellIndex];
            const bool isBoundary = (row > 0 && original[cellIndex - static_cast<size_t>(gridN)] != self) ||
                                     (row + 1 < gridN && original[cellIndex + static_cast<size_t>(gridN)] != self) ||
                                     (col > 0 && original[cellIndex - 1] != self) ||
                                     (col + 1 < gridN && original[cellIndex + 1] != self);
            if (!isBoundary) continue;

            const int x0 = (col * width) / gridN;
            const int x1 = ((col + 1) * width) / gridN;
            if (x1 - x0 < 2 || y1 - y0 < 2) continue; // too small to usefully subdivide

            std::vector<bool> leaves(static_cast<size_t>(leafGrid) * leafGrid, false);
            std::vector<bool> forced(leaves.size(), false);
            RefineQuadrant(captureRgba, wallpaperRgba, width, height, gain, config, x0, x1, y0, y1, /*depth=*/0,
                           config.boundaryRefineMaxDepth, leafGrid, 0, 0, leafGrid, leaves, forced, forcedRects);

            // Noise filter: only evidence groups of connected leaves count (forced leaves always do).
            std::vector<bool> evidence(leaves.size());
            for (size_t i = 0; i < leaves.size(); ++i) evidence[i] = leaves[i] && !forced[i];
            DropSmallLeafGroups(evidence, leafGrid, minGroup);
            bool any = false;
            for (size_t i = 0; i < leaves.size(); ++i) {
                leaves[i] = forced[i] || evidence[i];
                any = any || leaves[i];
            }

            // Only ever promote -- see the function's doc comment on why a cell already `true` is
            // left alone even if every leaf disagrees.
            if (!self && any) mask[cellIndex] = true;
            result.cells[static_cast<int>(cellIndex)] = std::move(leaves);
        }
    }
    return result;
}

BoundaryRefinement FinishContentMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba, std::vector<bool>& mask,
                                      const std::vector<float>& differingFraction,
                                      const std::vector<PixelRect>& candidateRects, const ContentMaskConfig& config,
                                      std::vector<PixelRect>* usedRects, std::vector<bool>* candidateAccepted,
                                      ContentMaskStats* stats) {
    auto count = [&]() {
        int k = 0;
        for (bool b : mask) k += b ? 1 : 0;
        return k;
    };
    ContentMaskStats local;
    local.raw = count();
    const std::vector<PixelRect> rects = SelectEvidencedRects(mask, candidateRects, config, candidateAccepted);
    ForceRectsIntoMask(mask, rects, config);
    local.afterRects = count();
    FillEnclosedMaskHoles(mask, config.gridN);
    local.afterEnclosed = count();
    FillMajorityNeighborCells(mask, config.gridN);
    local.afterMajority = count();
    BoundaryRefinement refinement = RefineBoundaryMask(captureRgba, wallpaperRgba, mask, config, rects);
    local.afterRefine = count();
    FillBoundaryStraddlingCells(mask, differingFraction, config.gridN);
    local.afterStraddle = count();
    if (usedRects) *usedRects = rects;
    if (stats) *stats = local;
    return refinement;
}

} // namespace core
