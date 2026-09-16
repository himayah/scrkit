#pragma once
// Determines which cells of the real-desktop capture actually differ from
// the plain wallpaper -- i.e. where an icon, the taskbar, an open window, or
// anything else is currently drawn on top of it. Pure pixel math, no
// Windows dependency, so (like the rest of src/core/) it's unit-testable on
// any OS. Replaces the earlier per-window/per-icon query approach (querying
// individual window/icon positions via EnumWindows and the desktop's icon
// ListView, and capturing each individually via PrintWindow), which did not
// hold up in practice on a real machine.

#include <cstdint>
#include <vector>

namespace core {

// Nearest-neighbor resample of a top-down RGBA8 buffer from srcW x srcH to
// dstW x dstH. `dst` must already be sized dstW*dstH*4 bytes. Used to bring
// the (arbitrarily-sized) decoded wallpaper image to the same pixel grid as
// the real-desktop capture before diffing them.
void ResampleRgba(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH);

// Which of `gridN` evenly-distributed cells covering [0, totalSize) a given
// pixel coordinate falls into -- the exact inverse of the cell-boundary
// convention core::ComputeContentMask uses internally (cell k spans
// [k*totalSize/gridN, (k+1)*totalSize/gridN)), so callers elsewhere that
// need to know "which ComputeContentMask cell is this pixel in" (e.g.
// platform::CreateMaskedTextureFromImage) get boundaries that line up
// exactly instead of drifting from a naively fixed totalSize/gridN cell
// width (which isn't the same partition whenever totalSize isn't an exact
// multiple of gridN -- true for essentially every real screen resolution).
inline int PixelToGridIndex(int pixel, int gridN, int totalSize) {
    if (gridN <= 1 || totalSize <= 0) return 0;
    int index = (pixel * gridN) / totalSize;
    if (index < 0) index = 0;
    if (index >= gridN) index = gridN - 1;
    // The direct inverse can undershoot by exactly 1 at some cell boundaries
    // (integer double-flooring: floor(pixel*gridN/totalSize) isn't always
    // the same cell as the one whose own floor(col*totalSize/gridN) bound
    // actually contains `pixel`). Since the true answer is never more than
    // 1 above this estimate, one corrective step is sufficient and exact.
    if (index + 1 < gridN && ((index + 1) * totalSize) / gridN <= pixel) {
        ++index;
    }
    return index;
}

struct ContentMaskConfig {
    int screenWidth = 1920;
    int screenHeight = 1080;
    int gridN = 32; // same grid partition as core::BuildParticleGrid
    // A pixel counts as "different" once |ΔR|+|ΔG|+|ΔB| exceeds this.
    //
    // Higher than it might look for a per-pixel color comparison: even after
    // brightness-gain correction and crop-offset alignment (see
    // core::ComputeContentMask / core::CompositeWallpaperAligned) fix the
    // *systematic* mismatches, a detailed photographic wallpaper still shows
    // scattered moderate-magnitude per-pixel diffs (edge/anti-aliasing noise
    // along mountain ridgelines, tree lines, etc.) purely from being decoded
    // and scaled by a different pipeline (this code's own WIC decode) than
    // whatever rendered the real screen -- real-machine feedback (analysis
    // of debug_capture.bmp/debug_wallpaper.bmp, see spiral-saver-open-work
    // memory) found ~38% of pixels in that "moderate" 24-400 diff band vs.
    // only ~16% in a "near-total mismatch" band that corresponded to actual
    // real content (an open window, desktop icons). 90 sits above that
    // texture-noise band while staying far below the stark contrast a real
    // UI element produces against a photo background.
    int pixelDiffThreshold = 90;
    // A cell counts as real-desktop "content" once at least this fraction
    // of its pixels differ. Widened twice now (0.03 -> 0.12 -> 0.30): after
    // fixing the visible background render to use the same properly-fit
    // wallpaper as the diff (see AppController::Initialize), real-machine
    // analysis of debug_capture.bmp/debug_wallpaper.bmp still found the
    // busiest textured regions (a snow-capped mountain range) sitting at
    // ~17-19% of pixels over pixelDiffThreshold -- comfortably below real
    // content's actual rate (icons/taskbar/an open window measured at
    // 70-100% in the same captures) but still above the old 0.12, which is
    // why plain sky stayed correctly excluded while the mountain wrongly
    // got swept in (user feedback: "空の部分は対象外になっているが山の部分
    // は対象になってしまっている"). 0.30 sits with a clear margin above
    // that texture-noise ceiling while staying far below real content's.
    float cellDifferingFraction = 0.30f;
};

// Returns gridN*gridN bools, in the same row-major cell order as
// core::BuildParticleGrid, marking which cells of `captureRgba` differ
// enough from `wallpaperRgba` to be treated as real desktop content to suck
// away. Both buffers must already be screenWidth x screenHeight RGBA8 (top-
// down), matching `config`.
//
// Two corrections run before the actual per-pixel diff:
//
// 1. `wallpaperRgba` is rescaled by a brightness gain -- the *median* of
//    the per-pixel capture/wallpaper luminance ratio, not a single ratio of
//    the two images' overall sums -- so a uniform brightness offset between
//    the two doesn't itself register as "content". Real-machine feedback
//    showed Windows tone-maps the whole desktop noticeably brighter than a
//    wallpaper file's raw pixels when the display has HDR/"Advanced color"
//    enabled (~1.7x brighter on a real 4K HDR screen); on a plain SDR
//    display there's no such offset, so the computed gain is ~1.0 there
//    and this is a no-op either way. Using the median instead of a sum
//    ratio matters once real content is on screen: a sum-based ratio is
//    skewed by however much of the *content* differs too (real-machine
//    feedback: with a large window open, the sum-based ratio came out as
//    1.2x and made the false-positive rate on the plain background *worse*,
//    not better, since it "corrected" background pixels that never needed
//    it based on a number contaminated by the window). The median stays
//    representative of the actual background as long as background pixels
//    remain the majority of the screen, which they normally are.
// 2. Both `captureRgba` and the gain-corrected wallpaper are then run
//    through the same small box blur before the pixel-by-pixel diff. An
//    independently decoded and resized wallpaper can never reproduce the
//    real screen's own rendering pixel-for-pixel, and that mismatch
//    concentrates as edge/anti-aliasing noise in detailed textures --
//    real-machine feedback found a snow-capped mountain wallpaper still
//    getting swept into the suction effect well after the brightness/crop
//    fixes, while smooth areas like sky were already fine. Blurring both
//    images by the same amount suppresses that sub-pixel-scale noise
//    equally on both sides while leaving real content (icons, taskbar,
//    open windows -- all much larger and starkly different) essentially
//    untouched.
std::vector<bool> ComputeContentMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba,
                                      const ContentMaskConfig& config);

// TEMPORARY diagnostic (to be removed once the reported foreground-mask
// problem is root-caused, matching this project's usual policy -- see git
// history for other examples of this pattern, e.g. DESIGN.md §9.7/§9.8).
// Returns a copy of `rgba` with every pixel whose cell `mask` does NOT flag
// as content replaced by a solid tint color, so any gap/hole inside what
// should be a solid rectangle (a captured icon or window) is immediately
// visible against the real captured pixels around it. Uses the exact same
// PixelToGridIndex cell lookup CreateMaskedTextureFromImage uses, so this
// shows precisely what that function's mask decision looks like per pixel.
std::vector<uint8_t> BuildDiffOverlayRgba(const uint8_t* rgba, int width, int height,
                                           const std::vector<bool>& mask, int gridN, uint8_t tintR,
                                           uint8_t tintG, uint8_t tintB);

} // namespace core
