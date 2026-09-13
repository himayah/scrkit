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

struct ContentMaskConfig {
    int screenWidth = 1920;
    int screenHeight = 1080;
    int gridN = 32; // same grid partition as core::BuildParticleGrid
    // A pixel counts as "different" once |ΔR|+|ΔG|+|ΔB| exceeds this.
    int pixelDiffThreshold = 24;
    // A cell counts as real-desktop "content" once at least this fraction
    // of its pixels differ.
    float cellDifferingFraction = 0.03f;
};

// Returns gridN*gridN bools, in the same row-major cell order as
// core::BuildParticleGrid, marking which cells of `captureRgba` differ
// enough from `wallpaperRgba` to be treated as real desktop content to suck
// away. Both buffers must already be screenWidth x screenHeight RGBA8 (top-
// down), matching `config`.
//
// Before diffing, `wallpaperRgba` is rescaled by a single global brightness
// gain (captureRgba's overall mean / wallpaperRgba's overall mean, clamped
// to a sane range) so a uniform brightness offset between the two doesn't
// itself register as "content" -- real-machine feedback showed Windows
// tone-maps the whole desktop noticeably brighter than a wallpaper file's
// raw pixels when the display has HDR/"Advanced color" enabled (~1.7x
// brighter on a real 4K HDR screen), which otherwise pushed nearly every
// cell's diff over pixelDiffThreshold and flagged ~97% of the screen as
// content. A plain SDR display has no such offset, so the computed gain
// there is ~1.0 and this is a no-op.
std::vector<bool> ComputeContentMask(const uint8_t* captureRgba, const uint8_t* wallpaperRgba,
                                      const ContentMaskConfig& config);

} // namespace core
