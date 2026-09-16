#include "test_framework.h"

#include "../src/core/effects/EffectCatalog.h"
#include "../src/core/effects/EffectScheduler.h"

using core::IRandomSource;
using core::fx::EffectId;
using core::fx::EffectKind;
using core::fx::EffectParams;
using core::fx::EngineConfig;
using core::fx::LayerKind;
using core::fx::MakeDefaultEngineConfig;
using core::fx::TimelineEntry;

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

TEST_CASE(Scheduler_DisabledEffectIsNeverSelected) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.foreground.perEffect[EffectId::FlagWave].enabled = false;
    FixedSequenceRandom rng(std::vector<float>(200, 0.0f)); // always picks the first weighted candidate
    std::deque<TimelineEntry> history;
    for (int i = 0; i < 50; ++i) {
        auto pick = core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine,
                                                     history, rng);
        CHECK(pick.has_value());
        CHECK(pick->id != EffectId::FlagWave);
    }
}

TEST_CASE(Scheduler_AvoidsLastThreeWhenEnoughCandidates) {
    EngineConfig engine = MakeDefaultEngineConfig(); // 8 fg continuous candidates, >=4
    FixedSequenceRandom rng(std::vector<float>(500, 0.0f));
    std::deque<TimelineEntry> history;
    std::vector<EffectId> picked;
    for (int i = 0; i < 20; ++i) {
        auto pick = core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine,
                                                     history, rng);
        CHECK(pick.has_value());
        if (picked.size() >= 3) {
            CHECK(pick->id != picked[picked.size() - 1]);
            CHECK(pick->id != picked[picked.size() - 2]);
            CHECK(pick->id != picked[picked.size() - 3]);
        }
        picked.push_back(pick->id);
        history.push_back(TimelineEntry{LayerKind::Foreground, pick->id, 0.0f, pick->durationSeconds, 0});
    }
}

TEST_CASE(Scheduler_SingleCandidateAllowsConsecutiveRepeat) {
    EngineConfig engine = MakeDefaultEngineConfig();
    // Disable every fg continuous effect except one.
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) {
        engine.foreground.perEffect[id].enabled = (id == EffectId::FlagWave);
    }
    FixedSequenceRandom rng(std::vector<float>(50, 0.0f));
    std::deque<TimelineEntry> history;
    for (int i = 0; i < 5; ++i) {
        auto pick = core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine,
                                                     history, rng);
        CHECK(pick.has_value());
        CHECK(pick->id == EffectId::FlagWave);
        history.push_back(TimelineEntry{LayerKind::Foreground, pick->id, 0.0f, pick->durationSeconds, 0});
    }
}

TEST_CASE(Scheduler_ZeroWeightEffectivelyExcludesFromWeightedPick) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) {
        engine.foreground.perEffect[id].weight = (id == EffectId::NorenSwing) ? 1.0f : 0.0f;
    }
    FixedSequenceRandom rng({0.5f});
    std::deque<TimelineEntry> history;
    auto pick =
        core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
    CHECK(pick.has_value());
    CHECK(pick->id == EffectId::NorenSwing);
}

TEST_CASE(Scheduler_DurationStaysWithinConfiguredRange) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.foreground.perEffect[EffectId::FlagWave].minSeconds = 3.0f;
    engine.foreground.perEffect[EffectId::FlagWave].maxSeconds = 4.0f;
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) {
        engine.foreground.perEffect[id].enabled = (id == EffectId::FlagWave);
    }
    FixedSequenceRandom rng({0.0f, 0.25f, 0.0f, 0.75f, 0.0f, 1.0f});
    std::deque<TimelineEntry> history;
    for (int i = 0; i < 3; ++i) {
        auto pick =
            core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
        CHECK(pick.has_value());
        CHECK(pick->durationSeconds >= 3.0f);
        CHECK(pick->durationSeconds <= 4.0f);
    }
}

TEST_CASE(Scheduler_EitherBoundZeroFallsBackToLayerDefault) {
    EngineConfig engine = MakeDefaultEngineConfig(); // layer default 5..10
    engine.foreground.perEffect[EffectId::FlagWave].minSeconds = 0.0f;
    engine.foreground.perEffect[EffectId::FlagWave].maxSeconds = 3.0f; // ignored: min is 0
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) {
        engine.foreground.perEffect[id].enabled = (id == EffectId::FlagWave);
    }
    FixedSequenceRandom rng({0.0f, 1.0f});
    std::deque<TimelineEntry> history;
    auto pick =
        core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
    CHECK(pick.has_value());
    CHECK(pick->durationSeconds >= 5.0f);
    CHECK(pick->durationSeconds <= 10.0f);
}

TEST_CASE(Scheduler_MinGreaterThanMaxFallsBackToLayerDefaultEvenWhenBothNonzero) {
    EngineConfig engine = MakeDefaultEngineConfig(); // layer default 5..10
    engine.foreground.perEffect[EffectId::FlagWave].minSeconds = 9.0f;
    engine.foreground.perEffect[EffectId::FlagWave].maxSeconds = 2.0f; // Min > Max, both nonzero
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) {
        engine.foreground.perEffect[id].enabled = (id == EffectId::FlagWave);
    }
    FixedSequenceRandom rng({0.0f, 1.0f});
    std::deque<TimelineEntry> history;
    auto pick =
        core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
    CHECK(pick.has_value());
    CHECK(pick->durationSeconds >= 5.0f);
    CHECK(pick->durationSeconds <= 10.0f);
}

TEST_CASE(Scheduler_ScriptedSequenceCyclesInOrder) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.scriptedForeground = {EffectId::FlagWave, EffectId::NorenSwing};
    FixedSequenceRandom rng({0.0f});
    std::deque<TimelineEntry> history;

    auto first =
        core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
    CHECK(first.has_value());
    CHECK(first->id == EffectId::FlagWave);
    history.push_back(TimelineEntry{LayerKind::Foreground, first->id, 0.0f, first->durationSeconds, 0});

    auto second =
        core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
    CHECK(second.has_value());
    CHECK(second->id == EffectId::NorenSwing);
    history.push_back(TimelineEntry{LayerKind::Foreground, second->id, 0.0f, second->durationSeconds, 0});

    auto third =
        core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
    CHECK(third.has_value());
    CHECK(third->id == EffectId::FlagWave); // cycles back to the start
}

TEST_CASE(Scheduler_ScriptedTerminalOnlyEntryIsSkippedForContinuousPick) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.scriptedForeground = {EffectId::GlassShatter}; // terminal-only script (§12.5 manual checklist)
    FixedSequenceRandom rng({0.0f});
    std::deque<TimelineEntry> history;

    auto continuousPick =
        core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng);
    CHECK(!continuousPick.has_value());

    auto terminalPick =
        core::fx::EffectScheduler::Pick(EffectKind::Terminal, LayerKind::Foreground, engine, history, rng);
    CHECK(terminalPick.has_value());
    CHECK(terminalPick->id == EffectId::GlassShatter);
}

TEST_CASE(Scheduler_HueShiftExcludedWhileRingNotReady) {
    EngineConfig engine = MakeDefaultEngineConfig();
    for (EffectId id : core::fx::BackgroundContinuousCatalog()) {
        engine.background.perEffect[id].enabled = (id == EffectId::HueShift);
    }
    FixedSequenceRandom rng({0.0f});
    std::deque<TimelineEntry> history;
    auto notReady = core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Background, engine,
                                                     history, rng, /*hueShiftReady=*/false);
    CHECK(!notReady.has_value());

    auto ready = core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Background, engine, history,
                                                  rng, /*hueShiftReady=*/true);
    CHECK(ready.has_value());
    CHECK(ready->id == EffectId::HueShift);
}

TEST_CASE(Scheduler_EngineDisabledAlwaysReturnsNulloptEvenWithScript) {
    EngineConfig engine = MakeDefaultEngineConfig();
    engine.enabled = false;
    engine.scriptedForeground = {EffectId::FlagWave};
    FixedSequenceRandom rng({0.0f});
    std::deque<TimelineEntry> history;
    CHECK(!core::fx::EffectScheduler::Pick(EffectKind::Continuous, LayerKind::Foreground, engine, history, rng)
               .has_value());
    CHECK(!core::fx::EffectScheduler::Pick(EffectKind::Terminal, LayerKind::Foreground, engine, history, rng)
               .has_value());
}
