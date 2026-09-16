#pragma once
// Infinite scrolling (wrap-around translation), foreground continuous
// (DESIGN_EFFECTS.md §6.1.3).

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../../RandomSource.h"

namespace core::fx {

Vec2 ScrollOffsetStep(Vec2 offset, Vec2 velocity, float dt);
// Nearest point (independently per axis) where offset wraps seamlessly.
Vec2 ScrollRestTarget(Vec2 offset, float screenW, float screenH);

class InfiniteScrollEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::InfiniteScroll; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Tiles; }
    ExitStrategy Exit() const override { return ExitStrategy::ReturnToRest; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

    void RequestExit(float exitSeconds) override;
    bool IsAtRest() const override { return exiting_ && exitElapsed_ >= exitDuration_; }

private:
    const LayerSource* layer_ = nullptr;
    InfiniteScrollParams params_;
    core::Mt19937RandomSource rng_{0};

    Vec2 direction_{1.0f, 0.0f};
    Vec2 offset_{0.0f, 0.0f};

    bool exiting_ = false;
    Vec2 exitStartOffset_{0.0f, 0.0f};
    Vec2 restTarget_{0.0f, 0.0f};
    float exitElapsed_ = 0.0f;
    float exitDuration_ = 0.0f;
};

} // namespace core::fx
