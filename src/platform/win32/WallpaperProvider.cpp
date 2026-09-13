#include "WallpaperProvider.h"

#include <cstdlib>

#include <windows.h>

#include "../../core/Logger.h"

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

core::WallpaperFitMode GetSystemWallpaperFitMode() {
    // Pre-filled with Windows 10/11's own default (Fill, not tiled) so a
    // registry read failure just falls back to the most common case rather
    // than an arbitrary one. RegGetValueW is a pure read (要件.txt 禁止事項準拠).
    wchar_t styleBuf[8] = L"10";
    wchar_t tileBuf[8] = L"0";
    DWORD styleSize = sizeof(styleBuf);
    DWORD tileSize = sizeof(tileBuf);
    RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"WallpaperStyle", RRF_RT_REG_SZ, nullptr,
                 styleBuf, &styleSize);
    RegGetValueW(HKEY_CURRENT_USER, L"Control Panel\\Desktop", L"TileWallpaper", RRF_RT_REG_SZ, nullptr,
                 tileBuf, &tileSize);

    const int style = _wtoi(styleBuf);
    const bool tile = _wtoi(tileBuf) != 0;
    core::WallpaperFitMode mode;
    switch (style) {
        case 0: mode = tile ? core::WallpaperFitMode::Tile : core::WallpaperFitMode::Center; break;
        case 2: mode = core::WallpaperFitMode::Stretch; break;
        case 6: mode = core::WallpaperFitMode::Fit; break;
        case 22: mode = core::WallpaperFitMode::Span; break;
        case 10:
        default: mode = core::WallpaperFitMode::Fill; break;
    }
    // Diagnostic (temporary -- chasing the compositedWallpaper-too-dark bug,
    // see docs/TODO / spiral-saver-open-work memory): confirms the raw
    // registry values actually read on the real machine and which mode they
    // resolved to, so a misread WallpaperStyle/TileWallpaper can be ruled
    // in/out directly instead of guessing from the composited result alone.
    core::Logger::Info("GetSystemWallpaperFitMode: WallpaperStyle=\"" + std::to_string(style) +
                        "\" TileWallpaper=\"" + std::to_string(_wtoi(tileBuf)) +
                        "\" -> mode=" + std::to_string(static_cast<int>(mode)));
    return mode;
}

} // namespace platform
