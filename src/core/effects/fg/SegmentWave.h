#pragma once
// Row-banded horizontal wave, foreground continuous (DESIGN_EFFECTS.md
// §6.1.14).

#include "../EffectParams.h"
#include "../FragmentSystem.h"
#include "../IEffect.h"

namespace core::fx {

class SegmentWaveEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::SegmentWave; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Fragments; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    SegmentWaveParams params_;
    FragmentSystem system_;
};

} // namespace core::fx
