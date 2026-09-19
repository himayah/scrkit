#pragma once
// Determines which cells of the real-desktop capture actually differ from
// the plain wallpaper -- i.e. where an icon, the taskbar, an open window, or
// anything else is currently drawn on top of it. Pure pixel math, no
// Windows dependency, so (like the rest of src/core/) it's unit-testable on
// any OS. Replaces the earlier per-window/per-icon query approach (querying
// individual window/icon positions via EnumWindows and the desktop's icon
// ListView, and capturing each individually via PrintWindow), which did not
// hold up in practice on a real machine as the *primary* source of what to
// animate. The pixel diff below is still what decides what counts as content;
// real window rectangles (platform::EnumerateVisibleWindowRects) come back
// only as corroborating geometry (SelectEvidencedRects/ForceRectsIntoMask), to
// keep a window's interior opaque where its pixels happen to match the
// wallpaper and the diff therefore has no evidence there at all.

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace core {

// Nearest-neighbor resample of a top-down RGBA8 buffer from srcW x srcH to
// dstW x dstH. `dst` must already be sized dstW*dstH*4 bytes. Used to bring
// the (arbitrarily-sized) decoded wallpaper image to the same pixel grid as
// the real-desktop capture before diffing them.
void ResampleRgba(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH);

// Area-averaging (downscale) / linear (upscale) resample of a top-down RGBA8 buffer, separable, the
// way an image is normally scaled for display. ResampleRgba (nearest neighbor) picks single source
// pixels, which on a large, finely textured wallpaper produces aliasing noise that no real screen
// shows -- and comparing such a reference with a real capture flags nearly every pixel.
void ResampleRgbaSmooth(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH);

// How well a wallpaper reference resembles the capture *as a picture* (diagnostics): mean color of
// each, and the correlation of their luminance over 16x16-pixel blocks. ~1.0 with equal means: the
// same image, aligned. High correlation but different means: a brightness/tone difference. Low
// correlation: a different image, fit mode, or position.
struct ReferenceComparison {
    double meanCapture[3] = {0, 0, 0};
    double meanReference[3] = {0, 0, 0};
    double luminanceCorrelation = 0.0;
};
ReferenceComparison CompareReference(const uint8_t* captureRgba, const uint8_t* referenceRgba, int width, int height);

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
    // of saved capture/wallpaper dumps of a real desktop) found ~38% of
    // pixels in that "moderate" 24-400 diff band vs. only ~16% in a "near-total mismatch" band that corresponded to actual
    // real content (an open window, desktop icons). 90 sits above that
    // texture-noise band while staying far below the stark contrast a real
    // UI element produces against a photo background.
    int pixelDiffThreshold = 90;
    // A cell counts as real-desktop "content" once at least this fraction
    // of its pixels differ. Widened twice now (0.03 -> 0.12 -> 0.30): after
    // fixing the visible background render to use the same properly-fit
    // wallpaper as the diff (see AppController::Initialize), real-machine
    // analysis of saved capture/wallpaper dumps of a real desktop still found the
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
    // pixel texture. Real-machine feedback (analysis of saved capture/
    // wallpaper dumps of a real desktop) found a dark terminal window
    // sitting on a shadowed tree/rock area this way -- well over half its
    // cells excluded by color alone.
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

    // Max recursive halving-depth RefineBoundaryMask subdivides a boundary
    // cell's pixel rectangle to (each level quarters the area: 1/4, 1/16,
    // 1/64 of the original cell at depths 1-3). 0 disables refinement
    // entirely. Runs once at startup (AppController::Initialize), not per
    // frame, so even the default depth of 3 costs at most a few milliseconds
    // -- see RefineBoundaryMask's own doc comment.
    int boundaryRefineMaxDepth = 3;

    // Minimum fraction of a real window rectangle's cells (cell center inside
    // the rectangle) that the raw pixel diff must already flag before
    // SelectEvidencedRects trusts that rectangle -- see its doc comment. Low
    // on purpose: a plain white dialog sitting on a white patch of wallpaper
    // only ever shows its text, controls and title bar, a few percent of its
    // area at most, and that's exactly the case rectangles exist to rescue.
    float rectMinEvidenceFraction = 0.02f;
};

// Axis-aligned pixel rectangle, half-open: covers [left,right) x [top,bottom)
// in the same top-down screen coordinates the capture buffer uses.
struct PixelRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
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
//
// The box-blur radius is chosen from the data (see ChooseBlurRadius): 2 when the wallpaper reference
// matches the capture, wider when it doesn't match at fine scale (a finely textured wallpaper the
// reference reproduces only approximately), so that mismatch isn't reported as content. Reported
// through `outBlurRadius` when non-null.
std::vector<bool> ComputeContentMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba,
                                      const ContentMaskConfig& config,
                                      std::vector<float>* outDifferingFraction = nullptr,
                                      int* outBlurRadius = nullptr);

// The smallest blur radius (2, 3, 4, 6, 8 or 12) at which the quietest parts of the screen no longer
// look different from the wallpaper reference: the 25th percentile, over a coarse tile grid, of the
// fraction of pixels whose blurred difference exceeds pixelDiffThreshold must be at most 10%. A
// pixel-exact reference already passes at radius 2, so it stays 2 and behavior is unchanged; a
// reference that differs at fine scale everywhere (~1px misalignment or different resampling on a
// high-contrast texture) needs a wider blur first. Real content is clustered, so a desktop mostly
// covered by windows still has quiet tiles and is not mistaken for a mismatch.
int ChooseBlurRadius(const uint8_t* captureRgba, const uint8_t* wallpaperRgba, const ContentMaskConfig& config);

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
// of saved capture/wallpaper dumps of a real desktop): spot-checking every cell this rule newly filled on one capture
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

// Sparse, finer-than-`mask` classification produced by RefineBoundaryMask
// for the specific cells it actually subdivided (every other cell keeps
// today's flat whole-cell behavior). `leafGrid` (a power of 2, 1 <<
// config.boundaryRefineMaxDepth) is the per-axis leaf resolution every entry
// in `cells` uses; `cells[cellIndex]` is a row-major leafGrid x leafGrid bool
// grid covering that coarse cell's own pixel rectangle. Consumed by
// platform::CreateMaskedTextureFromImage to trace a boundary cell's real
// pixel-level edge instead of including/excluding it as one flat block.
struct BoundaryRefinement {
    int leafGrid = 1;
    std::unordered_map<int, std::vector<bool>> cells;
};

// Re-examines every cell of `mask` that borders a differently-flagged
// neighbor (a "boundary cell"): unconditionally subdivides its pixel
// rectangle (quartering the area each level, up to
// config.boundaryRefineMaxDepth levels -- 1/4, 1/16, 1/64 of the original
// cell at depths 1-3) and re-runs the exact same per-region diff test
// ComputeContentMask uses on every resulting sub-rectangle, all the way
// down. Deliberately does NOT stop early just because an intermediate
// region's own verdict already matches its parent's -- content that's
// diluted at every intermediate scale but concentrated only at the deepest
// one (e.g. a window corner clipping a cell narrowly enough that neither the
// whole cell nor either half of it individually clears
// cellDifferingFraction) would otherwise never be found. Cost is still
// bounded to the boundary's length, not the screen's area, since only
// boundary cells are examined at all (roughly 84 small region evaluations
// per boundary cell at the default depth of 3).
//
// In place: promotes a `false` cell in `mask` to `true` if any leaf of its
// finest achieved refinement reads as content -- direct pixel evidence for
// exactly the case FillBoundaryStraddlingCells can only approximate via
// neighbor counting. Never demotes an already-`true` cell (see design
// discussion: a coarse cell including a little real background at its edge
// is harmless -- the background layer underneath already shows the same
// pixels there -- so refinement only ever adds detected content, never
// removes it).
//
// `captureRgba`/`wallpaperRgba` must be the same buffers (raw, i.e.
// `wallpaperRgba` NOT yet brightness-gain-corrected) ComputeContentMask was
// called with to produce `mask` -- the brightness gain is recomputed
// internally from them (cheap, and keeps this function self-contained
// rather than threading an extra out-param through ComputeContentMask's
// signature for this one caller).
//
// Call this after FillEnclosedMaskHoles/FillMajorityNeighborCells (which
// target whole-cell coincidental matches, not a resolution problem
// subdividing can help with) and before FillBoundaryStraddlingCells (which
// stays as a last-resort fallback for the rare cell where even the deepest
// refinement level reads exactly zero diff -- a perfect coincidental
// pixel-for-pixel match, not something further subdivision can resolve
// either).
//
// `forcedRects` (optional, already vetted by SelectEvidencedRects): every
// sub-region whose center lies inside any of these rectangles counts as
// content regardless of what the pixel diff says, at every level of the
// recursion. This is what makes a real window's edge pixel-exact even where
// the window body and the wallpaper behind it are identical (a white window
// over a white patch of wallpaper): the boundary cell's leaves follow the
// rectangle instead of reading "no difference" and punching a transparent
// notch into the window. Rectangles are ignored for cells that aren't
// boundary cells (an interior cell is already fully `true` from
// ForceRectsIntoMask).
BoundaryRefinement RefineBoundaryMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba,
                                       std::vector<bool>& mask, const ContentMaskConfig& config,
                                       const std::vector<PixelRect>& forcedRects = {});

// Keeps only the rectangles in `rects` (real, OS-reported visible window/
// taskbar bounds -- see platform::EnumerateVisibleWindowRects) that the pixel
// evidence in `rawMask` (ComputeContentMask's raw output, *before* any hole
// filling) actually corroborates: at least config.rectMinEvidenceFraction of
// the grid cells whose center lies in the rectangle (after clipping to the
// screen) must already be flagged. Rejects rectangles that report a window
// that isn't really painting anything there -- a transparent overlay, an
// invisible helper window, a window mid-fade -- which would otherwise turn
// plain wallpaper into "content". A rectangle too small to contain any cell
// center is dropped too (nothing to force, and too little to vouch for it).
//
// The evidence is counted only over the rectangle's EXCLUSIVE cells: those inside it and inside no
// other candidate still in play. Counting all its cells let a large invisible window (a transparent
// overlay, a hidden helper) that merely spans real windows and open wallpaper borrow their evidence
// and get accepted, turning nearly the whole screen into "content" (seen on a real desktop: 99.6% of
// cells). A real window has content of its own where nothing else is; an overlay's exclusive area is
// just wallpaper. A candidate with no exclusive cells at all (it lies inside another) is undecided
// rather than failed: candidates that fail are removed first and the rest re-counted, so real windows
// inside a bogus overlay are still accepted once the overlay is gone. Whatever is still undecided at
// the end sits inside an accepted rectangle and is dropped (that one already forces the area);
// identical rectangles count once (the first is kept). `accepted`, if given, is resized to
// rects.size() and marks the kept ones.
std::vector<PixelRect> SelectEvidencedRects(const std::vector<bool>& rawMask, const std::vector<PixelRect>& rects,
                                             const ContentMaskConfig& config, std::vector<bool>* accepted = nullptr);

// In place: marks every cell whose center lies inside any rectangle of
// `rects` as content. The point of the whole exercise: a real window is an
// opaque rectangle, so no part of its interior may be transparent, however
// closely its pixels happen to resemble the wallpaper behind it (pixel
// evidence simply doesn't exist there -- nothing FillEnclosedMaskHoles/
// FillMajorityNeighborCells can infer from the mask's shape alone when the
// hole is open to the outside on one side, as a white window's body over a
// white patch of wallpaper typically is). Cells that straddle a rectangle's
// edge without their center inside are left to RefineBoundaryMask(forcedRects),
// which resolves them at pixel level.
void ForceRectsIntoMask(std::vector<bool>& mask, const std::vector<PixelRect>& rects,
                         const ContentMaskConfig& config);

// The whole post-processing chain the raw per-cell mask goes through, in the order that matters
// (each step's rationale is on its own function above): trust only the candidate window
// rectangles the raw diff corroborates and force their interiors (SelectEvidencedRects,
// ForceRectsIntoMask), fill enclosed and mostly-surrounded holes, re-evaluate boundary cells at
// sub-cell resolution following those rectangles (RefineBoundaryMask), then the last-resort
// straddling-cell fill. `mask` is ComputeContentMask's raw result (with `differingFraction`),
// modified in place. Returns the sub-cell refinement; `usedRects`, if given, receives the
// rectangles that were trusted. Shared by the real saver and the SCRAPI preview.
// Flagged-cell counts after each stage of FinishContentMask, for diagnostics.
struct ContentMaskStats {
    int raw = 0, afterRects = 0, afterEnclosed = 0, afterMajority = 0, afterRefine = 0, afterStraddle = 0;
};

BoundaryRefinement FinishContentMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba, std::vector<bool>& mask,
                                      const std::vector<float>& differingFraction,
                                      const std::vector<PixelRect>& candidateRects, const ContentMaskConfig& config,
                                      std::vector<PixelRect>* usedRects = nullptr,
                                      std::vector<bool>* candidateAccepted = nullptr, ContentMaskStats* stats = nullptr);

} // namespace core
