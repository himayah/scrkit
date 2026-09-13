#include "ScreenCapture.h"

#include <vector>

#include "../../core/Logger.h"

namespace platform {

void LogDisplayColorInfo() {
    UINT32 numPaths = 0, numModes = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &numPaths, &numModes) != ERROR_SUCCESS ||
        numPaths == 0) {
        core::Logger::Warn("LogDisplayColorInfo: GetDisplayConfigBufferSizes failed");
        return;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(numPaths);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(numModes);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &numPaths, paths.data(), &numModes, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        core::Logger::Warn("LogDisplayColorInfo: QueryDisplayConfig failed");
        return;
    }

    for (UINT32 i = 0; i < numPaths; ++i) {
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO colorInfo{};
        colorInfo.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        colorInfo.header.size = sizeof(colorInfo);
        colorInfo.header.adapterId = paths[i].targetInfo.adapterId;
        colorInfo.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&colorInfo.header) == ERROR_SUCCESS) {
            core::Logger::Info(
                "LogDisplayColorInfo: path " + std::to_string(i) + " advancedColorSupported=" +
                std::to_string(colorInfo.advancedColorSupported) +
                " advancedColorEnabled=" + std::to_string(colorInfo.advancedColorEnabled) +
                " wideColorEnforced=" + std::to_string(colorInfo.wideColorEnforced) +
                " advancedColorForceDisabled=" + std::to_string(colorInfo.advancedColorForceDisabled) +
                " bitsPerColorChannel=" + std::to_string(colorInfo.bitsPerColorChannel));
        } else {
            core::Logger::Warn("LogDisplayColorInfo: DisplayConfigGetDeviceInfo failed for path " +
                                std::to_string(i));
        }
    }
}

bool CaptureScreenToImage(int width, int height, DecodedImage& out) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    HDC screenDC = GetDC(nullptr);
    if (!screenDC) {
        core::Logger::Warn("ScreenCapture: GetDC(NULL) failed");
        return false;
    }

    HDC memDC = CreateCompatibleDC(screenDC);
    if (!memDC) {
        ReleaseDC(nullptr, screenDC);
        core::Logger::Warn("ScreenCapture: CreateCompatibleDC failed");
        return false;
    }

    HBITMAP bitmap = CreateCompatibleBitmap(screenDC, width, height);
    if (!bitmap) {
        DeleteDC(memDC);
        ReleaseDC(nullptr, screenDC);
        core::Logger::Warn("ScreenCapture: CreateCompatibleBitmap failed");
        return false;
    }

    HGDIOBJ oldBitmap = SelectObject(memDC, bitmap);
    const BOOL blitOk = BitBlt(memDC, 0, 0, width, height, screenDC, 0, 0, SRCCOPY);

    bool ok = false;
    if (blitOk) {
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
        const int scanlines = GetDIBits(memDC, bitmap, 0, static_cast<UINT>(height), bgra.data(), &bmi,
                                         DIB_RGB_COLORS);
        if (scanlines == height) {
            out.width = width;
            out.height = height;
            out.rgba.assign(bgra.size(), 0);
            for (size_t i = 0; i + 3 < bgra.size(); i += 4) {
                out.rgba[i + 0] = bgra[i + 2]; // R <- B
                out.rgba[i + 1] = bgra[i + 1]; // G
                out.rgba[i + 2] = bgra[i + 0]; // B <- R
                out.rgba[i + 3] = 255;         // force opaque
            }
            ok = true;
        } else {
            core::Logger::Warn("ScreenCapture: GetDIBits failed");
        }
    } else {
        core::Logger::Warn("ScreenCapture: BitBlt failed");
    }

    SelectObject(memDC, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDC);
    ReleaseDC(nullptr, screenDC);
    return ok;
}

} // namespace platform
