#pragma once
// A synthetic desktop drawn over a wallpaper, for the SCRAPI preview (docs/DESIGN_VIEWER.md
// §B.4): the preview has no real screen capture, so the foreground layer would be empty. This
// produces a deterministic "capture" (a light window, a dark terminal window, a column of
// icons, a taskbar) plus the exact rectangles of the windows, which go through the very same
// mask pipeline as a real desktop. The light window's near-white body is deliberately the
// hard case for a plain pixel diff (see core::ForceRectsIntoMask).

#include <cstdint>
#include <vector>

#include "ContentMask.h" // PixelRect

namespace core {

struct SampleDesktop {
    std::vector<uint8_t> rgba;          // w*h*4, top-down, opaque
    std::vector<PixelRect> windows;     // the two windows and the taskbar
};

// `wallpaperRgba` is w*h*4 (already composited to the screen size). Sizes scale with w/h.
SampleDesktop MakeSampleDesktop(const uint8_t* wallpaperRgba, int width, int height);

} // namespace core
