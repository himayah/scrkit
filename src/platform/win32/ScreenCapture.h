#pragma once
// Captures still images of the real desktop / individual real windows, so
// the fullscreen saver can texture its simulated icon/window boxes with a
// clipping of what was really there, instead of a flat placeholder color --
// addresses user feedback that the boxes looked too plain.
//
// These are read-only pixel captures taken once, before the saver's own
// window covers the screen. They never query or touch real icon/window
// *positions* or state (that's RealDesktopQuery) -- they only read
// already-rendered pixels, same as a screenshot tool would (要件.txt
// 禁止事項: 実際のデスクトップを操作してはならない、は引き続き遵守 --
// ここでは「操作」ではなく単純な画面読み取りのみを行う).

#include "ImageLoader.h"

#include <windows.h>

namespace platform {

// Captures the top-left `width` x `height` region of the virtual screen via
// BitBlt -- i.e. the final, composited image exactly as shown on screen.
// If something is currently covering part of that region, the covering
// content is what gets captured there (this is the fallback used when a
// more specific per-window capture -- see below -- isn't available).
// Returns false (leaving `out` untouched) on any GDI failure.
bool CaptureScreenToImage(int width, int height, DecodedImage& out);

// Captures `hwnd`'s own content via PrintWindow, independent of whatever
// currently obscures it on the real screen -- i.e. this gets the window's
// true appearance even where another window is on top of it right now
// (user feedback: overlapped windows/icons should still clip their real
// content, not whatever happens to be drawn over them). The image covers
// exactly `hwnd`'s GetWindowRect dimensions, in that same coordinate
// origin. Returns false if PrintWindow fails; some apps using
// hardware-overlay video or certain protected content may also render
// blank despite PrintWindow reporting success -- an inherent OS-level
// limitation shared by any screenshot tool using this technique, not
// something this project can work around.
bool CaptureWindowToImage(HWND hwnd, DecodedImage& out);

} // namespace platform
