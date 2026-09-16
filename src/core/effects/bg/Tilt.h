#pragma once
// Slow rocking tilt, background continuous (DESIGN_EFFECTS.md §6.2.4, with
// the D-19 scale fix from the external review).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

Transform2D TiltTransform(float t, float intensity, const TiltParams& params, float screenW, float screenH);

class TiltEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::Tilt; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Transform; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    TiltParams params_;
    float screenW_ = 1920.0f, screenH_ = 1080.0f;
};

} // namespace core::fx
