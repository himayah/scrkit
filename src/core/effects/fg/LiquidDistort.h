#pragma once
// Liquid-like noise distortion (position + UV refraction), foreground
// continuous (DESIGN_EFFECTS.md §6.1.8).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

MeshVertex LiquidDistortVertex(const MeshVertex& rest, float t, float intensity,
                                const LiquidDistortParams& params, uint32_t seed, float screenW, float screenH);

class LiquidDistortEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::LiquidDistort; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    LiquidDistortParams params_;
    uint32_t seed_ = 0;
};

} // namespace core::fx
