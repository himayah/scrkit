#pragma once
// Barrel/pincushion lens distortion, background continuous
// (DESIGN_EFFECTS.md §6.2.5).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

// Returns the new absolute position (not a delta -- matches §6.2.5's
// p' = c + (p-c)*f(r^2) formula directly).
Vec2 LensDisplace(Vec2 rest, float k, float screenW, float screenH);

class LensDistortEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::LensDistort; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    LensDistortParams params_;
};

} // namespace core::fx
