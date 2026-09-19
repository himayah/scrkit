#include "test_framework.h"

#include <set>

#include "../src/core/ParticleGrid.h"
#include "../src/core/RandomSource.h"
#include "../src/core/effects/EffectEngine.h"
#include "../src/core/effects/EffectScheduler.h"

using core::Mt19937RandomSource;
using core::SaverState;
using core::fx::EffectEngine;
using core::fx::EffectId;
using core::fx::EffectKind;
using core::fx::EngineConfig;
using core::fx::FxState;
using core::fx::LayerDirective;
using core::fx::LayerKind;
using core::fx::LayerMode;
using core::fx::LayerSource;
using core::fx::MakeDefaultEngineConfig;
using core::fx::TextureRole;
namespace Scheduler = core::fx::EffectScheduler;

namespace {
LayerSource MakeLayer(LayerKind kind, int gridN = 8) {
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

LayerDirective Pin(EffectId id, bool loopTerminal = true) {
    LayerDirective d;
    d.mode = LayerMode::Pin;
    d.pinned = id;
    d.loopTerminal = loopTerminal;
    return d;
}

LayerDirective Rest() {
    LayerDirective d;
    d.mode = LayerMode::Rest;
    return d;
}

struct Rig {
    Mt19937RandomSource rng;
    EffectEngine engine;
    explicit Rig(uint32_t seed = 42, const EngineConfig& config = MakeDefaultEngineConfig())
        : rng(seed), engine(config, rng) {
        engine.SetLayers(MakeLayer(LayerKind::Foreground), MakeLayer(LayerKind::Background));
        engine.OnPhaseEntered(SaverState::STATE_CONTENT);
    }
    void Run(float seconds, float dt = 1.0f / 30.0f) {
        EffectEngine::Inputs in;
        in.dt = dt;
        in.suctionCenter = {960.0f, 540.0f};
        for (float t = 0.0f; t < seconds; t += dt) engine.Update(in);
    }
};
} // namespace

TEST_CASE(Directive_SchedulerPinIgnoresHistoryAndEnabledFlag) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.background.perEffect[EffectId::Ripple].enabled = false;
    config.backgroundDirective = Pin(EffectId::Ripple);
    Mt19937RandomSource rng(1);
    std::deque<core::fx::TimelineEntry> history;
    for (int i = 0; i < 5; ++i) {
        history.push_back({LayerKind::Background, EffectId::Ripple, 0.0f, 5.0f, static_cast<uint32_t>(i)});
    }
    for (int i = 0; i < 20; ++i) {
        auto pick = Scheduler::Pick(EffectKind::Continuous, LayerKind::Background, config, history, rng);
        CHECK(pick.has_value());
        if (pick) CHECK(pick->id == EffectId::Ripple);
    }
}

TEST_CASE(Directive_SchedulerPinOnlyAnswersItsOwnKind) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.foregroundDirective = Pin(EffectId::VortexSuction); // terminal
    Mt19937RandomSource rng(2);
    std::deque<core::fx::TimelineEntry> history;
    CHECK(!Scheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, config, history, rng).has_value());
    auto terminal = Scheduler::Pick(EffectKind::Terminal, LayerKind::Foreground, config, history, rng);
    CHECK(terminal.has_value());
    if (terminal) CHECK(terminal->id == EffectId::VortexSuction);
}

TEST_CASE(Directive_SchedulerRestAndForeignEffectYieldNoCandidate) {
    EngineConfig config = MakeDefaultEngineConfig();
    Mt19937RandomSource rng(3);
    std::deque<core::fx::TimelineEntry> history;
    config.backgroundDirective = Rest();
    CHECK(!Scheduler::Pick(EffectKind::Continuous, LayerKind::Background, config, history, rng).has_value());
    // A foreground-only effect pinned on the background layer is not a valid pin.
    config.backgroundDirective = Pin(EffectId::FlagWave);
    CHECK(!Scheduler::Pick(EffectKind::Continuous, LayerKind::Background, config, history, rng).has_value());
}

TEST_CASE(Directive_IgnoredWhileEffectSystemDisabled) {
    EngineConfig config = MakeDefaultEngineConfig();
    config.enabled = false;
    config.backgroundDirective = Pin(EffectId::Ripple);
    CHECK(core::fx::ActiveDirectiveFor(config, LayerKind::Background).mode == LayerMode::Auto);
}

TEST_CASE(Directive_PinnedBackgroundStaysPinnedAcrossManyReselections) {
    Rig rig;
    rig.engine.SetDirective(LayerKind::Background, Pin(EffectId::Ripple));
    std::set<EffectId> seen;
    EffectEngine::Inputs in;
    in.dt = 1.0f / 30.0f;
    // Long enough for many natural expiries (default durations are seconds).
    for (int frame = 0; frame < 30 * 120; ++frame) {
        rig.engine.Update(in);
        const auto status = rig.engine.Status(LayerKind::Background);
        if (frame > 30 * 3 && status.hasEffect && (status.state == FxState::Running || status.state == FxState::Entering)) {
            seen.insert(status.effect);
        }
    }
    CHECK_EQ(seen.size(), static_cast<size_t>(1));
    CHECK(seen.count(EffectId::Ripple) == 1);
}

TEST_CASE(Directive_PinnedForegroundContinuousSurvivesTheShowcaseTimer) {
    // Auto would switch the foreground to a terminal effect after
    // foregroundShowcaseSeconds; a pinned continuous effect must not.
    EngineConfig config = MakeDefaultEngineConfig();
    config.foregroundShowcaseSeconds = 5.0f;
    Rig rig(7, config);
    rig.engine.SetDirective(LayerKind::Foreground, Pin(EffectId::FlagWave));
    rig.Run(40.0f);
    const auto status = rig.engine.Status(LayerKind::Foreground);
    CHECK(status.hasEffect);
    CHECK(status.effect == EffectId::FlagWave);
    CHECK(status.state != FxState::TerminalRunning);
}

TEST_CASE(Directive_RestLayerAppliesNoEffectAndDrawsRestMesh) {
    Rig rig;
    rig.Run(1.0f);
    rig.engine.SetDirective(LayerKind::Foreground, Rest());
    rig.Run(2.0f); // let any running effect ease out
    CHECK(rig.engine.Status(LayerKind::Foreground).state == FxState::Rest);
    // The background is unaffected by the foreground's directive.
    CHECK(rig.engine.Status(LayerKind::Background).hasEffect);

    const auto& list = rig.engine.DrawList();
    CHECK_EQ(list.batches.size(), static_cast<size_t>(2));
    CHECK(list.batches[1].texture == TextureRole::Foreground);
    CHECK(list.batches[1].mesh != nullptr); // static rest mesh, not effect geometry
}

TEST_CASE(Directive_ForegroundRestWithBackgroundPinIsThePreviewBackgroundOnlyCase) {
    Rig rig;
    rig.engine.SetDirective(LayerKind::Foreground, Rest());
    rig.engine.SetDirective(LayerKind::Background, Pin(EffectId::Tilt));
    rig.Run(3.0f);
    CHECK(rig.engine.Status(LayerKind::Foreground).state == FxState::Rest);
    const auto bg = rig.engine.Status(LayerKind::Background);
    CHECK(bg.hasEffect);
    CHECK(bg.effect == EffectId::Tilt);
}

TEST_CASE(Directive_PinnedTerminalLoopsAndReRunsOnBothLayers) {
    Rig rig;
    rig.engine.SetDirective(LayerKind::Foreground, Pin(EffectId::VortexSuction));
    rig.engine.SetDirective(LayerKind::Background, Pin(EffectId::BackgroundSuction));

    // A re-run shows up as the effect's own elapsed time restarting from ~0
    // (the state can stay TerminalRunning across the restart, so state edges
    // alone can't see it).
    int fgRuns = 1, bgRuns = 1; // the run started by SetDirective itself
    float fgPrev = 0.0f, bgPrev = 0.0f;
    EffectEngine::Inputs in;
    in.dt = 1.0f / 30.0f;
    in.suctionCenter = {960.0f, 540.0f};
    for (int frame = 0; frame < 30 * 90; ++frame) {
        rig.engine.Update(in);
        const auto fg = rig.engine.Status(LayerKind::Foreground);
        const auto bg = rig.engine.Status(LayerKind::Background);
        if (frame > 30 * 3) { // after the previous continuous effect has eased out
            CHECK(fg.state == FxState::TerminalRunning || fg.state == FxState::TerminalDraining);
            CHECK(bg.state == FxState::TerminalRunning || bg.state == FxState::TerminalDraining);
        }
        if (fg.effectElapsedSeconds < fgPrev) ++fgRuns;
        if (bg.effectElapsedSeconds < bgPrev) ++bgRuns;
        fgPrev = fg.effectElapsedSeconds;
        bgPrev = bg.effectElapsedSeconds;
    }
    CHECK(fgRuns >= 2); // it finished and started again (a terminal effect ends within terminalMaxSeconds)
    CHECK(bgRuns >= 2);
}

TEST_CASE(Directive_PinnedTerminalWithoutLoopFallsBackToRest) {
    Rig rig;
    rig.engine.SetDirective(LayerKind::Background, Pin(EffectId::BackgroundSuction, /*loopTerminal=*/false));
    rig.Run(60.0f); // > terminalMaxSeconds (20s) + draining
    CHECK(rig.engine.Status(LayerKind::Background).state == FxState::Rest);
}

TEST_CASE(Directive_SwitchingBackToAutoResumesRandomCycling) {
    Rig rig;
    rig.engine.SetDirective(LayerKind::Background, Pin(EffectId::Ripple));
    rig.Run(5.0f);
    rig.engine.SetDirective(LayerKind::Background, LayerDirective{});
    std::set<EffectId> seen;
    EffectEngine::Inputs in;
    in.dt = 1.0f / 30.0f;
    for (int frame = 0; frame < 30 * 200; ++frame) {
        rig.engine.Update(in);
        const auto status = rig.engine.Status(LayerKind::Background);
        if (status.hasEffect) seen.insert(status.effect);
    }
    CHECK(seen.size() >= 3); // Auto picks varied effects again
}

TEST_CASE(Directive_RandomSwitchingNeverBreaksAndAlwaysSettlesOnTheLastDirective) {
    // Spike S4 (docs/DESIGN_VIEWER.md §D.1): arbitrary interleavings of
    // directive changes and frames must neither crash nor wedge a layer.
    Mt19937RandomSource chooser(99);
    const EffectId bgChoices[] = {EffectId::Ripple, EffectId::Tilt, EffectId::LensDistort, EffectId::BackgroundSuction};
    const EffectId fgChoices[] = {EffectId::FlagWave, EffectId::Kaleidoscope, EffectId::VortexSuction,
                                  EffectId::GlassShatter, EffectId::NoiseDissolve};
    for (int trial = 0; trial < 12; ++trial) {
        Rig rig(1000 + trial);
        LayerDirective lastBg, lastFg;
        for (int step = 0; step < 40; ++step) {
            const auto pickDirective = [&](const EffectId* choices, size_t n) {
                LayerDirective d;
                const uint32_t r = chooser.NextUInt32() % 3;
                if (r == 0) return d; // Auto
                if (r == 1) return Rest();
                return Pin(choices[chooser.NextUInt32() % n], chooser.NextUInt32() % 2 == 0);
            };
            lastBg = pickDirective(bgChoices, 4);
            lastFg = pickDirective(fgChoices, 5);
            rig.engine.SetDirective(LayerKind::Background, lastBg);
            rig.engine.SetDirective(LayerKind::Foreground, lastFg);
            rig.Run(static_cast<float>(chooser.NextUInt32() % 100) / 50.0f); // 0-2s
        }
        rig.Run(4.0f);
        if (lastBg.mode == LayerMode::Rest) CHECK(rig.engine.Status(LayerKind::Background).state == FxState::Rest);
        if (lastFg.mode == LayerMode::Rest) CHECK(rig.engine.Status(LayerKind::Foreground).state == FxState::Rest);
        if (lastBg.mode == LayerMode::Pin && lastBg.pinned != EffectId::BackgroundSuction) {
            const auto s = rig.engine.Status(LayerKind::Background);
            CHECK(s.hasEffect && s.effect == lastBg.pinned);
        }
        if (lastFg.mode == LayerMode::Pin && lastFg.pinned == EffectId::FlagWave) {
            const auto s = rig.engine.Status(LayerKind::Foreground);
            CHECK(s.hasEffect && s.effect == lastFg.pinned);
        }
    }
}

TEST_CASE(Directive_StatusReportsCurrentEffectAndStateName) {
    Rig rig;
    rig.engine.SetDirective(LayerKind::Background, Pin(EffectId::Ripple));
    rig.Run(2.0f);
    const auto s = rig.engine.Status(LayerKind::Background);
    CHECK(s.hasEffect);
    CHECK(s.effect == EffectId::Ripple);
    CHECK(std::string(core::fx::FxStateToString(s.state)) == "running");
}

TEST_CASE(Directive_SwappingTheForegroundLayerKeepsTheDirectiveAndRestartsTheLayer) {
    Rig rig;
    rig.engine.SetDirective(LayerKind::Foreground, Pin(EffectId::FlagWave));
    rig.Run(3.0f);
    CHECK(rig.engine.Status(LayerKind::Foreground).effect == EffectId::FlagWave);

    // New content arrives (e.g. the viewer switched the content source).
    LayerSource fresh = MakeLayer(LayerKind::Foreground, 10);
    rig.engine.SetForegroundLayer(std::move(fresh));
    rig.Run(3.0f);
    const auto s = rig.engine.Status(LayerKind::Foreground);
    CHECK(s.hasEffect);
    CHECK(s.effect == EffectId::FlagWave); // the pin survived the swap
    CHECK(rig.engine.Status(LayerKind::Background).hasEffect); // background undisturbed

    // An empty foreground (source "none") keeps the layer quiet without breaking anything.
    LayerSource empty = MakeLayer(LayerKind::Foreground, 10);
    empty.empty = true;
    empty.emptyReason = core::fx::EmptyReason::PreviewMode;
    rig.engine.SetForegroundLayer(std::move(empty));
    rig.Run(2.0f);
    CHECK(rig.engine.Status(LayerKind::Foreground).state == FxState::Empty);
}
