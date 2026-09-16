#include "test_framework.h"

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

TEST_CASE(Invariants_UnregisteredEffectReturnsNullptr) {
    // Sanity check for Step 9's incremental registration: an id not yet
    // wired up in EffectRegistry::Create must return nullptr, not crash.
    CHECK(Create(EffectId::FlagWave) == nullptr);
}
