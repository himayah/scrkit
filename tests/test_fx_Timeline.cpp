#include "test_framework.h"

#include "../src/core/effects/Timeline.h"

using core::fx::EffectId;
using core::fx::LayerKind;
using core::fx::Timeline;
using core::fx::TimelineEntry;

TEST_CASE(Timeline_AppendAndCurrent) {
    Timeline t;
    CHECK(t.Current() == nullptr);
    t.Append(TimelineEntry{LayerKind::Foreground, EffectId::FlagWave, 0.0f, 7.0f, 1});
    CHECK(t.Current() != nullptr);
    CHECK(t.Current()->effectId == EffectId::FlagWave);
    t.Append(TimelineEntry{LayerKind::Foreground, EffectId::NorenSwing, 7.0f, 5.0f, 2});
    CHECK(t.Current()->effectId == EffectId::NorenSwing);
}

TEST_CASE(Timeline_HistoryCapsAtEight) {
    Timeline t;
    for (int i = 0; i < 12; ++i) {
        t.Append(TimelineEntry{LayerKind::Foreground, EffectId::FlagWave, static_cast<float>(i), 1.0f,
                                static_cast<uint32_t>(i)});
    }
    CHECK_EQ(t.History().size(), static_cast<size_t>(8));
    CHECK_EQ(t.History().front().seed, static_cast<uint32_t>(4)); // oldest 4 evicted
    CHECK_EQ(t.History().back().seed, static_cast<uint32_t>(11));
}

TEST_CASE(Timeline_ToStringFormat) {
    Timeline t;
    t.Append(TimelineEntry{LayerKind::Foreground, EffectId::FlagWave, 0.0f, 7.3f, 1});
    t.Append(TimelineEntry{LayerKind::Foreground, EffectId::InfiniteScroll, 7.3f, 5.1f, 2});
    const std::string s = t.ToString();
    CHECK(s.find("FlagWave") != std::string::npos);
    CHECK(s.find("InfiniteScroll") != std::string::npos);
    CHECK(s.find("->") != std::string::npos);
}
