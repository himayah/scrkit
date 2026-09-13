#pragma once
// Retrieves the path of the wallpaper currently in use on the real Windows
// desktop (要件.txt §3-3: 「Windowsデスクトップで現在使用されている壁紙」を
// 自動取得). This only *reads* the path via the documented Win32 API; it
// never modifies the desktop or its settings.

#include <cstdint>
#include <string>

#include "../../core/WallpaperFit.h"

namespace platform {

// Returns the full path to the current desktop wallpaper, or an empty
// string if none is set / it could not be determined -- notably, Windows
// returns an empty path here (not an error) when the user has chosen a
// plain solid-color background instead of a picture.
std::wstring GetSystemWallpaperPath();

// Returns the desktop's solid background color (Settings > Personalization
// > Background > Solid color, or the classic "Desktop" color) via
// GetSysColor(COLOR_DESKTOP). Used as the fallback when GetSystemWallpaperPath
// returns empty, so that fallback still matches what's actually on screen
// (instead of an arbitrary placeholder color) -- this matters for
// core::ContentMask's diff: comparing the real capture against the wrong
// color would flag the entire plain desktop as "content".
void GetSystemDesktopColor(uint8_t& r, uint8_t& g, uint8_t& b);

// Returns how Windows is currently positioning/scaling the desktop
// wallpaper (Settings > Personalization > Background > "壁紙を合わせる"),
// read from HKCU\Control Panel\Desktop's WallpaperStyle/TileWallpaper
// values. This only reads the setting; it never changes it. Defaults to
// Fill (Windows 10/11's own default, and the safest guess if the registry
// values are missing/unreadable) so core::CompositeWallpaper's diff
// reference matches what's actually on screen instead of assuming a plain
// stretch, which only corresponds to one of several possible styles.
core::WallpaperFitMode GetSystemWallpaperFitMode();

} // namespace platform
