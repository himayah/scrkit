#include "Bmp.h"

#include <cstddef>

namespace core {

namespace {
void PutU16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x));
    v.push_back(static_cast<uint8_t>(x >> 8));
}
void PutU32(std::vector<uint8_t>& v, uint32_t x) {
    for (int i = 0; i < 4; ++i) v.push_back(static_cast<uint8_t>(x >> (8 * i)));
}
} // namespace

std::vector<uint8_t> EncodeBmp24(const uint8_t* rgba, int width, int height) {
    std::vector<uint8_t> out;
    if (!rgba || width <= 0 || height <= 0) return out;
    const uint32_t rowBytes = (static_cast<uint32_t>(width) * 3 + 3) & ~3u;
    const uint32_t imageBytes = rowBytes * static_cast<uint32_t>(height);
    out.reserve(54 + imageBytes);
    out.push_back('B');
    out.push_back('M');
    PutU32(out, 54 + imageBytes);
    PutU32(out, 0);
    PutU32(out, 54);
    PutU32(out, 40);                       // BITMAPINFOHEADER
    PutU32(out, static_cast<uint32_t>(width));
    PutU32(out, static_cast<uint32_t>(height));
    PutU16(out, 1);
    PutU16(out, 24);
    PutU32(out, 0);                        // BI_RGB
    PutU32(out, imageBytes);
    PutU32(out, 2835);
    PutU32(out, 2835);
    PutU32(out, 0);
    PutU32(out, 0);
    for (int y = height - 1; y >= 0; --y) {
        const uint8_t* row = rgba + static_cast<size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x) {
            out.push_back(row[x * 4 + 2]); // B
            out.push_back(row[x * 4 + 1]); // G
            out.push_back(row[x * 4 + 0]); // R
        }
        for (uint32_t pad = width * 3; pad < rowBytes; ++pad) out.push_back(0);
    }
    return out;
}

} // namespace core
