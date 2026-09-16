#pragma once
// Shared implementation for VortexSuction (§6.1.5) and BackgroundSuction
// (§6.2.12): both are "reuse the existing spiral suction math on a
// FragmentSystem" and differ only in their suction-speed/centerAccelFactor
// constants. Not split into fg/VortexSuction.cpp + bg/BackgroundSuction.cpp
// like the other 23 effects since there is no per-effect logic left to
// separate once those constants are parameterized (EffectRegistry.cpp
// constructs this directly for both ids).

#include "../RandomSource.h"
#include "FragmentSystem.h"
#include "IEffect.h"

namespace core::fx {

class SuctionEffect : public IEffect {
public:
    explicit SuctionEffect(EffectId id) : id_(id) {}

    EffectId Id() const override { return id_; }
    EffectKind Kind() const override { return EffectKind::Terminal; }
    GeometryKind Geometry() const override { return GeometryKind::Fragments; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; } // unused: Terminal effects don't exit

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return system_.AllDead(); }

private:
    // The design's §6.1.5/§6.2.12 pseudocode initializes each fragment's
    // core::SpiralState relative to "the suction center" inside Begin(), but
    // EffectContext (§4.5) doesn't carry the live suction center -- only
    // EffectFrame does, once per Step(). So spiral state is instead lazily
    // initialized on the first Step() call, using that frame's
    // suctionCenter, rather than in Begin() itself.
    void InitSpirals(Vec2 center);

    EffectId id_;
    const LayerSource* layer_ = nullptr;
    core::Mt19937RandomSource rng_{0};
    FragmentSystem system_;
    bool initialized_ = false;
};

} // namespace core::fx
