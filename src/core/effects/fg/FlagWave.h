#pragma once
// Flag-waving mesh distortion, foreground continuous (DESIGN_EFFECTS.md §6.1.1).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

// Pure math, unit-testable without an IEffect instance.
Vec2 FlagWaveDisplace(Vec2 rest, float t, float intensity, const FlagWaveParams& params, float screenW,
                       float screenH);
float FlagWaveShade(Vec2 rest, float t, float intensity, const FlagWaveParams& params, float screenW);

class FlagWaveEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::FlagWave; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    FlagWaveParams params_;
};

} // namespace core::fx
