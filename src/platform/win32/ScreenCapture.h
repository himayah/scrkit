#pragma once
// Captures a still image of the real desktop (wallpaper + real icons/open
// windows, whatever is actually on screen right now) so the fullscreen
// saver can texture its simulated icon/window boxes with a clipping of
// what was really there, instead of a flat placeholder color -- addresses
// user feedback that the boxes looked too plain.
//
// This is a read-only pixel capture (GDI BitBlt from the desktop DC) taken
// once, before the saver's own window covers the screen. It never queries
// or touches real icon/window positions or state -- it only reads the
// already-rendered screen image, same as a screenshot tool would
// (要件.txt 禁止事項: 実際のデスクトップを操作してはならない、は引き続き
// 遵守 -- ここでは「操作」ではなく単純な画面読み取りのみを行う).

#include "ImageLoader.h"

namespace platform {

// Captures the top-left `width` x `height` region of the virtual screen.
// Returns false (leaving `out` untouched) on any GDI failure.
bool CaptureScreenToImage(int width, int height, DecodedImage& out);

} // namespace platform
