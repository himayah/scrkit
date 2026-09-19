#pragma once
// Read-only enumeration of the real, currently-visible top-level windows'
// on-screen rectangles (open windows and the taskbar -- anything
// EnumWindows reports that is actually painted). Used as *corroborating
// geometry* for core::ContentMask, never as its primary source: a window is
// an opaque rectangle, so once the pixel diff has vouched that a window is
// really showing something (core::SelectEvidencedRects), its whole interior
// can be treated as content even where its pixels happen to match the
// wallpaper behind it (a white dialog over a white patch of wallpaper),
// which no amount of pixel comparison can see. See core::ForceRectsIntoMask.
//
// Like the screen capture, this only *reads* window bounds; it never moves,
// resizes, or otherwise touches a window (要件.txt §10).

#include <vector>

#include <windows.h>

#include "../../core/ContentMask.h"

namespace platform {

// Returns the visible-frame rectangle of every visible, non-minimized,
// non-cloaked top-level window not owned by this process, clipped to
// [0,screenWidth) x [0,screenHeight) and expressed in the same pixel space
// CaptureScreenToImage captured (the process's own coordinate space, which
// matters for a DPI-unaware process on a scaled display -- see the .cpp).
// Desktop-background windows (Progman/WorkerW), click-through overlays, and
// fully transparent layered windows are skipped. Never fails hard: on any
// API error the affected window is just left out.
std::vector<core::PixelRect> EnumerateVisibleWindowRects(int screenWidth, int screenHeight);

} // namespace platform
