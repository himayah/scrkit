#pragma once
// Composites a source image onto a screen-sized canvas the same way Windows
// renders the desktop wallpaper under each of its "Fit" display styles
// (Fill / Fit / Stretch / Center / Tile / Span), so core::ContentMask's diff
// reference actually matches, pixel-for-pixel, what Windows really draws --
// a naive full-canvas stretch (ignoring aspect ratio, no cropping) only
// matches the "Stretch" style; "Fill" (Windows 10/11's own default) instead
// scales-to-cover and crops, so comparing the real capture against a
// naively-stretched reference produced widespread false "content" wherever
// the crop/positioning actually differed (user feedback: patches that
// looked like the background color were visibly flickering inside content
// blocks that should never have been flagged as content in the first
// place). Pure pixel math, no Windows dependency, so (like the rest of
// src/core/) it's unit-testable on any OS -- the Windows-specific part is
// only reading *which* mode is configured (see
// platform::GetSystemWallpaperFitMode).

#include <cstdint>

namespace core {

enum class WallpaperFitMode {
    Center, // native size, centered, cropped/letterboxed as needed
    Tile,   // native size, repeated from the top-left corner
    Stretch, // scaled to exactly fill the canvas, aspect ratio ignored
    Fit,    // scaled to fit entirely inside the canvas, letterboxed (aka "Fit to screen")
    Fill,   // scaled to cover the canvas entirely, cropped (Windows 10/11 default)
    Span,   // multi-monitor only; treated the same as Fill for a single screen
};

// Fills `dst` (already sized dstW*dstH*4, RGBA8, top-down) with `src`
// (srcW x srcH, RGBA8) composited per `mode`. Any area `src` doesn't cover
// (Center/Fit letterboxing) is filled with (letterboxR, letterboxG,
// letterboxB) -- Windows uses the user's chosen desktop background color
// there (see platform::GetSystemDesktopColor).
void CompositeWallpaper(const uint8_t* src, int srcW, int srcH, uint8_t* dst, int dstW, int dstH,
                         WallpaperFitMode mode, uint8_t letterboxR, uint8_t letterboxG,
                         uint8_t letterboxB);

} // namespace core
