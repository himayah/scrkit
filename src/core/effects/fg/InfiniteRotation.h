#pragma once
// Whole-layer rotation with 3x3 (or 5x5) tile replicas, foreground continuous
// (DESIGN_EFFECTS.md §6.1.4).

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

class InfiniteRotationEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::InfiniteRotation; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Tiles; }
    ExitStrategy Exit() const override { return ExitStrategy::Crossfade; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    InfiniteRotationParams params_;
    core::Mt19937RandomSource rng_{0};
    float omegaSign_ = 1.0f;
    float theta_ = 0.0f;
};

} // namespace core::fx
