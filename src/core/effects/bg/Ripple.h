#pragma once
// Spawning concentric ripples, background continuous (DESIGN_EFFECTS.md
// §6.2.1).

#include <vector>

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

struct RippleDrop {
    Vec2 origin{0.0f, 0.0f};
    float spawnTime = 0.0f;
};

// Pure: sum of every drop's contribution at `rest`, at time t.
Vec2 RippleDisplace(Vec2 rest, float t, const std::vector<RippleDrop>& drops, float intensity,
                     const RippleParams& params);

class RippleEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::Ripple; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    RippleParams params_;
    core::Mt19937RandomSource rng_{0};
    std::vector<RippleDrop> drops_;
    float nextSpawnTime_ = 0.0f;
};

} // namespace core::fx
