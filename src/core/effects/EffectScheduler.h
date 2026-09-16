#pragma once
// Weighted random effect selection (DESIGN_EFFECTS.md §5.5).

#include <optional>

#include "../SuctionCenterWalker.h" // core::IRandomSource
#include "EffectParams.h"
#include "Timeline.h"

namespace core::fx {

struct EffectPick {
    EffectId id = EffectId::FlagWave;
    float durationSeconds = 0.0f;
    uint32_t seed = 0;
};

namespace EffectScheduler {

// Picks the next `kind` effect for `layer`, or nullopt if there is no valid
// candidate (engine disabled, every candidate individually disabled, or --
// for HueShift specifically -- its hue-ring textures aren't ready yet).
// `hueShiftReady` is irrelevant outside LayerKind::Background /
// EffectKind::Continuous but accepted unconditionally to keep the signature
// uniform for both call sites.
std::optional<EffectPick> Pick(EffectKind kind, LayerKind layer, const EngineConfig& engine,
                                const std::deque<TimelineEntry>& history, core::IRandomSource& rng,
                                bool hueShiftReady = true);

} // namespace EffectScheduler

} // namespace core::fx
