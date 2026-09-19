#pragma once
// Captures a still image of the real desktop (wallpaper + real icons/open
// windows, whatever is actually on screen right now) so the fullscreen
// saver can diff it against the plain wallpaper and texture its "content"
// particles with a clipping of what was really there (see core::ContentMask
// for how that diff decides which grid cells count as content).
//
// This is a read-only pixel capture (GDI BitBlt from the desktop DC) taken
// once, before the saver's own window covers the screen. It never moves,
// resizes, or otherwise touches real icons or windows -- it only reads the
// already-rendered screen image, same as a screenshot tool would (window
// bounds are read separately, and only as corroborating geometry, by
// platform::EnumerateVisibleWindowRects)
// (要件.txt 禁止事項: 実際のデスクトップを操作してはならない、は引き続き
// 遵守 -- ここでは「操作」ではなく単純な画面読み取りのみを行う).

#include "ImageLoader.h"

namespace platform {

// Captures the top-left `width` x `height` region of the virtual screen.
// Returns false (leaving `out` untouched) on any GDI failure.
bool CaptureScreenToImage(int width, int height, DecodedImage& out);

} // namespace platform
