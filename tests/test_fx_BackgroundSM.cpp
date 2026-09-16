#include "test_framework.h"

#include "../src/core/effects/EffectCatalog.h"
#include "../src/core/effects/EffectStateMachine.h"

using core::IRandomSource;
using core::fx::BackgroundEffectStateMachine;
using core::fx::EffectId;
using core::fx::EmptyReason;
using core::fx::EngineConfig;
using core::fx::ExitStrategy;
using core::fx::FxInputs;
using core::fx::FxState;
using core::fx::MakeDefaultEngineConfig;
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
} // namespace

TEST_CASE(BackgroundSM_NoContinuousCandidateStaysIdleUntilRequestTerminal) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::BackgroundContinuousCatalog()) engine.background.perEffect[id].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    BackgroundEffectStateMachine sm(engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::Idle); // WaitForTerminalRequest: stays put, unlike Foreground

    FxInputs in;
    in.dt = 1.0f / 60.0f;
    for (int i = 0; i < 6000; ++i) sm.Step(in); // even 100 simulated seconds change nothing on its own
    CHECK(sm.Current() == FxState::Idle);

    sm.RequestTerminal();
    CHECK(sm.Current() == FxState::TerminalRunning);
    CHECK(timeline.Current()->effectId == EffectId::BackgroundSuction);
}

TEST_CASE(BackgroundSM_ShowcaseSecondsNeverAutoTriggersTerminal) {
    // Background has no showcase concept -- even with foregroundShowcaseSeconds
    // left at its tiny default, a running Background continuous effect must
    // not be auto-terminated the way Foreground's is.
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.foregroundShowcaseSeconds = 0.01f;
    engine.background.defaultMinSeconds = 5.0f;
    engine.background.defaultMaxSeconds = 5.0f;
    FixedSequenceRandom rng(std::vector<float>(500, 0.0f));
    Timeline timeline;
    BackgroundEffectStateMachine sm(engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::Entering);

    FxInputs in;
    in.dt = 1.0f / 60.0f;
    in.currentExitStrategy = ExitStrategy::Envelope;
    for (int i = 0; i < 30; ++i) sm.Step(in); // 0.5s, well past the tiny showcase window
    CHECK(sm.Current() != FxState::TerminalRunning);
}

TEST_CASE(BackgroundSM_RequestTerminalDuringRunningIsImmediate) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.transitionSeconds = 0.05f;
    engine.background.defaultMinSeconds = 100.0f;
    engine.background.defaultMaxSeconds = 100.0f;
    FixedSequenceRandom rng(std::vector<float>(500, 0.0f));
    Timeline timeline;
    BackgroundEffectStateMachine sm(engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);

    FxInputs in;
    in.dt = 1.0f / 60.0f;
    for (int i = 0; i < 5; ++i) sm.Step(in); // past transitionSeconds -> Running
    CHECK(sm.Current() == FxState::Running);

    sm.RequestTerminal();
    CHECK(sm.Current() == FxState::Exiting);
}

TEST_CASE(BackgroundSM_NoTerminalCandidateForcesBackgroundSuction) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::BackgroundContinuousCatalog()) engine.background.perEffect[id].enabled = false;
    // BackgroundTerminalCatalog only has BackgroundSuction; disable it too.
    engine.background.perEffect[EffectId::BackgroundSuction].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    BackgroundEffectStateMachine sm(engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::Idle);
    sm.RequestTerminal();
    CHECK(sm.Current() == FxState::TerminalRunning);
    CHECK(timeline.Current()->effectId == EffectId::BackgroundSuction); // forced default despite being disabled
}
