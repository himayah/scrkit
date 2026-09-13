#include "WallpaperProvider.h"

#include <windows.h>

namespace platform {

std::wstring GetSystemWallpaperPath() {
    wchar_t path[MAX_PATH] = {};
    // SPI_GETDESKWALLPAPER is a pure read of current user settings; this
    // never changes anything on the system (要件.txt 禁止事項準拠).
    if (!SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0)) {
        return L"";
    }
    return path;
}

void GetSystemDesktopColor(uint8_t& r, uint8_t& g, uint8_t& b) {
    // GetSysColor is a pure read of the current user setting, same
    // read-only guarantee as GetSystemWallpaperPath above.
    const COLORREF color = GetSysColor(COLOR_DESKTOP);
    r = GetRValue(color);
    g = GetGValue(color);
    b = GetBValue(color);
}

} // namespace platform
