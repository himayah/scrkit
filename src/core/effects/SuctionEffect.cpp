#include "SuctionEffect.h"

namespace core::fx {

namespace {
void Integrate(Fragment& f, const FragmentGroup*, const EffectFrame& frame, const void*) {
    const Vec2 p = core::StepSpiral(f.spiral, f.spiralParams, frame.suctionCenter.x, frame.suctionCenter.y);
    f.pos = p;
    f.alive = f.spiral.alive;
}
} // namespace

void SuctionEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    system_.InitFromCells(ctx.layer->cells);
    initialized_ = false;
}

void SuctionEffect::InitSpirals(Vec2 center) {
    const bool isBackground = id_ == EffectId::BackgroundSuction;
    // kContentSuctionSpeed / kLightweightParticleThreshold / kParticleCenterAccelFactor
    // from the existing AppController (DESIGN_EFFECTS.md appendix A.4.8).
    const float suctionSpeed =
        !isBackground ? 2.0f : (layer_->cells.size() > 3000 ? 0.5f : 2.0f);
    const float centerAccelFactor = isBackground ? 4.0f : 0.0f;

    for (auto& f : system_.fragments) {
        f.spiral = core::MakeSpiralState(f.restPos.x, f.restPos.y, center.x, center.y);
        const float targetRevolutions = 1.5f + rng_.NextFloat01() * 1.0f; // U(1.5, 2.5)
        f.spiralParams = core::MakeParamsForRevolutions(f.spiral.r, suctionSpeed, targetRevolutions);
        f.spiralParams.centerAccelFactor = centerAccelFactor;
    }
}

void SuctionEffect::Step(EffectFrame& frame) {
    if (!initialized_) {
        InitSpirals(frame.suctionCenter);
        initialized_ = true;
    }
    system_.StepAll(&Integrate, frame, nullptr);
    frame.out->quads.clear();
    system_.EmitQuads(layer_->cells, layer_->cellHalfW, layer_->cellHalfH, frame.out->quads);
}

} // namespace core::fx
