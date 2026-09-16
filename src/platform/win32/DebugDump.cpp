#include "DebugDump.h"

#include "WinFileIO.h"

namespace platform {

namespace {
void PutU16LE(std::string& buf, uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
}
void PutU32LE(std::string& buf, uint32_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
}
void PutI32LE(std::string& buf, int32_t v) { PutU32LE(buf, static_cast<uint32_t>(v)); }
} // namespace

bool WriteDebugBmp(const std::wstring& path, const uint8_t* rgba, int width, int height) {
    if (width <= 0 || height <= 0 || !rgba) return false;

    const int rowSize = ((width * 3 + 3) / 4) * 4; // rows padded to a 4-byte boundary
    const uint32_t pixelDataSize = static_cast<uint32_t>(rowSize) * static_cast<uint32_t>(height);
    const uint32_t fileSize = 14 + 40 + pixelDataSize;

    std::string buf;
    buf.reserve(fileSize);

    // BITMAPFILEHEADER (14 bytes)
    buf.push_back('B');
    buf.push_back('M');
    PutU32LE(buf, fileSize);
    PutU32LE(buf, 0); // reserved
    PutU32LE(buf, 14 + 40); // pixel data offset

    // BITMAPINFOHEADER (40 bytes)
    PutU32LE(buf, 40); // header size
    PutI32LE(buf, width);
    PutI32LE(buf, height); // positive = bottom-up storage
    PutU16LE(buf, 1); // planes
    PutU16LE(buf, 24); // bits per pixel
    PutU32LE(buf, 0); // BI_RGB, no compression
    PutU32LE(buf, pixelDataSize);
    PutI32LE(buf, 2835); // ~72 DPI
    PutI32LE(buf, 2835);
    PutU32LE(buf, 0); // palette colors
    PutU32LE(buf, 0); // important colors

    buf.resize(fileSize, '\0'); // pixel data area, zero-initialized (covers row padding)
    char* pixels = buf.data() + 54;
    for (int y = 0; y < height; ++y) {
        const int srcY = height - 1 - y; // BMP rows go bottom-up
        const uint8_t* srcRow = rgba + static_cast<size_t>(srcY) * width * 4;
        char* dstRow = pixels + static_cast<size_t>(y) * rowSize;
        for (int x = 0; x < width; ++x) {
            dstRow[x * 3 + 0] = static_cast<char>(srcRow[x * 4 + 2]); // B
            dstRow[x * 3 + 1] = static_cast<char>(srcRow[x * 4 + 1]); // G
            dstRow[x * 3 + 2] = static_cast<char>(srcRow[x * 4 + 0]); // R
        }
    }

    return WriteTextFileW(path, buf);
}

} // namespace platform
