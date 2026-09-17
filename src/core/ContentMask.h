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
    // A cell that the color-diff check above still doesn't flag gets a
    // second chance based on *texture*, not color: if the wallpaper there is
    // at least this much more varied (higher pixel-value standard
    // deviation, RGB samples pooled together) than the real capture, it's
    // flagged as content anyway.
    //
    // Real windows -- a terminal, a dialog, browser chrome -- are almost
    // always much flatter/more uniform than a photographic wallpaper, even
    // in the wallpaper's own dark/shadowed patches; a plain dark UI
    // background can coincidentally land close enough in *average* color to
    // a shadowed patch of foliage or rock to dodge the diff check above
    // entirely, without ever coming close to that patch's actual pixel-to-
    // pixel texture. Real-machine feedback (analysis of debug_capture.bmp/
    // debug_wallpaper.bmp, see spiral-saver-open-work memory) found a dark
    // terminal window sitting on a shadowed tree/rock area this way -- well
    // over half its cells excluded by color alone.
    //
    // Tuned once already (20 -> 15): measuring std margin (wallpaper std
    // minus capture std) across a real capture, cells *outside* the
    // offending window (genuine background) clustered tightly near zero
    // (median 0.36, 95th percentile 3.8, 99th percentile 6.7 out of ~8000
    // such cells), while the coincidentally-color-matched terminal cells
    // ranged from single digits up into the 20s-40s. Sweeping the threshold
    // against that same capture (including FillEnclosedMaskHoles's
    // cascading effect, since recovering more cells can itself newly
    // enclose others): 20 recovered the terminal to 88.8% coverage at a
    // background-cell cost of 12; 15 reached 93.1% at a cost of 20 -- a
    // clearly worthwhile trade. Below that the curve flattens fast (12 ->
    // 93.7% / 29, 10 -> 94.1% / 45, 8 -> 94.4% / 75), diminishing returns for
    // a fast-rising false-positive cost, so 15 is where this stopped.
    float textureFlatnessMargin = 15.0f;
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
//
// A cell the diff above still doesn't flag gets one more chance via
// `config.textureFlatnessMargin` (see its own doc comment): real UI is
// almost always far flatter than a photographic wallpaper, even in the
// wallpaper's own dark/shadowed patches, so a capture region that's
// suspiciously *more uniform* than the wallpaper underneath it there is
// still very likely real content that happened to land close in average
// color, not an actual match.
//
// This is the raw per-cell diff only -- it does not fill enclosed holes
// (see FillEnclosedMaskHoles). Callers that want the polished result should
// run IsContentMaskSuspicious (if needed) on this raw mask first -- it's a
// meaningfully more reliable "does this look like two unrelated images"
// signal before enclosed holes get filled in, since filling can itself push
// a legitimately busy, heavily-windowed desktop's flagged fraction well
// above what it was pre-fill (real-machine measurement: 75% -> 96% on one
// capture) -- and only call FillEnclosedMaskHoles afterward.
//
// When `outDifferingFraction` is non-null, it's resized to gridN*gridN and
// filled with each cell's raw `differing / total` ratio (the color-diff
// fraction *before* the cellDifferingFraction cutoff or the texture-
// flatness fallback) -- FillBoundaryStraddlingCells' evidence for cells a
// real window edge only partially covers.
std::vector<bool> ComputeContentMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba,
                                      const ContentMaskConfig& config,
                                      std::vector<float>* outDifferingFraction = nullptr);

// Returns true if `mask` flags at least `threshold` (default 90%) of its
// cells as content -- a strong signal that `captureRgba` and `wallpaperRgba`
// don't actually correspond to the same on-screen image at all, rather than
// genuine dense foreground content. Real-machine investigation found this
// exact failure mode with Windows Spotlight/slideshow desktop backgrounds:
// platform::GetSystemWallpaperPath() (SPI_GETDESKWALLPAPER) can return a
// path that no longer matches what's actually rendered on screen, so the
// diff compares two unrelated photos and flags nearly the whole screen.
// A single legitimate maximized window can also flag close to 100%, so this
// is meant to gate a cheap, bounded retry (re-fetch the wallpaper path and
// recompute once -- see AppController::Initialize) rather than to reject
// the result outright: a false trigger just costs one extra recompute.
bool IsContentMaskSuspicious(const std::vector<bool>& mask, double threshold = 0.90);

// In place: promotes any `false` cell in `mask` (a gridN x gridN grid, same
// row-major order as ComputeContentMask's result) to `true` if it cannot
// reach the edge of the grid through a 4-connected chain of other `false`
// cells -- i.e. it's fully enclosed by "content" cells on every side.
//
// Real windows are opaque rectangles; ComputeContentMask's per-cell diff can
// still miss part of one if the captured pixels there coincidentally
// resemble the wallpaper at that exact spot (e.g. a plain white dialog
// background, or a dark terminal, landing close enough to a similarly-
// colored patch of wallpaper after brightness-gain correction). Real-
// machine feedback found this cutting out enormous contiguous chunks of a
// plain-background settings dialog at once (well over 1000 of ~12000 grid
// cells in one capture) -- the wallpaper visibly showing through the middle
// of an otherwise-solid window, exactly the originally reported "holes"
// symptom, and a second, independent cause of it discovered only after the
// Windows Spotlight/slideshow wallpaper-staleness bug (see
// AppController::Initialize) was already fixed. A cell that's surrounded by
// flagged neighbors is far more likely to be exactly that kind of
// coincidental miss than a real gap of visible wallpaper in the middle of a
// window, since real windows don't have gaps. A background cell that
// legitimately reaches the grid edge (i.e. visible desktop background
// that's actually connected to open space, not boxed in by windows on
// every side) is left alone.
void FillEnclosedMaskHoles(std::vector<bool>& mask, int gridN);

// In place: repeatedly promotes any `false` cell to `true` once at least 3
// of its (up to 4) orthogonal neighbors are already `true`, iterating to a
// fixed point (a newly-promoted cell can itself push a further neighbor
// over the same bar). A corner cell (only 2 possible orthogonal neighbors)
// can never reach 3 and is left alone either way.
//
// Complements FillEnclosedMaskHoles for misses that aren't *fully*
// enclosed: a real window's own edge, title bar, or a thin text separator
// line inside it can sit right where a large background region is only one
// cell away and connected onward to the grid edge (so the strict enclosure
// check above correctly leaves it alone) while still being surrounded on
// 3 of its 4 sides by other flagged cells. Real-machine feedback (analysis
// of debug_capture.bmp/debug_wallpaper.bmp, see spiral-saver-open-work
// memory): spot-checking every cell this rule newly filled on one capture
// (a window's rounded title-bar corner, a horizontal separator line inside
// a terminal, a window edge against the wallpaper) confirmed each one was
// genuinely inside real content, not visible background -- recovering
// ~0.7% of the grid beyond what color, texture, and strict enclosure alone
// found.
void FillMajorityNeighborCells(std::vector<bool>& mask, int gridN);

// In place: repeatedly promotes a `false` cell to `true` once at least
// `requiredNeighbors` (default 2) of its (up to 4) orthogonal neighbors are
// already `true` AND its own raw color-diff fraction in `differingFraction`
// (ComputeContentMask's optional outDifferingFraction, same row-major
// layout) is greater than zero, iterating to a fixed point.
//
// Targets a real window's own straight edge, which essentially never lands
// exactly on a grid cell boundary: the one cell it cuts through genuinely
// contains a mix of window and background pixels, so neither
// cellDifferingFraction (the mix rarely reaches 30% on its own) nor
// textureFlatnessMargin (*both* sides show real pixel variance there --
// the capture's own mix of flat window and photographic background, not a
// flat/textured mismatch) can resolve it, and FillMajorityNeighborCells'
// 3-of-4 bar is usually unreachable too (a cell straddling a straight edge
// typically only ever has 2 true neighbors: the one further along the same
// edge, and the one on the content side). Real-machine measurement on a
// capture with exactly this failure mode along a window's right edge:
// deep-interior background cells (no true neighbors) never showed *any*
// non-zero raw fraction, so requiring only "greater than zero" plus 2
// neighbors -- much weaker than the primary threshold -- is still safe;
// visually, this closed most of the row-by-row wobble along that edge,
// with the remainder (cells whose own fraction reads exactly zero despite
// truly being inside the window -- the wallpaper patch behind them
// happened to match perfectly) needing evidence this function deliberately
// doesn't use.
void FillBoundaryStraddlingCells(std::vector<bool>& mask, const std::vector<float>& differingFraction, int gridN,
                                  int requiredNeighbors = 2);

} // namespace core
