#pragma once
// Voronoi-shard glass shatter, foreground terminal (DESIGN_EFFECTS.md §6.1.9).

#include <vector>

#include "../EffectParams.h"
#include "../FragmentSystem.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

// Nearest-seed assignment: result[i] is the index into `seeds` closest to
// cellPos[i]. Pure, so the Voronoi grouping itself is unit-testable.
std::vector<int> AssignShards(const std::vector<Vec2>& cellPos, const std::vector<Vec2>& seeds);

class GlassShatterEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::GlassShatter; }
    EffectKind Kind() const override { return EffectKind::Terminal; }
    GeometryKind Geometry() const override { return GeometryKind::Fragments; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; } // unused: Terminal effects don't exit

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return system_.AllDead(); }

private:
    const LayerSource* layer_ = nullptr;
    GlassShatterParams params_;
    core::Mt19937RandomSource rng_{0};
    FragmentSystem system_;
    // Per-group rest centroid (§6.1.9's restCentroid_g) -- not part of the
    // generic FragmentGroup struct since no other group-based effect needs
    // it (SegmentWave repurposes Fragment::group as a plain label, not a
    // FragmentSystem::groups entry at all).
    std::vector<Vec2> groupRestCentroids_;
};

} // namespace core::fx
