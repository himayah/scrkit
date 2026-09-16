#include "test_framework.h"

#include <cstdint>
#include <cstdlib>
#include <vector>

#include "../src/core/effects/HueRotate.h"

using core::fx::HueRotateRgba;

TEST_CASE(HueRotate_360DegreesIsIdentity) {
    const uint8_t src[4] = {200, 60, 10, 255};
    uint8_t dst[4];
    HueRotateRgba(src, dst, 1, 1, 360.0f);
    CHECK(std::abs(static_cast<int>(dst[0]) - static_cast<int>(src[0])) <= 1);
    CHECK(std::abs(static_cast<int>(dst[1]) - static_cast<int>(src[1])) <= 1);
    CHECK(std::abs(static_cast<int>(dst[2]) - static_cast<int>(src[2])) <= 1);
    CHECK_EQ(dst[3], src[3]);
}

TEST_CASE(HueRotate_RedRotates120ToGreen) {
    const uint8_t src[4] = {255, 0, 0, 128};
    uint8_t dst[4];
    HueRotateRgba(src, dst, 1, 1, 120.0f);
    CHECK(std::abs(static_cast<int>(dst[0]) - 0) <= 3);
    CHECK(std::abs(static_cast<int>(dst[1]) - 255) <= 3);
    CHECK(std::abs(static_cast<int>(dst[2]) - 0) <= 3);
    CHECK_EQ(dst[3], src[3]); // alpha untouched
}

TEST_CASE(HueRotate_PreservesLuminanceSum) {
    const uint8_t src[4] = {120, 200, 40, 255};
    uint8_t dst[4];
    for (float deg = 0.0f; deg < 360.0f; deg += 37.0f) {
        HueRotateRgba(src, dst, 1, 1, deg);
        const int srcSum = static_cast<int>(src[0]) + src[1] + src[2];
        const int dstSum = static_cast<int>(dst[0]) + dst[1] + dst[2];
        CHECK(std::abs(srcSum - dstSum) <= 2);
    }
}

TEST_CASE(HueRotate_SmallImagesDoNotCrash) {
    std::vector<uint8_t> src1x1 = {10, 20, 30, 255};
    std::vector<uint8_t> dst1x1(4);
    HueRotateRgba(src1x1.data(), dst1x1.data(), 1, 1, 45.0f);

    std::vector<uint8_t> src3x2(3 * 2 * 4, 128);
    std::vector<uint8_t> dst3x2(3 * 2 * 4);
    HueRotateRgba(src3x2.data(), dst3x2.data(), 3, 2, 200.0f);
    // No specific value check -- just confirm every pixel's alpha survived.
    for (int i = 0; i < 3 * 2; ++i) {
        CHECK_EQ(dst3x2[i * 4 + 3], 128);
    }
}
