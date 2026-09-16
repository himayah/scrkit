#pragma once
// Noise-modulated concentric ripple, background continuous
// (DESIGN_EFFECTS.md §6.2.7).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

Vec2 NoiseRippleDisplace(Vec2 rest, float t, float intensity, const NoiseRippleParams& params, uint32_t seed,
                          float screenW, float screenH);

class NoiseRippleEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::NoiseRipple; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    NoiseRippleParams params_;
    uint32_t seed_ = 0;
};

} // namespace core::fx
