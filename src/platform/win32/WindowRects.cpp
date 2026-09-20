#include "WindowRects.h"

#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cwchar>

#include "../../core/Logger.h"

namespace platform {

namespace {

// Not defined by older MinGW-w64 dwmapi.h versions; values are part of the
// stable Win32 ABI (DWMWINDOWATTRIBUTE).
constexpr DWORD kDwmwaExtendedFrameBounds = 9;
constexpr DWORD kDwmwaCloaked = 14;

struct EnumContext {
    std::vector<WindowInfo>* out;
    DWORD ownProcessId;
    // physicalToProcess: multiply a physical-pixel coordinate (what DWM
    // always reports) by this to get the coordinate space the screen capture
    // and GetWindowRect use for this process. 1.0 unless the process is
    // DPI-unaware on a scaled display, where SM_CXSCREEN/BitBlt see the
    // virtualized (smaller) screen but DwmGetWindowAttribute does not.
    double physicalToProcess;
    int screenWidth;
    int screenHeight;
};

bool IsDesktopBackgroundWindow(HWND hwnd) {
    wchar_t className[64] = {};
    if (GetClassNameW(hwnd, className, 64) == 0) return false;
    // Progman / WorkerW host the wallpaper and the desktop icon ListView and
    // span the whole screen; treating them as a window would flag everything.
    return wcscmp(className, L"Progman") == 0 || wcscmp(className, L"WorkerW") == 0;
}

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumContext*>(lParam);

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == ctx->ownProcessId) return TRUE;

    BOOL cloaked = FALSE;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, kDwmwaCloaked, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;

    if (IsDesktopBackgroundWindow(hwnd)) return TRUE;

    const LONG exStyle = GetWindowLongW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_LAYERED) {
        // Click-through layered overlays (screen recorders, HUDs) aren't
        // real content, and a layered window at ~zero alpha paints nothing.
        if (exStyle & WS_EX_TRANSPARENT) return TRUE;
        COLORREF colorKey = 0;
        BYTE alpha = 255;
        DWORD flags = 0;
        if (GetLayeredWindowAttributes(hwnd, &colorKey, &alpha, &flags) && (flags & LWA_ALPHA) && alpha < 32) {
            return TRUE;
        }
    }

    // Prefer DWM's extended frame bounds: GetWindowRect includes the
    // invisible resize borders modern Windows adds around every window
    // (~7-8px on the sides and bottom), which would put a band of plain
    // wallpaper into the "window". DWM reports physical pixels regardless of
    // this process's DPI awareness; GetWindowRect is already in process
    // coordinates and is the fallback.
    RECT rect{};
    bool haveRect = false;
    RECT dwmRect{};
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, kDwmwaExtendedFrameBounds, &dwmRect, sizeof(dwmRect)))) {
        const double s = ctx->physicalToProcess;
        rect.left = static_cast<LONG>(std::floor(dwmRect.left * s));
        rect.top = static_cast<LONG>(std::floor(dwmRect.top * s));
        rect.right = static_cast<LONG>(std::ceil(dwmRect.right * s));
        rect.bottom = static_cast<LONG>(std::ceil(dwmRect.bottom * s));
        haveRect = true;
    } else if (GetWindowRect(hwnd, &rect)) {
        haveRect = true;
    }
    if (!haveRect) return TRUE;

    core::PixelRect r;
    r.left = std::max(static_cast<int>(rect.left), 0);
    r.top = std::max(static_cast<int>(rect.top), 0);
    r.right = std::min(static_cast<int>(rect.right), ctx->screenWidth);
    r.bottom = std::min(static_cast<int>(rect.bottom), ctx->screenHeight);
    if (r.right <= r.left || r.bottom <= r.top) return TRUE;

    WindowInfo info;
    info.rect = r;
    info.exStyle = static_cast<unsigned long>(exStyle);
    wchar_t cls[96] = {};
    if (GetClassNameW(hwnd, cls, 96) > 0) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, cls, -1, nullptr, 0, nullptr, nullptr);
        if (n > 1) {
            info.className.assign(static_cast<size_t>(n) - 1, '\0');
            WideCharToMultiByte(CP_UTF8, 0, cls, -1, &info.className[0], n, nullptr, nullptr);
        }
    }
    ctx->out->push_back(std::move(info));
    return TRUE;
}

// Physical-to-process coordinate scale: primary display's physical width
// (GetDeviceCaps DESKTOPHORZRES -- physical even for a DPI-unaware process)
// over the width this process sees (SM_CXSCREEN). Falls back to 1.0 on any
// oddity, and never scales *up* past sanity bounds.
double PhysicalToProcessScale() {
    HDC screenDC = GetDC(nullptr);
    if (!screenDC) return 1.0;
    const int physicalWidth = GetDeviceCaps(screenDC, DESKTOPHORZRES);
    ReleaseDC(nullptr, screenDC);
    const int processWidth = GetSystemMetrics(SM_CXSCREEN);
    if (physicalWidth <= 0 || processWidth <= 0) return 1.0;
    const double scale = static_cast<double>(processWidth) / static_cast<double>(physicalWidth);
    return (scale > 0.25 && scale <= 1.0) ? scale : 1.0;
}

} // namespace

std::vector<WindowInfo> EnumerateVisibleWindows(int screenWidth, int screenHeight) {
    std::vector<WindowInfo> windows;
    if (screenWidth <= 0 || screenHeight <= 0) return windows;

    EnumContext ctx{&windows, GetCurrentProcessId(), PhysicalToProcessScale(), screenWidth, screenHeight};
    if (!EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx))) {
        core::Logger::Warn("WindowRects: EnumWindows failed; continuing with whatever was collected");
    }
    return windows;
}

std::vector<core::PixelRect> EnumerateVisibleWindowRects(int screenWidth, int screenHeight) {
    std::vector<core::PixelRect> rects;
    for (const WindowInfo& w : EnumerateVisibleWindows(screenWidth, screenHeight)) rects.push_back(w.rect);
    return rects;
}

} // namespace platform
