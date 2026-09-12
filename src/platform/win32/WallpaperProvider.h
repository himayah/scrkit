#pragma once
// Retrieves the path of the wallpaper currently in use on the real Windows
// desktop (要件.txt §3-3: 「Windowsデスクトップで現在使用されている壁紙」を
// 自動取得). This only *reads* the path via the documented Win32 API; it
// never modifies the desktop or its settings.

#include <string>

namespace platform {

// Returns the full path to the current desktop wallpaper, or an empty
// string if none is set / it could not be determined.
std::wstring GetSystemWallpaperPath();

} // namespace platform
