#pragma once
// Bursty horizontal-band glitch with RGB channel split, background
// continuous (DESIGN_EFFECTS.md §6.2.9).

#include <vector>

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

struct Band {
    float y0 = 0.0f, y1 = 0.0f, dx = 0.0f;
};

// Pure: bands cover [0,H] with no gap/overlap. `rng` draws the random band
// heights/offsets (§4.5-style caller-owned RNG, not the effect's own --
// exposed this way so the partition logic itself is deterministically
// testable).
std::vector<Band> MakeGlitchBands(core::IRandomSource& rng, float screenH, const GlitchShiftParams& params,
                                   float intensity, float screenW);

class GlitchShiftEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::GlitchShift; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Bands; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    GlitchShiftParams params_;
    core::Mt19937RandomSource rng_{0};

    bool inBurst_ = false;
    float nextBurstTime_ = 0.0f;
    float burstEndTime_ = 0.0f;
    float lastRebandTime_ = -1.0f;
    std::vector<Band> bands_;
};

} // namespace core::fx
