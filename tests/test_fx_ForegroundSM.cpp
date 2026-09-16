#include "test_framework.h"

#include <algorithm>

#include "../src/core/effects/EffectCatalog.h"
#include "../src/core/effects/EffectStateMachine.h"

using core::IRandomSource;
using core::fx::EffectId;
using core::fx::EngineConfig;
using core::fx::ExitStrategy;
using core::fx::ForegroundEffectStateMachine;
using core::fx::FxInputs;
using core::fx::FxState;
using core::fx::EmptyReason;
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

TEST_CASE(ForegroundSM_ShowcaseElapsedForcesExitThenTerminal) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.foregroundShowcaseSeconds = 0.3f;
    engine.transitionSeconds = 0.1f;
    engine.foreground.defaultMinSeconds = 100.0f; // effect duration far longer than the showcase window
    engine.foreground.defaultMaxSeconds = 100.0f;
    FixedSequenceRandom rng(std::vector<float>(500, 0.0f));
    Timeline timeline;
    ForegroundEffectStateMachine sm(engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);

    FxInputs in;
    in.dt = 1.0f / 60.0f;
    in.currentExitStrategy = ExitStrategy::Envelope;
    bool reachedTerminal = false;
    for (int i = 0; i < 600 && !reachedTerminal; ++i) {
        sm.Step(in);
        if (sm.Current() == FxState::TerminalRunning) reachedTerminal = true;
    }
    CHECK(reachedTerminal);
    CHECK(timeline.Current() != nullptr);
    CHECK(timeline.Current()->effectId == EffectId::VortexSuction ||
          std::find(core::fx::ForegroundTerminalCatalog().begin(), core::fx::ForegroundTerminalCatalog().end(),
                    timeline.Current()->effectId) != core::fx::ForegroundTerminalCatalog().end());
}

TEST_CASE(ForegroundSM_NoContinuousCandidateEntersTerminalImmediately) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) engine.foreground.perEffect[id].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    ForegroundEffectStateMachine sm(engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::TerminalRunning); // ImmediateTerminal policy, no showcase wait
}

TEST_CASE(ForegroundSM_NoTerminalCandidateForcesVortexSuction) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) engine.foreground.perEffect[id].enabled = false;
    for (EffectId id : core::fx::ForegroundTerminalCatalog()) engine.foreground.perEffect[id].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    Timeline timeline;
    ForegroundEffectStateMachine sm(engine, rng, timeline);
    sm.Reset(EmptyReason::NotEmpty);
    CHECK(sm.Current() == FxState::TerminalRunning);
    CHECK(timeline.Current()->effectId == EffectId::VortexSuction);
}
