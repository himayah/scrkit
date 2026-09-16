#pragma once
// Kaleidoscope radial fold, foreground continuous (DESIGN_EFFECTS.md §6.1.13).

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../Mesh.h"
#include "../../RandomSource.h"

namespace core::fx {

class KaleidoscopeEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::Kaleidoscope; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::RadialMesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Crossfade; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    KaleidoscopeParams params_;
    core::Mt19937RandomSource rng_{0};
    Mesh radialMesh_; // own copy (§4.1: LayerSource has no mutable radial-mesh slot)
    int segments_ = 6;
    float wedgeOriginRad_ = 0.0f;
};

} // namespace core::fx
