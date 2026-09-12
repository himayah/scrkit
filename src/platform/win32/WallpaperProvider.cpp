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

} // namespace platform
