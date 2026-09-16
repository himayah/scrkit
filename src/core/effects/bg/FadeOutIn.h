#pragma once
// Fade to near-black and back, background continuous (DESIGN_EFFECTS.md
// §6.2.2).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

float FadeOutInAlpha(float t, float durationSeconds, float intensity, const FadeOutInParams& params);

class FadeOutInEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::FadeOutIn; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Transform; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    FadeOutInParams params_;
    float durationSeconds_ = 1.0f;
};

} // namespace core::fx
