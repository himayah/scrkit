#pragma once
// Records the sequence of effects a layer has played (DESIGN_EFFECTS.md
// §4.8). Used by EffectScheduler to avoid immediate repeats and by logging
// (§11) / test reproduction (§12.4).

#include <cstdint>
#include <deque>
#include <string>

#include "EffectTypes.h"

namespace core::fx {

struct TimelineEntry {
    LayerKind layer = LayerKind::Foreground;
    EffectId effectId = EffectId::FlagWave;
    float startSeconds = 0.0f;
    float durationSeconds = 0.0f;
    uint32_t seed = 0;
};

class Timeline {
public:
    // Keeps only the most recent kHistoryLimit entries (§5.5's "直近8件").
    static constexpr size_t kHistoryLimit = 8;

    void Append(TimelineEntry e) {
        history_.push_back(e);
        while (history_.size() > kHistoryLimit) history_.pop_front();
    }

    const TimelineEntry* Current() const { return history_.empty() ? nullptr : &history_.back(); }

    const std::deque<TimelineEntry>& History() const { return history_; }

    std::string ToString() const {
        std::string out;
        for (size_t i = 0; i < history_.size(); ++i) {
            if (i > 0) out += " -> ";
            const TimelineEntry& e = history_[i];
            out += EffectIdToString(e.effectId);
            out += "@" + std::to_string(e.startSeconds) + "s(" + std::to_string(e.durationSeconds) + "s)";
        }
        return out;
    }

private:
    std::deque<TimelineEntry> history_;
};

} // namespace core::fx
