#include "test_framework.h"

#include "../src/core/Bmp.h"
#include "../src/core/ContentMask.h"

TEST_CASE(Bmp_EncodesHeaderPaddingAndBottomUpBgrRows) {
    // 3x2 image: rows are 9 bytes -> padded to 12.
    std::vector<uint8_t> rgba = {
        255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255,       // top row: R G B
        10, 20, 30, 255, 40, 50, 60, 255, 70, 80, 90, 255};   // bottom row
    const auto bmp = core::EncodeBmp24(rgba.data(), 3, 2);
    CHECK_EQ(bmp.size(), static_cast<size_t>(54 + 12 * 2));
    CHECK(bmp[0] == 'B' && bmp[1] == 'M');
    CHECK_EQ(bmp[18], 3);            // width
    CHECK_EQ(bmp[22], 2);            // height
    CHECK_EQ(bmp[28], 24);           // bits per pixel
    // First stored row is the BOTTOM row, in BGR order.
    CHECK_EQ(bmp[54], 30);
    CHECK_EQ(bmp[55], 20);
    CHECK_EQ(bmp[56], 10);
    CHECK_EQ(bmp[54 + 9], 0);        // padding
    // Second stored row is the top row: red pixel = B0 G0 R255.
    CHECK_EQ(bmp[54 + 12 + 0], 0);
    CHECK_EQ(bmp[54 + 12 + 2], 255);
    CHECK(core::EncodeBmp24(nullptr, 3, 2).empty());
}

TEST_CASE(CompareReference_DistinguishesSameImageToneShiftAndDifferentImage) {
    const int w = 128, h = 96;
    std::vector<uint8_t> a(static_cast<size_t>(w) * h * 4), b = a, c = a, d = a;
    uint32_t s = 7;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const size_t i = (static_cast<size_t>(y) * w + x) * 4;
            const uint8_t v = static_cast<uint8_t>(40 + 170 * ((x / 16 + y / 16) % 2) + (x % 5));
            a[i] = a[i + 1] = a[i + 2] = v; a[i + 3] = 255;
            b[i] = b[i + 1] = b[i + 2] = static_cast<uint8_t>(v / 2 + 20); b[i + 3] = 255;   // same picture, darker
            s = s * 1664525u + 1013904223u;
            const uint8_t r = static_cast<uint8_t>(s >> 24);
            c[i] = c[i + 1] = c[i + 2] = r; c[i + 3] = 255;                                     // unrelated image
            d[i] = d[i + 1] = d[i + 2] = a[i]; d[i + 3] = 255;
        }
    }
    const auto same = core::CompareReference(a.data(), d.data(), w, h);
    CHECK(same.luminanceCorrelation > 0.99);
    CHECK_NEAR(same.meanCapture[0], same.meanReference[0], 1e-9);
    const auto tone = core::CompareReference(a.data(), b.data(), w, h);
    CHECK(tone.luminanceCorrelation > 0.95);               // same picture...
    CHECK(tone.meanCapture[0] > tone.meanReference[0] + 20); // ...but the reference is darker
    const auto different = core::CompareReference(a.data(), c.data(), w, h);
    CHECK(different.luminanceCorrelation < 0.5);
}
