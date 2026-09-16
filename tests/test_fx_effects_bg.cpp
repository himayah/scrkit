#include "test_framework.h"

#include "../src/core/effects/CoverScale.h"
#include "../src/core/effects/bg/FadeOutIn.h"
#include "../src/core/effects/bg/ParallaxTilt.h"
#include "../src/core/effects/bg/Tilt.h"
#include "../src/core/effects/bg/ZoomShake.h"

using core::fx::FadeOutInAlpha;
using core::fx::FadeOutInParams;
using core::fx::ParallaxTiltParams;
using core::fx::ParallaxTiltTransform;
using core::fx::TiltParams;
using core::fx::TiltTransform;
using core::fx::ZoomShakeParams;
using core::fx::ZoomShakeTransform;

namespace {
constexpr float kW = 1920.0f;
constexpr float kH = 1080.0f;
}

// ---- FadeOutIn (§6.2.2) ----------------------------------------------------

TEST_CASE(FadeOutIn_OutHoldInPhases) {
    const FadeOutInParams params; // holdRatio=0.2, floor=0.15
    const float duration = 10.0f;
    // Start and end: fully visible (a=1 -> alpha=1).
    CHECK_NEAR(FadeOutInAlpha(0.0f, duration, 1.0f, params), 1.0f, 1e-3f);
    CHECK_NEAR(FadeOutInAlpha(duration, duration, 1.0f, params), 1.0f, 1e-3f);
    // Middle of the hold window: fully at the floor.
    CHECK_NEAR(FadeOutInAlpha(duration * 0.5f, duration, 1.0f, params), params.floor, 1e-3f);
}

TEST_CASE(FadeOutIn_ZeroIntensityIsStaticMatch) {
    const FadeOutInParams params;
    CHECK_NEAR(FadeOutInAlpha(5.0f, 10.0f, 0.0f, params), 1.0f, 1e-6f);
}

// ---- Tilt (§6.2.4, D-19) ---------------------------------------------------

TEST_CASE(Tilt_ScaleReturnsToOneAtZeroIntensity) {
    const TiltParams params;
    const auto t = TiltTransform(1.5f, 0.0f, params, kW, kH);
    CHECK_NEAR(t.scale, 1.0f, 1e-5f);
    CHECK_NEAR(t.rotateRad, 0.0f, 1e-5f);
}

TEST_CASE(Tilt_ScaleNeverExceedsMaxAngleCoverScale) {
    const TiltParams params;
    const float maxCover = core::fx::CoverScaleForRotation(kW, kH, params.maxDeg * 3.14159265f / 180.0f);
    for (float t = 0.0f; t < 10.0f; t += 0.3f) {
        const auto tr = TiltTransform(t, 1.0f, params, kW, kH);
        CHECK(tr.scale <= maxCover + 1e-4f);
    }
}

// ---- ZoomShake (§6.2.3) ----------------------------------------------------

TEST_CASE(ZoomShake_ZeroIntensityIsStaticMatchOnRotationAndTranslation) {
    const ZoomShakeParams params;
    const auto t = ZoomShakeTransform(2.0f, 0.0f, params, 5, kW, kH);
    CHECK_NEAR(t.translate.x, 0.0f, 1e-5f);
    CHECK_NEAR(t.translate.y, 0.0f, 1e-5f);
    // scale still includes the fixed coverScale factor even at I=0 (only the
    // zoom/jitter *oscillation* term is gated by intensity, §6.2.3's formula
    // multiplies coverScale outside the I-scaled sum).
    const float coverScale = core::fx::CoverScaleForShift(kW, kH, params.shakePx, params.shakePx) *
                              (1.0f + params.zoomAmp + params.jitterAmp);
    CHECK_NEAR(t.scale, coverScale, 1e-3f);
}

// ---- ParallaxTilt (§6.2.10) -------------------------------------------------

TEST_CASE(ParallaxTilt_ZeroIntensityIsStaticMatch) {
    const ParallaxTiltParams params;
    const auto t = ParallaxTiltTransform(3.0f, 0.0f, params, 9, kW, kH);
    CHECK_NEAR(t.translate.x, 0.0f, 1e-5f);
    CHECK_NEAR(t.translate.y, 0.0f, 1e-5f);
    CHECK_NEAR(t.rotateRad, 0.0f, 1e-5f);
    CHECK_NEAR(t.scale, 1.0f, 1e-5f);
}
