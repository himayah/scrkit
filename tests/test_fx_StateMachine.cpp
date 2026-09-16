#include "test_framework.h"

#include "../src/core/effects/EffectCatalog.h"
#include "../src/core/effects/EffectStateMachine.h"

using core::IRandomSource;
using core::fx::EffectId;
using core::fx::EffectKind;
using core::fx::EffectStateMachine;
using core::fx::EmptyReason;
using core::fx::EngineConfig;
using core::fx::ExitStrategy;
using core::fx::FxInputs;
using core::fx::FxState;
using core::fx::LayerKind;
using core::fx::MakeDefaultEngineConfig;
using core::fx::NoCandidatePolicy;
using core::fx::Timeline;

namespace {
class FixedSequenceRandom : public IRandomSource {
public:
    explicit FixedSequenceRandom(std::vector<float> values) : values_(std::move(values)) {}
    float NextFloat01() override {
        float v = values_[index_ % values_.size()];
        ++index_;
        return v;
    }

private:
    std::vector<float> values_;
    size_t index_ = 0;
};

FxInputs MakeInputs(float dt, ExitStrategy strategy = ExitStrategy::Envelope) {
    FxInputs in;
    in.dt = dt;
    in.currentExitStrategy = strategy;
    return in;
}
} // namespace

TEST_CASE(StateMachine_ResetWithCandidateEntersEntering) {
    EngineConfig engine = MakeDefaultEngineConfig();
    FixedSequenceRandom rng(std::vector<float>(200, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::Entering);
    CHECK_NEAR(sm.Envelope(), 0.0f, 1e-6f);
}

TEST_CASE(StateMachine_EnteringBecomesRunningAfterTransitionSeconds) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.transitionSeconds = 0.5f;
    FixedSequenceRandom rng(std::vector<float>(200, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    for (int i = 0; i < 30; ++i) sm.Step(MakeInputs(1.0f / 60.0f)); // 0.5s
    CHECK(sm.Current() == FxState::Running);
    CHECK_NEAR(sm.Envelope(), 1.0f, 1e-3f);
}

TEST_CASE(StateMachine_RunningExitsAfterDurationThenReenters) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.transitionSeconds = 0.1f;
    engine.foreground.defaultMinSeconds = 1.0f;
    engine.foreground.defaultMaxSeconds = 1.0f;
    FixedSequenceRandom rng(std::vector<float>(500, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);

    const float dt = 1.0f / 60.0f;
    bool sawExiting = false, reenteredEntering = false;
    for (int i = 0; i < 600 && !reenteredEntering; ++i) {
        auto out = sm.Step(MakeInputs(dt));
        if (sm.Current() == FxState::Exiting) sawExiting = true;
        if (sawExiting && out.switched && sm.Current() == FxState::Entering) reenteredEntering = true;
    }
    CHECK(sawExiting);
    CHECK(reenteredEntering);
}

TEST_CASE(StateMachine_TerminalRunningConsumedWhenCurrentFinished) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) engine.foreground.perEffect[id].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::TerminalRunning);

    FxInputs in = MakeInputs(1.0f / 60.0f);
    in.currentFinished = false;
    auto out = sm.Step(in);
    CHECK(sm.Current() == FxState::TerminalRunning);
    CHECK(!out.consumed);

    in.currentFinished = true;
    out = sm.Step(in);
    CHECK(sm.Current() == FxState::Consumed);
    CHECK(out.consumed);
}

TEST_CASE(StateMachine_TerminalMaxSecondsForcesDrainingThenConsumed) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.terminalMaxSeconds = 1.0f;
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) engine.foreground.perEffect[id].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);

    FxInputs in = MakeInputs(1.0f / 60.0f);
    for (int i = 0; i < 61; ++i) sm.Step(in); // >1.0s without ever finishing
    CHECK(sm.Current() == FxState::TerminalDraining);

    for (int i = 0; i < 31; ++i) sm.Step(in); // >0.5s
    CHECK(sm.Current() == FxState::Consumed);
}

TEST_CASE(StateMachine_EmptyPreviewModeWaitsShowcaseThenConsumed) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.foregroundShowcaseSeconds = 0.2f;
    FixedSequenceRandom rng(std::vector<float>(10, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::PreviewMode);
    CHECK(sm.Current() == FxState::Empty);

    FxInputs in = MakeInputs(1.0f / 60.0f);
    auto out = sm.Step(in);
    CHECK(sm.Current() == FxState::Empty);
    CHECK(!out.consumed);
    for (int i = 0; i < 20; ++i) out = sm.Step(in);
    CHECK(sm.Current() == FxState::Consumed);
}

TEST_CASE(StateMachine_EmptyCaptureFailedConsumesImmediately) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.foregroundShowcaseSeconds = 40.0f;
    FixedSequenceRandom rng(std::vector<float>(10, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::CaptureFailed);
    CHECK(sm.Current() == FxState::Empty);
    auto out = sm.Step(MakeInputs(1.0f / 60.0f));
    CHECK(sm.Current() == FxState::Consumed);
    CHECK(out.consumed);
}

TEST_CASE(StateMachine_RequestTerminalFromEnteringOrRunningIsImmediate) {
    EngineConfig engine = MakeDefaultEngineConfig();
    FixedSequenceRandom rng(std::vector<float>(500, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::Entering);
    sm.RequestTerminal();
    CHECK(sm.Current() == FxState::Exiting); // synchronous, no Step() call needed
}

TEST_CASE(StateMachine_RequestTerminalFromIdleIsImmediateForWaitPolicy) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::BackgroundContinuousCatalog()) engine.background.perEffect[id].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Background, NoCandidatePolicy::WaitForTerminalRequest, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::Idle);
    sm.RequestTerminal();
    CHECK(sm.Current() == FxState::TerminalRunning);
}

TEST_CASE(StateMachine_ForceIdleAndForceRestWorkFromAnyState) {
    EngineConfig engine = MakeDefaultEngineConfig();
    FixedSequenceRandom rng(std::vector<float>(200, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::Entering);

    sm.ForceIdle();
    CHECK(sm.Current() == FxState::Idle);

    sm.ForceRest();
    CHECK(sm.Current() == FxState::Rest);
}

TEST_CASE(StateMachine_TerminalUnavailableForcesLayerDefault) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.enabled = false; // forces nullopt from Pick regardless of kind
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    EffectStateMachine sm(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::TerminalRunning);
    CHECK(timeline.Current() != nullptr);
    CHECK(timeline.Current()->effectId == EffectId::VortexSuction);
}
