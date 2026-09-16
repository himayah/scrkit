#include "test_framework.h"

#include "../src/core/effects/fg/ClothBend.h"
#include "../src/core/effects/fg/FlagWave.h"
#include "../src/core/effects/fg/LiquidDistort.h"
#include "../src/core/effects/fg/NorenSwing.h"

using core::fx::ClothBendDisplace;
using core::fx::ClothBendParams;
using core::fx::ClothBendShade;
using core::fx::FlagWaveDisplace;
using core::fx::FlagWaveParams;
using core::fx::FlagWaveShade;
using core::fx::LiquidDistortParams;
using core::fx::LiquidDistortVertex;
using core::fx::MeshVertex;
using core::fx::NorenDisplace;
using core::fx::NorenSwingParams;
using core::fx::Vec2;

namespace {
constexpr float kW = 1920.0f;
constexpr float kH = 1080.0f;
}

// ---- FlagWave (§6.1.1) ---------------------------------------------------

TEST_CASE(FlagWave_ZeroIntensityIsStaticMatch) {
    const FlagWaveParams params;
    const Vec2 d = FlagWaveDisplace({500.0f, 300.0f}, 2.5f, 0.0f, params, kW, kH);
    CHECK_NEAR(d.x, 0.0f, 1e-6f);
    CHECK_NEAR(d.y, 0.0f, 1e-6f);
    CHECK_NEAR(FlagWaveShade({500.0f, 300.0f}, 2.5f, 0.0f, params, kW), 1.0f, 1e-6f);
}

TEST_CASE(FlagWave_ShadeStaysAtOrBelowOne) {
    const FlagWaveParams params;
    for (float x = 0.0f; x < kW; x += 137.0f) {
        for (float t = 0.0f; t < 3.0f; t += 0.4f) {
            const float shade = FlagWaveShade({x, 0.0f}, t, 1.0f, params, kW);
            CHECK(shade <= 1.0f + 1e-4f);
            CHECK(shade >= 1.0f - params.shadeDepth - 1e-4f);
        }
    }
}

// ---- NorenSwing (§6.1.2) --------------------------------------------------

TEST_CASE(NorenSwing_PinnedTopEdgeNeverMoves) {
    const NorenSwingParams params;
    for (int strip = 0; strip < params.strips; ++strip) {
        for (float t = 0.0f; t < 3.0f; t += 0.5f) {
            const Vec2 d = NorenDisplace({0.0f, 0.0f}, strip, t, 1.0f, params, kW, kH);
            CHECK_NEAR(d.x, 0.0f, 1e-5f);
            CHECK_NEAR(d.y, 0.0f, 1e-5f);
        }
    }
}

TEST_CASE(NorenSwing_ZeroIntensityIsStaticMatch) {
    const NorenSwingParams params;
    const Vec2 d = NorenDisplace({100.0f, 800.0f}, 3, 1.7f, 0.0f, params, kW, kH);
    CHECK_NEAR(d.x, 0.0f, 1e-6f);
    CHECK_NEAR(d.y, 0.0f, 1e-6f);
}

// ---- ClothBend (§6.1.7) ---------------------------------------------------

TEST_CASE(ClothBend_PinnedCornersNeverMoveVertically) {
    const ClothBendParams params;
    for (float t = 0.0f; t < 3.0f; t += 0.4f) {
        CHECK_NEAR(ClothBendDisplace({0.0f, 500.0f}, t, 1.0f, params, kW, kH).y, 0.0f, 1e-4f);
        CHECK_NEAR(ClothBendDisplace({kW, 500.0f}, t, 1.0f, params, kW, kH).y, 0.0f, 1e-4f);
        CHECK_NEAR(ClothBendDisplace({500.0f, 0.0f}, t, 1.0f, params, kW, kH).y, 0.0f, 1e-4f);
    }
}

TEST_CASE(ClothBend_ZeroIntensityIsStaticMatch) {
    const ClothBendParams params;
    const Vec2 d = ClothBendDisplace({960.0f, 540.0f}, 1.2f, 0.0f, params, kW, kH);
    CHECK_NEAR(d.x, 0.0f, 1e-6f);
    CHECK_NEAR(d.y, 0.0f, 1e-6f);
    CHECK_NEAR(ClothBendShade({960.0f, 540.0f}, 1.2f, 0.0f, params, kW, kH), 1.0f, 1e-6f);
}

// ---- LiquidDistort (§6.1.8) ------------------------------------------------

TEST_CASE(LiquidDistort_ZeroIntensityIsStaticMatch) {
    const LiquidDistortParams params;
    MeshVertex rest;
    rest.pos = {960.0f, 540.0f};
    rest.uv = {0.5f, 0.5f};
    const MeshVertex out = LiquidDistortVertex(rest, 4.0f, 0.0f, params, 42, kW, kH);
    CHECK_NEAR(out.pos.x, rest.pos.x, 1e-5f);
    CHECK_NEAR(out.pos.y, rest.pos.y, 1e-5f);
    CHECK_NEAR(out.uv.x, rest.uv.x, 1e-6f);
    CHECK_NEAR(out.uv.y, rest.uv.y, 1e-6f);
}

TEST_CASE(LiquidDistort_DeterministicForSameSeedAndTime) {
    const LiquidDistortParams params;
    MeshVertex rest;
    rest.pos = {300.0f, 700.0f};
    rest.uv = {0.2f, 0.6f};
    const MeshVertex a = LiquidDistortVertex(rest, 2.5f, 1.0f, params, 7, kW, kH);
    const MeshVertex b = LiquidDistortVertex(rest, 2.5f, 1.0f, params, 7, kW, kH);
    CHECK_NEAR(a.pos.x, b.pos.x, 1e-6f);
    CHECK_NEAR(a.pos.y, b.pos.y, 1e-6f);
}
