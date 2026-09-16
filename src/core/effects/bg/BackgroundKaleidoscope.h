#pragma once
// Kaleidoscope radial fold on the background texture, background continuous
// (DESIGN_EFFECTS.md §6.2.6 -- same implementation as fg Kaleidoscope
// §6.1.13, different segment choices/rates and background texture).

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../Mesh.h"
#include "../../RandomSource.h"

namespace core::fx {

class BackgroundKaleidoscopeEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::BackgroundKaleidoscope; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::RadialMesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Crossfade; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    BackgroundKaleidoscopeParams params_;
    core::Mt19937RandomSource rng_{0};
    Mesh radialMesh_;
    int segments_ = 6;
    float wedgeOriginRad_ = 0.0f;
};

} // namespace core::fx
