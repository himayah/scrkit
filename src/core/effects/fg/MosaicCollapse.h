#pragma once
// Bottom-up tile collapse, foreground terminal (DESIGN_EFFECTS.md §6.1.11).

#include "../EffectParams.h"
#include "../FragmentSystem.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

class MosaicCollapseEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::MosaicCollapse; }
    EffectKind Kind() const override { return EffectKind::Terminal; }
    GeometryKind Geometry() const override { return GeometryKind::Fragments; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; } // unused: Terminal effects don't exit

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return system_.AllDead(); }

private:
    const LayerSource* layer_ = nullptr;
    MosaicCollapseParams params_;
    core::Mt19937RandomSource rng_{0};
    FragmentSystem system_;
};

} // namespace core::fx
