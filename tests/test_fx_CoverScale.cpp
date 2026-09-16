#include "test_framework.h"

#include <cmath>

#include "../src/core/effects/CoverScale.h"

using core::fx::CoverScaleForDisplacement;
using core::fx::CoverScaleForRotation;
using core::fx::CoverScaleForShift;

namespace {
constexpr float kPi = 3.14159265358979323846f;

// After scaling the WxH layer by `scale` and rotating it by theta about its
// own center, does it still fully cover the unrotated WxH screen rect?
// Equivalent to: every screen corner, inverse-transformed back into the
// layer's local space, lands within [-W/2, W/2] x [-H/2, H/2].
bool RotatedScaledLayerCoversScreen(float w, float h, float thetaRad, float scale) {
    const float corners[4][2] = {{0, 0}, {w, 0}, {0, h}, {w, h}};
    const float c = std::cos(-thetaRad), s = std::sin(-thetaRad);
    for (const auto& corner : corners) {
        const float dx = corner[0] - w / 2.0f;
        const float dy = corner[1] - h / 2.0f;
        const float rx = (dx * c - dy * s) / scale;
        const float ry = (dx * s + dy * c) / scale;
        if (std::fabs(rx) > w / 2.0f + 1e-2f) return false;
        if (std::fabs(ry) > h / 2.0f + 1e-2f) return false;
    }
    return true;
}
} // namespace

TEST_CASE(CoverScale_RotationIsOneAtZeroAngle) {
    CHECK_NEAR(CoverScaleForRotation(1920.0f, 1080.0f, 0.0f), 1.0f, 1e-5f);
}

TEST_CASE(CoverScale_RotationAt90DegreesIsAspectSwap) {
    const float w = 1920.0f, h = 1080.0f;
    const float expected = std::max(h / w, w / h);
    CHECK_NEAR(CoverScaleForRotation(w, h, kPi / 2.0f), expected, 1e-3f);
}

TEST_CASE(CoverScale_RotationCoversAllFourCornersForVariousAnglesAndAspects) {
    const float sizes[][2] = {{1920, 1080}, {2560, 1080}, {1080, 1920}, {1000, 1000}};
    for (const auto& wh : sizes) {
        for (float deg = 0.0f; deg <= 90.0f; deg += 7.0f) {
            const float theta = deg * kPi / 180.0f;
            const float scale = CoverScaleForRotation(wh[0], wh[1], theta);
            CHECK(RotatedScaledLayerCoversScreen(wh[0], wh[1], theta, scale));
        }
    }
}

TEST_CASE(CoverScale_ShiftMatchesFormula) {
    const float w = 1920.0f, h = 1080.0f;
    CHECK_NEAR(CoverScaleForShift(w, h, 0.0f, 0.0f), 1.0f, 1e-6f);
    CHECK_NEAR(CoverScaleForShift(w, h, 100.0f, 0.0f), 1.0f + 200.0f / w, 1e-6f);
    CHECK_NEAR(CoverScaleForShift(w, h, 0.0f, 50.0f), 1.0f + 100.0f / h, 1e-6f);
}

TEST_CASE(CoverScale_DisplacementMatchesFormula) {
    const float w = 1920.0f, h = 1080.0f;
    CHECK_NEAR(CoverScaleForDisplacement(w, h, 0.0f), 1.0f, 1e-6f);
    CHECK_NEAR(CoverScaleForDisplacement(w, h, 54.0f), 1.0f + 108.0f / h, 1e-6f);
}
