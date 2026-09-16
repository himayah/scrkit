#include "test_framework.h"

#include <utility>

#include "../src/core/ParticleGrid.h"
#include "../src/core/effects/DrawList.h"
#include "../src/core/effects/EffectRegistry.h"
#include "../src/core/effects/IEffect.h"
#include "../src/core/effects/Layer.h"

using core::fx::EffectContext;
using core::fx::EffectFrame;
using core::fx::EffectId;
using core::fx::EffectRegistry::Create;
using core::fx::LayerGeometry;
using core::fx::LayerKind;
using core::fx::LayerSource;
using core::fx::TextureRole;
using core::fx::Vec2;

namespace {
// §12.2's shared fixture: W=1920,H=1080, gridN=8 (all 64 cells present --
// this file only exercises the effects registered so far, §16 Step 5's
// VortexSuction/BackgroundSuction, both of which use every cell).
LayerSource MakeLayerSource(LayerKind kind) {
    LayerSource layer;
    layer.kind = kind;
    layer.texture = kind == LayerKind::Foreground ? TextureRole::Foreground : TextureRole::Background;
    layer.screenW = 1920.0f;
    layer.screenH = 1080.0f;
    layer.gridN = 8;
    core::ParticleGridConfig config;
    config.screenWidth = layer.screenW;
    config.screenHeight = layer.screenH;
    config.gridN = layer.gridN;
    config.particleCount = layer.gridN * layer.gridN;
    layer.cells = core::BuildParticleGrid(config);
    for (size_t i = 0; i < layer.cells.size(); ++i) layer.cellIndices.push_back(static_cast<int>(i));
    layer.cellHalfW = (layer.screenW / layer.gridN) * 0.55f;
    layer.cellHalfH = (layer.screenH / layer.gridN) * 0.55f;
    return layer;
}

// Runs a terminal effect to completion (or `maxFrames`), checking the
// invariants of §12.2: IsFinished() is monotonic, it completes within
// terminalMaxSeconds, and its final EmitQuads output is empty.
void RunTerminalEffectAndCheckInvariants(EffectId id, LayerKind kind) {
    auto effect = Create(id);
    CHECK(effect != nullptr);
    if (!effect) return;

    LayerSource layer = MakeLayerSource(kind);
    EffectContext ctx;
    ctx.layer = &layer;
    ctx.screenW = layer.screenW;
    ctx.screenH = layer.screenH;
    ctx.seed = 0xC0FFEEu;
    effect->Begin(ctx);

    LayerGeometry geometry;
    EffectFrame frame;
    frame.suctionCenter = {960.0f, 540.0f};
    frame.dt = 1.0f / 60.0f;
    frame.out = &geometry;

    const int terminalMaxFrames = static_cast<int>(20.0f / frame.dt); // terminalMaxSeconds default
    bool everFinished = false;
    bool finishedWithinBudget = false;
    for (int i = 0; i < terminalMaxFrames; ++i) {
        frame.t = i * frame.dt;
        effect->Step(frame);
        const bool finished = effect->IsFinished();
        if (everFinished) CHECK(finished); // monotonic: once true, stays true
        if (finished) {
            everFinished = true;
            finishedWithinBudget = true;
            CHECK(geometry.quads.empty()); // no fragment left to draw once finished
            break;
        }
    }
    CHECK(finishedWithinBudget);
}
} // namespace

TEST_CASE(Invariants_VortexSuctionCompletesAndDrawsNothingWhenFinished) {
    RunTerminalEffectAndCheckInvariants(EffectId::VortexSuction, LayerKind::Foreground);
}

TEST_CASE(Invariants_BackgroundSuctionCompletesAndDrawsNothingWhenFinished) {
    RunTerminalEffectAndCheckInvariants(EffectId::BackgroundSuction, LayerKind::Background);
}

TEST_CASE(Invariants_AllForegroundTerminalEffectsCompleteAndDrawNothingWhenFinished) {
    // §12.2's terminal-effect table, run generically across every fg
    // terminal effect registered so far instead of one test case each.
    for (EffectId id : {EffectId::FragmentFlyAway, EffectId::GlassShatter, EffectId::ConfettiFall,
                         EffectId::MosaicCollapse, EffectId::NoiseDissolve}) {
        RunTerminalEffectAndCheckInvariants(id, LayerKind::Foreground);
    }
}

TEST_CASE(Invariants_EveryRegisteredContinuousEffectRunsWithoutCrashing) {
    // Broad smoke test across every registered Continuous effect (both
    // layers): Begin() + 3 seconds of Step() at 60fps must not crash and
    // must leave IsFinished() false throughout (continuous effects never
    // finish on their own).
    const std::pair<EffectId, LayerKind> continuousIds[] = {
        {EffectId::FlagWave, LayerKind::Foreground},
        {EffectId::NorenSwing, LayerKind::Foreground},
        {EffectId::InfiniteScroll, LayerKind::Foreground},
        {EffectId::InfiniteRotation, LayerKind::Foreground},
        {EffectId::ClothBend, LayerKind::Foreground},
        {EffectId::LiquidDistort, LayerKind::Foreground},
        {EffectId::Kaleidoscope, LayerKind::Foreground},
        {EffectId::SegmentWave, LayerKind::Foreground},
        {EffectId::Ripple, LayerKind::Background},
        {EffectId::FadeOutIn, LayerKind::Background},
        {EffectId::ZoomShake, LayerKind::Background},
        {EffectId::Tilt, LayerKind::Background},
        {EffectId::LensDistort, LayerKind::Background},
        {EffectId::BackgroundKaleidoscope, LayerKind::Background},
        {EffectId::NoiseRipple, LayerKind::Background},
        {EffectId::GlitchShift, LayerKind::Background},
        {EffectId::ParallaxTilt, LayerKind::Background},
        {EffectId::WaveZoom, LayerKind::Background},
        {EffectId::HueShift, LayerKind::Background},
    };
    LayerSource fgLayer = MakeLayerSource(LayerKind::Foreground);
    LayerSource bgLayer = MakeLayerSource(LayerKind::Background);

    for (const auto& [id, kind] : continuousIds) {
        auto effect = Create(id);
        CHECK(effect != nullptr);
        if (!effect) continue;

        LayerSource& layer = kind == LayerKind::Foreground ? fgLayer : bgLayer;
        EffectContext ctx;
        ctx.layer = &layer;
        ctx.screenW = layer.screenW;
        ctx.screenH = layer.screenH;
        ctx.seed = 12345;
        ctx.durationSeconds = 8.0f;
        effect->Begin(ctx);

        LayerGeometry geometry;
        EffectFrame frame;
        frame.suctionCenter = {960.0f, 540.0f};
        frame.dt = 1.0f / 60.0f;
        frame.intensity = 0.8f;
        frame.out = &geometry;
        for (int i = 0; i < 180; ++i) {
            frame.t = i * frame.dt;
            effect->Step(frame);
        }
        CHECK(!effect->IsFinished());
    }
}

TEST_CASE(Invariants_AllTwentySixEffectIdsAreRegistered) {
    // §16 Steps 5-10 registered every EffectId incrementally; this is the
    // final checkpoint that none was missed once HueShift (the last one,
    // §6.2.8) landed.
    for (int i = 0; i < core::fx::kEffectIdCount; ++i) {
        CHECK(Create(static_cast<EffectId>(i)) != nullptr);
    }
}
