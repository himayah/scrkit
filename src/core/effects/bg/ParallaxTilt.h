#pragma once
// Slow low-frequency drift + tiny rotation, background continuous
// (DESIGN_EFFECTS.md §6.2.10).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

Transform2D ParallaxTiltTransform(float t, float intensity, const ParallaxTiltParams& params, uint32_t seed,
                                   float screenW, float screenH);

class ParallaxTiltEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::ParallaxTilt; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Transform; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    ParallaxTiltParams params_;
    uint32_t seed_ = 0;
    float screenW_ = 1920.0f, screenH_ = 1080.0f;
};

} // namespace core::fx
