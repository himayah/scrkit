#pragma once
// Row-staggered falling confetti, foreground terminal (DESIGN_EFFECTS.md
// §6.1.10).

#include <vector>

#include "../EffectParams.h"
#include "../FragmentSystem.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

// Per-fragment randomized motion parameters, drawn once in Begin() (D-16's
// row-normalized delay lives on Fragment::delay itself, not here).
struct ConfettiIndividual {
    float fallSpeed = 0.0f;
    float swayAmp = 0.0f;
    float swayOmega = 0.0f;
    float swayPhase = 0.0f;
    float tumbleOmega = 0.0f;
};

// Pure, closed-form position at tau seconds after this fragment's delay.
Vec2 ConfettiPosition(Vec2 rest, float tau, const ConfettiIndividual& individual);

class ConfettiFallEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::ConfettiFall; }
    EffectKind Kind() const override { return EffectKind::Terminal; }
    GeometryKind Geometry() const override { return GeometryKind::Fragments; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; } // unused: Terminal effects don't exit

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return system_.AllDead(); }

private:
    const LayerSource* layer_ = nullptr;
    ConfettiFallParams params_;
    core::Mt19937RandomSource rng_{0};
    FragmentSystem system_;
    std::vector<ConfettiIndividual> individuals_;
};

} // namespace core::fx
