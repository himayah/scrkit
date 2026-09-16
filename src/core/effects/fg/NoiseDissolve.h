#pragma once
// Noise-threshold dissolve, foreground terminal (DESIGN_EFFECTS.md §6.1.12).

#include <vector>

#include "../EffectParams.h"
#include "../FragmentSystem.h"
#include "../IEffect.h"

namespace core::fx {

// Pure: alpha = 1 for noise01 > tau, 0 for noise01 < tau - edge, smoothstep
// in between.
float DissolveAlpha(float noise01, float tau, float edge);

class NoiseDissolveEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::NoiseDissolve; }
    EffectKind Kind() const override { return EffectKind::Terminal; }
    GeometryKind Geometry() const override { return GeometryKind::Fragments; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; } // unused: Terminal effects don't exit

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return system_.AllDead(); }

private:
    const LayerSource* layer_ = nullptr;
    NoiseDissolveParams params_;
    FragmentSystem system_;
    std::vector<float> noiseValues_; // one per fragment, drawn once in Begin()
};

} // namespace core::fx
