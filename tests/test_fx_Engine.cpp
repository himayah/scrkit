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

TEST_CASE(Engine_DisabledBackgroundStaysAtRestUntilRequestTerminal) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.enabled = false;
    Mt19937RandomSource rng(1234);
    EffectEngine engine(config, rng);
    engine.SetLayers(MakeLayerSource(LayerKind::Foreground), MakeLayerSource(LayerKind::Background));
    engine.OnPhaseEntered(SaverState::STATE_CONTENT);

    EffectEngine::Inputs in;
    in.dt = 1.0f / 60.0f;
    for (int i = 0; i < 10; ++i) engine.Update(in);

    // Background: Idle (renders like Rest -- plain rest mesh, no fragments)
    // until the phase machine requests its terminal.
    const auto& list = engine.DrawList();
    CHECK_EQ(list.batches.size(), static_cast<size_t>(2));
    CHECK(list.batches[0].mesh != nullptr); // background's rest mesh batch
    CHECK(list.batches[0].quads == nullptr);

    engine.OnPhaseEntered(SaverState::STATE_BACKGROUND);
    engine.Update(in);
    const auto& list2 = engine.DrawList();
    CHECK(list2.batches[0].quads != nullptr); // now BackgroundSuction fragments
}

TEST_CASE(Engine_ConsumedFlagsOnlySetWhenLayerActuallyConsumed) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.enabled = false;
    config.terminalMaxSeconds = 1.0f; // small enough to reach Consumed within the test loop
    Mt19937RandomSource rng(1234);
    EffectEngine engine(config, rng);
    // A tiny 1x1 grid so VortexSuction's single fragment reaches the center
    // (and IsFinished()) quickly.
    engine.SetLayers(MakeLayerSource(LayerKind::Foreground, 1), MakeLayerSource(LayerKind::Background, 1));
    engine.OnPhaseEntered(SaverState::STATE_CONTENT);

    EffectEngine::Inputs in;
    in.dt = 1.0f / 60.0f;
    in.suctionCenter = {960.0f, 540.0f};
    bool everForegroundConsumed = false;
    for (int i = 0; i < 600; ++i) {
        auto out = engine.Update(in);
        CHECK(!out.backgroundConsumed); // background never ran (still Idle), must never claim consumed
        if (out.foregroundConsumed) {
            everForegroundConsumed = true;
            break;
        }
    }
    CHECK(everForegroundConsumed);
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
