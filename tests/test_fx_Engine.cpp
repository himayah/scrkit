#include "test_framework.h"

#include "../src/core/ParticleGrid.h"
#include "../src/core/RandomSource.h"
#include "../src/core/effects/EffectEngine.h"

using core::Mt19937RandomSource;
using core::SaverState;
using core::fx::EffectEngine;
using core::fx::EffectId;
using core::fx::EmptyReason;
using core::fx::EngineConfig;
using core::fx::FxState;
using core::fx::LayerKind;
using core::fx::LayerSource;
using core::fx::MakeDefaultEngineConfig;
using core::fx::TextureRole;

namespace {
LayerSource MakeLayerSource(LayerKind kind, int gridN = 8) {
    LayerSource layer;
    layer.kind = kind;
    layer.texture = kind == LayerKind::Foreground ? TextureRole::Foreground : TextureRole::Background;
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

TEST_CASE(Engine_DisabledForegroundStartsVortexSuctionImmediately) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.enabled = false; // v1-reproduction path (§9.4)
    Mt19937RandomSource rng(1234);
    EffectEngine engine(config, rng);
    engine.SetLayers(MakeLayerSource(LayerKind::Foreground), MakeLayerSource(LayerKind::Background));
    engine.OnPhaseEntered(SaverState::STATE_CONTENT);

    EffectEngine::Inputs in;
    in.dt = 1.0f / 60.0f;
    in.suctionCenter = {960.0f, 540.0f};
    engine.Update(in); // first frame: rt.current should already be VortexSuction/BackgroundSuction

    // Foreground: TerminalRunning with VortexSuction fragment quads drawn.
    const auto& list = engine.DrawList();
    CHECK_EQ(list.batches.size(), static_cast<size_t>(2)); // background batch + foreground batch
    CHECK(list.batches[0].texture == TextureRole::Background); // background drawn first (§4.6)
    CHECK(list.batches[1].texture == TextureRole::Foreground);
    CHECK(list.batches[1].quads != nullptr);
    CHECK(!list.batches[1].quads->empty());
}

TEST_CASE(Engine_DisabledBackgroundStaysAtRestSinceNoContinuousCandidateExists) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.enabled = false; // no candidates at all (fg falls back to VortexSuction; bg has none to fall back to)
    Mt19937RandomSource rng(1234);
    EffectEngine engine(config, rng);
    engine.SetLayers(MakeLayerSource(LayerKind::Foreground), MakeLayerSource(LayerKind::Background));
    engine.OnPhaseEntered(SaverState::STATE_CONTENT);

    EffectEngine::Inputs in;
    in.dt = 1.0f / 60.0f;
    for (int i = 0; i < 10; ++i) engine.Update(in);

    // Background: Idle (renders like Rest -- plain rest mesh, no fragments)
    // and stays that way -- background never has a terminal to fall back to
    // anymore, so with every continuous effect disabled it just sits still.
    const auto& list = engine.DrawList();
    CHECK_EQ(list.batches.size(), static_cast<size_t>(2));
    CHECK(list.batches[0].mesh != nullptr); // background's rest mesh batch
    CHECK(list.batches[0].quads == nullptr);

    // STATE_FADEOUT no longer forces anything -- background stays exactly as
    // it was (still Idle, still no fragments).
    engine.OnPhaseEntered(SaverState::STATE_FADEOUT);
    engine.Update(in);
    const auto& list2 = engine.DrawList();
    CHECK(list2.batches[0].quads == nullptr);
}

TEST_CASE(Engine_ForegroundNeverStaysConsumedItSelfLoops) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.enabled = false; // v1-reproduction path: forces VortexSuction every cycle (§9.4)
    config.terminalMaxSeconds = 3.0f; // bounds each cycle's worst case; real convergence is faster
    Mt19937RandomSource rng(1234);
    EffectEngine engine(config, rng);
    // Default 8x8 grid: cells start away from the suction center, so quads
    // stay visible for several frames each cycle instead of the degenerate
    // (near-)zero-distance case a 1x1 grid centered on suctionCenter would be.
    engine.SetLayers(MakeLayerSource(LayerKind::Foreground), MakeLayerSource(LayerKind::Background));
    engine.OnPhaseEntered(SaverState::STATE_CONTENT);

    EffectEngine::Inputs in;
    in.dt = 1.0f / 60.0f;
    in.suctionCenter = {960.0f, 540.0f};
    // Foreground content should never sit empty for more than an instant:
    // every time its VortexSuction fragment finishes, it must immediately
    // reappear and start sucking in again -- across many full cycles, the
    // foreground batch's quads must repeatedly appear, disappear, and
    // reappear rather than staying gone after the first completion.
    bool everSawForegroundQuads = false;
    int cyclesCompleted = 0;
    bool hadQuadsLastFrame = false;
    for (int i = 0; i < 3000 && cyclesCompleted < 3; ++i) {
        engine.Update(in);
        const auto& list = engine.DrawList();
        const bool hasQuadsNow = list.batches.size() > 1 && list.batches[1].quads && !list.batches[1].quads->empty();
        if (hasQuadsNow) everSawForegroundQuads = true;
        if (hadQuadsLastFrame && !hasQuadsNow) ++cyclesCompleted; // fragment finished once -> should reappear
        hadQuadsLastFrame = hasQuadsNow;
    }
    CHECK(everSawForegroundQuads);
    CHECK_EQ(cyclesCompleted, 3); // reached the loop bound via completions, not the frame-count cap
}

TEST_CASE(Engine_PhaseEnteredResetTransitionsBothLayersToRest) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.enabled = false;
    Mt19937RandomSource rng(1234);
    EffectEngine engine(config, rng);
    engine.SetLayers(MakeLayerSource(LayerKind::Foreground), MakeLayerSource(LayerKind::Background));
    engine.OnPhaseEntered(SaverState::STATE_CONTENT);

    EffectEngine::Inputs in;
    in.dt = 1.0f / 60.0f;
    engine.Update(in);

    engine.OnPhaseEntered(SaverState::STATE_RESET);
    engine.Update(in);
    const auto& list = engine.DrawList();
    // Rest draws exactly like Idle: one plain mesh batch per layer, no quads.
    CHECK_EQ(list.batches.size(), static_cast<size_t>(2));
    for (const auto& batch : list.batches) {
        CHECK(batch.mesh != nullptr);
        CHECK(batch.quads == nullptr);
    }
}
