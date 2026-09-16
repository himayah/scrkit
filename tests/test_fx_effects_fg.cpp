#include "test_framework.h"

#include "../src/core/ParticleGrid.h"
#include "../src/core/effects/EffectRegistry.h"
#include "../src/core/effects/fg/ClothBend.h"
#include "../src/core/effects/fg/FlagWave.h"
#include "../src/core/effects/fg/InfiniteScroll.h"
#include "../src/core/effects/fg/LiquidDistort.h"
#include "../src/core/effects/fg/NorenSwing.h"

using core::fx::ClothBendDisplace;
using core::fx::ClothBendParams;
using core::fx::ClothBendShade;
using core::fx::EffectContext;
using core::fx::EffectFrame;
using core::fx::FlagWaveDisplace;
using core::fx::FlagWaveParams;
using core::fx::FlagWaveShade;
using core::fx::LayerGeometry;
using core::fx::LayerKind;
using core::fx::LayerSource;
using core::fx::LiquidDistortParams;
using core::fx::LiquidDistortVertex;
using core::fx::MeshVertex;
using core::fx::NorenDisplace;
using core::fx::NorenSwingParams;
using core::fx::ScrollOffsetStep;
using core::fx::ScrollRestTarget;
using core::fx::TextureRole;
using core::fx::Vec2;

namespace {
LayerSource MakeForegroundLayerSource(int gridN = 8) {
    LayerSource layer;
    layer.kind = LayerKind::Foreground;
    layer.texture = TextureRole::Foreground;
    layer.screenW = 1920.0f;
    layer.screenH = 1080.0f;
    layer.gridN = gridN;
    core::ParticleGridConfig config;
    config.screenWidth = layer.screenW;
    config.screenHeight = layer.screenH;
    config.gridN = gridN;
    config.particleCount = gridN * gridN;
    layer.cells = core::BuildParticleGrid(config);
    for (size_t i = 0; i < layer.cells.size(); ++i) layer.cellIndices.push_back(static_cast<int>(i));
    layer.cellHalfW = (layer.screenW / gridN) * 0.55f;
    layer.cellHalfH = (layer.screenH / gridN) * 0.55f;
    return layer;
}
} // namespace

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

// ---- InfiniteScroll (§6.1.3) ----------------------------------------------

TEST_CASE(InfiniteScroll_OffsetStepIsPlainEulerIntegration) {
    const Vec2 offset = ScrollOffsetStep({10.0f, -5.0f}, {2.0f, 3.0f}, 0.5f);
    CHECK_NEAR(offset.x, 11.0f, 1e-6f);
    CHECK_NEAR(offset.y, -3.5f, 1e-6f);
}

TEST_CASE(InfiniteScroll_RestTargetSnapsToNearestPeriodPerAxis) {
    const Vec2 target = ScrollRestTarget({1850.0f, -1030.0f}, kW, kH);
    CHECK_NEAR(target.x, kW, 1e-3f);   // round(1850/1920) = 1 -> 1920
    CHECK_NEAR(target.y, -kH, 1e-3f);  // round(-1030/1080) = -1 -> -1080
}

TEST_CASE(InfiniteScroll_ProducesQuadsAndKeepsScrollingWithoutExit) {
    LayerSource layer = MakeForegroundLayerSource();
    auto effect = core::fx::EffectRegistry::Create(core::fx::EffectId::InfiniteScroll);
    CHECK(effect != nullptr);
    EffectContext ctx;
    ctx.layer = &layer;
    ctx.screenW = layer.screenW;
    ctx.screenH = layer.screenH;
    ctx.seed = 99;
    effect->Begin(ctx);

    LayerGeometry geometry;
    EffectFrame frame;
    frame.out = &geometry;
    frame.dt = 1.0f / 60.0f;
    frame.intensity = 1.0f;
    for (int i = 0; i < 10; ++i) {
        frame.t = i * frame.dt;
        effect->Step(frame);
    }
    CHECK(!geometry.quads.empty());
    CHECK(geometry.quads.size() % 4 == 0);
}

// ---- SegmentWave (§6.1.14) -------------------------------------------------

TEST_CASE(SegmentWave_AdjacentBandsMoveOppositeDirectionsWhenAlternating) {
    LayerSource layer = MakeForegroundLayerSource(4); // small grid: band 0 = rows 0-1, band 1 = rows 2-3
    auto effect = core::fx::EffectRegistry::Create(core::fx::EffectId::SegmentWave);
    CHECK(effect != nullptr);
    EffectContext ctx;
    ctx.layer = &layer;
    ctx.screenW = layer.screenW;
    ctx.screenH = layer.screenH;
    ctx.seed = 1;
    effect->Begin(ctx);

    LayerGeometry geometry;
    EffectFrame frame;
    frame.out = &geometry;
    frame.dt = 1.0f / 60.0f;
    frame.t = 0.3f; // pick a t where sin(...) isn't ~0
    frame.intensity = 1.0f;
    effect->Step(frame);

    // gridN=4, bandRows=2: band 0 = rows 0-1 (cell indices 0-7), band 1 =
    // rows 2-3 (cell indices 8-15). Fragment order matches cell order 1:1,
    // 4 vertices emitted per fragment, so cell 8's quad starts at index 32.
    CHECK_EQ(geometry.quads.size() % 4, static_cast<size_t>(0));
    // Average the quad's left/right corners (indices 0,1) to get the
    // fragment's own dx, cancelling out the +-halfW corner offset.
    const float band0X = (geometry.quads[0].pos.x + geometry.quads[1].pos.x) / 2.0f - layer.cells[0].x;
    const float band1X = (geometry.quads[32].pos.x + geometry.quads[33].pos.x) / 2.0f - layer.cells[8].x;
    CHECK(band0X * band1X < 0.0f); // opposite signs (alternate=true default)
}
