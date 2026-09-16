#pragma once
// Traveling-wave zoom pulse, background continuous (DESIGN_EFFECTS.md
// §6.2.11).

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

Vec2 WaveZoomDisplace(Vec2 rest, float t, float intensity, const WaveZoomParams& params, bool horizontal,
                       float screenW, float screenH);

class WaveZoomEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::WaveZoom; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    WaveZoomParams params_;
    core::Mt19937RandomSource rng_{0};
    bool horizontal_ = true;
};

} // namespace core::fx
