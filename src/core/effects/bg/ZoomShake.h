#pragma once
// Zoom pulse + jitter shake, background continuous (DESIGN_EFFECTS.md
// §6.2.3).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

Transform2D ZoomShakeTransform(float t, float intensity, const ZoomShakeParams& params, uint32_t seed,
                                float screenW, float screenH);

class ZoomShakeEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::ZoomShake; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Transform; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    ZoomShakeParams params_;
    uint32_t seed_ = 0;
    float screenW_ = 1920.0f, screenH_ = 1080.0f;
};

} // namespace core::fx
