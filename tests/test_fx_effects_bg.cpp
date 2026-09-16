#include "test_framework.h"

#include <algorithm>
#include <cmath>

#include "../src/core/RandomSource.h"
#include "../src/core/effects/CoverScale.h"
#include "../src/core/effects/bg/FadeOutIn.h"
#include "../src/core/effects/bg/GlitchShift.h"
#include "../src/core/effects/bg/HueShift.h"
#include "../src/core/effects/bg/LensDistort.h"
#include "../src/core/effects/bg/NoiseRipple.h"
#include "../src/core/effects/bg/ParallaxTilt.h"
#include "../src/core/effects/bg/Ripple.h"
#include "../src/core/effects/bg/Tilt.h"
#include "../src/core/effects/bg/WaveZoom.h"
#include "../src/core/effects/bg/ZoomShake.h"

using core::fx::FadeOutInAlpha;
using core::fx::FadeOutInParams;
using core::fx::LensDisplace;
using core::fx::LensDistortParams;
using core::fx::NoiseRippleDisplace;
using core::fx::NoiseRippleParams;
using core::fx::ParallaxTiltParams;
using core::fx::ParallaxTiltTransform;
using core::fx::RippleDisplace;
using core::fx::RippleDrop;
using core::fx::RippleParams;
using core::fx::TiltParams;
using core::fx::TiltTransform;
using core::fx::Vec2;
using core::fx::WaveZoomDisplace;
using core::fx::WaveZoomParams;
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

// ---- Ripple (§6.2.1) -------------------------------------------------------

TEST_CASE(Ripple_DropBeforeItsSpawnTimeContributesNothing) {
    const RippleParams params;
    const std::vector<RippleDrop> drops = {{{960.0f, 540.0f}, 5.0f}}; // spawns at t=5
    const Vec2 d = RippleDisplace({960.0f, 600.0f}, 2.0f, drops, 1.0f, params); // t=2 < spawnTime
    CHECK_NEAR(d.x, 0.0f, 1e-6f);
    CHECK_NEAR(d.y, 0.0f, 1e-6f);
}

TEST_CASE(Ripple_DecaysTowardsZeroOverTime) {
    const RippleParams params;
    const std::vector<RippleDrop> drops = {{{960.0f, 540.0f}, 0.0f}};
    const Vec2 near = RippleDisplace({960.0f, 600.0f}, 0.3f, drops, 1.0f, params);
    const Vec2 far = RippleDisplace({960.0f, 600.0f}, 20.0f, drops, 1.0f, params);
    const float nearMag = std::sqrt(near.x * near.x + near.y * near.y);
    const float farMag = std::sqrt(far.x * far.x + far.y * far.y);
    CHECK(farMag < nearMag);
    CHECK(farMag < 1e-3f); // effectively decayed away after 20s
}

// ---- LensDistort (§6.2.5) --------------------------------------------------

TEST_CASE(LensDistort_CornersStayFixedForAnyK) {
    for (float k : {-0.15f, 0.0f, 0.15f}) {
        const Vec2 corners[4] = {{0, 0}, {kW, 0}, {0, kH}, {kW, kH}};
        for (const auto& corner : corners) {
            const Vec2 p = LensDisplace(corner, k, kW, kH);
            CHECK_NEAR(p.x, corner.x, 1e-2f);
            CHECK_NEAR(p.y, corner.y, 1e-2f);
        }
    }
}

TEST_CASE(LensDistort_CenterMovesWithK) {
    const Vec2 c{kW / 2.0f, kH / 2.0f};
    const Vec2 offCenter{kW / 2.0f + 400.0f, kH / 2.0f};
    const Vec2 atZero = LensDisplace(offCenter, 0.0f, kW, kH);
    const Vec2 atPositive = LensDisplace(offCenter, 0.1f, kW, kH);
    CHECK_NEAR(atZero.x, offCenter.x, 1e-2f); // k=0 is identity
    CHECK(std::fabs(atPositive.x - c.x) != std::fabs(atZero.x - c.x));
}

// ---- NoiseRipple (§6.2.7) --------------------------------------------------

TEST_CASE(NoiseRipple_ZeroIntensityIsStaticMatch) {
    const NoiseRippleParams params;
    const Vec2 d = NoiseRippleDisplace({700.0f, 300.0f}, 1.5f, 0.0f, params, 3, kW, kH);
    CHECK_NEAR(d.x, 0.0f, 1e-6f);
    CHECK_NEAR(d.y, 0.0f, 1e-6f);
}

// ---- WaveZoom (§6.2.11) -----------------------------------------------------

TEST_CASE(WaveZoom_ZeroIntensityIsStaticMatch) {
    const WaveZoomParams params;
    const Vec2 rest{300.0f, 800.0f};
    const Vec2 p = WaveZoomDisplace(rest, 2.0f, 0.0f, params, true, kW, kH);
    CHECK_NEAR(p.x, rest.x, 1e-3f);
    CHECK_NEAR(p.y, rest.y, 1e-3f);
}

TEST_CASE(WaveZoom_CoverScaleKeepsCenterFixed) {
    const WaveZoomParams params;
    const Vec2 c{kW / 2.0f, kH / 2.0f};
    const Vec2 p = WaveZoomDisplace(c, 0.4f, 1.0f, params, true, kW, kH);
    CHECK_NEAR(p.x, c.x, 1e-2f);
    CHECK_NEAR(p.y, c.y, 1e-2f);
}

// ---- GlitchShift (§6.2.9) ---------------------------------------------------

TEST_CASE(GlitchShift_BandsCoverScreenHeightExactlyWithNoGapOrOverlap) {
    core::Mt19937RandomSource rng(123);
    const core::fx::GlitchShiftParams params;
    for (int trial = 0; trial < 20; ++trial) {
        const auto bands = core::fx::MakeGlitchBands(rng, kH, params, 1.0f, kW);
        CHECK(!bands.empty());
        CHECK_NEAR(bands.front().y0, 0.0f, 1e-3f);
        CHECK_NEAR(bands.back().y1, kH, 1e-3f);
        for (size_t i = 1; i < bands.size(); ++i) {
            CHECK_NEAR(bands[i].y0, bands[i - 1].y1, 1e-3f); // no gap, no overlap
        }
    }
}

// ---- HueShift (§6.2.8) ------------------------------------------------------

TEST_CASE(HueShift_SelectHueRingsAtStepBoundaryIsPureIndexA) {
    const auto sel = core::fx::SelectHueRings(0.0f, 6); // 0deg = exactly ring 0
    CHECK_EQ(sel.indexA, 0);
    CHECK_EQ(sel.indexB, 1);
    CHECK_NEAR(sel.blendB, 0.0f, 1e-4f);
}

TEST_CASE(HueShift_SelectHueRingsMidwayBetweenStepsBlendsHalfway) {
    const auto sel = core::fx::SelectHueRings(30.0f, 6); // steps are 60deg apart; 30 is the midpoint of ring 0->1
    CHECK_EQ(sel.indexA, 0);
    CHECK_EQ(sel.indexB, 1);
    CHECK_NEAR(sel.blendB, 0.5f, 1e-4f);
}

TEST_CASE(HueShift_SelectHueRingsWrapsAroundAt360) {
    const auto sel = core::fx::SelectHueRings(359.0f, 6); // just before wrapping back to ring 0
    CHECK_EQ(sel.indexA, 5);
    CHECK_EQ(sel.indexB, 0); // (5+1) % 6
}

TEST_CASE(HueShift_SelectHueRingsHandlesNegativeInputGracefully) {
    const auto selNeg = core::fx::SelectHueRings(-30.0f, 6);
    const auto selPos = core::fx::SelectHueRings(330.0f, 6); // -30 mod 360 == 330
    CHECK_EQ(selNeg.indexA, selPos.indexA);
    CHECK_NEAR(selNeg.blendB, selPos.blendB, 1e-4f);
}

