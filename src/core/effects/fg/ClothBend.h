#pragma once
// Cloth-like sagging/breathing mesh distortion, foreground continuous
// (DESIGN_EFFECTS.md §6.1.7).

#include "../EffectParams.h"
#include "../IEffect.h"

namespace core::fx {

Vec2 ClothBendDisplace(Vec2 rest, float t, float intensity, const ClothBendParams& params, float screenW,
                        float screenH);
float ClothBendShade(Vec2 rest, float t, float intensity, const ClothBendParams& params, float screenW,
                      float screenH);

class ClothBendEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::ClothBend; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    ClothBendParams params_;
};

} // namespace core::fx
