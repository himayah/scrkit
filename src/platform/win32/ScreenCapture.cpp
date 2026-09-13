#include "ScreenCapture.h"

#include <vector>

#include "../../core/Logger.h"

#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002
#endif

namespace platform {

namespace {

// Shared by CaptureScreenToImage/CaptureWindowToImage: given a memory DC
// that already has `width` x `height` of BGRA pixel data painted into
// `bitmap` (by BitBlt or PrintWindow), pulls it out into a top-down RGBA
// DecodedImage.
bool ExtractBitmapToImage(HDC memDC, HBITMAP bitmap, int width, int height, DecodedImage& out) {
    BITMAPINFOHEADER bi{};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = width;
    bi.biHeight = -height; // negative = top-down DIB, matches our RGBA row order
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    std::vector<unsigned char> bgra(static_cast<size_t>(width) * height * 4);
    BITMAPINFO bmi{};
    bmi.bmiHeader = bi;
    const int scanlines =
        GetDIBits(memDC, bitmap, 0, static_cast<UINT>(height), bgra.data(), &bmi, DIB_RGB_COLORS);
    if (scanlines != height) {
        core::Logger::Warn("ScreenCapture: GetDIBits failed");
        return false;
    }

    out.width = width;
    out.height = height;
    out.rgba.assign(bgra.size(), 0);
    for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
        out.rgba[i + 0] = bgra[i + 2]; // R <- B
        out.rgba[i + 1] = bgra[i + 1]; // G
        out.rgba[i + 2] = bgra[i + 0]; // B <- R
        out.rgba[i + 3] = 255;         // force opaque
    }
    return true;
}

// Common setup: a memory DC + bitmap of `width` x `height`, compatible with
// the desktop DC (needed for both BitBlt and PrintWindow targets).
struct MemoryCanvas {
    HDC screenDC = nullptr;
    HDC memDC = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ oldBitmap = nullptr;

    bool Create(int width, int height) {
        screenDC = GetDC(nullptr);
        if (!screenDC) return false;
        memDC = CreateCompatibleDC(screenDC);
        if (!memDC) return false;
        bitmap = CreateCompatibleBitmap(screenDC, width, height);
        if (!bitmap) return false;
        oldBitmap = SelectObject(memDC, bitmap);
        return true;
    }

    ~MemoryCanvas() {
        if (memDC && oldBitmap) SelectObject(memDC, oldBitmap);
        if (bitmap) DeleteObject(bitmap);
        if (memDC) DeleteDC(memDC);
        if (screenDC) ReleaseDC(nullptr, screenDC);
    }
};

} // namespace

bool CaptureScreenToImage(int width, int height, DecodedImage& out) {
    if (width <= 0 || height <= 0) return false;

    MemoryCanvas canvas;
    if (!canvas.Create(width, height)) {
        core::Logger::Warn("ScreenCapture: failed to set up capture canvas");
        return false;
    }

    if (!BitBlt(canvas.memDC, 0, 0, width, height, canvas.screenDC, 0, 0, SRCCOPY)) {
        core::Logger::Warn("ScreenCapture: BitBlt failed");
        return false;
    }

    return ExtractBitmapToImage(canvas.memDC, canvas.bitmap, width, height, out);
}

bool CaptureWindowToImage(HWND hwnd, DecodedImage& out) {
    if (!hwnd) return false;

    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) {
        core::Logger::Warn("ScreenCapture: GetWindowRect failed for window capture");
        return false;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    if (width <= 0 || height <= 0) return false;

    MemoryCanvas canvas;
    if (!canvas.Create(width, height)) {
        core::Logger::Warn("ScreenCapture: failed to set up capture canvas");
        return false;
    }

    // PW_RENDERFULLCONTENT (Windows 8.1+) asks the window to render its
    // real content even when hardware-accelerated/DirectComposition-based,
    // which plain PrintWindow(..., 0) often renders blank for.
    if (!PrintWindow(hwnd, canvas.memDC, PW_RENDERFULLCONTENT)) {
        core::Logger::Warn("ScreenCapture: PrintWindow failed; that window will fall back");
        return false;
    }

    return ExtractBitmapToImage(canvas.memDC, canvas.bitmap, width, height, out);
}

} // namespace platform
