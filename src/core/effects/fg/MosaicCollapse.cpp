#include "MosaicCollapse.h"

#include "../EffectMath.h"

namespace core::fx {

namespace {
struct IntegrateParams {
    MosaicCollapseParams params;
    float screenH, cellHalfH;
};

void Integrate(Fragment& f, const FragmentGroup*, const EffectFrame& frame, const void* paramsVoid) {
    const auto& ip = *static_cast<const IntegrateParams*>(paramsVoid);
    if (frame.t < f.delay) return; // §4.7 common contract: static before delay

    const float tau = frame.t - f.delay;
    f.vel.y += ip.params.gravity * frame.dt;
    f.pos.x += f.vel.x * frame.dt;
    f.pos.y += f.vel.y * frame.dt;
    f.rot += f.angVel * frame.dt;
    f.scaleX = f.scaleY = 1.0f - (1.0f - ip.params.shrinkTo) * SmoothStep01(tau / 0.8f);
    f.alive = f.pos.y < ip.screenH + 2.0f * ip.cellHalfH;
}
} // namespace

void MosaicCollapseEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    system_.InitFromCells(ctx.layer->cells);

    for (auto& f : system_.fragments) {
        f.delay = ((ctx.screenH - f.restPos.y) / ctx.screenH) * params_.collapseSeconds +
                   rng_.NextFloat01() * params_.jitter;
        f.vel = {(rng_.NextFloat01() * 2.0f - 1.0f) * params_.kickX, 0.0f};
        f.angVel = rng_.NextFloat01() * 2.0f - 1.0f;
    }
}

void MosaicCollapseEffect::Step(EffectFrame& frame) {
    IntegrateParams ip{params_, layer_->screenH, layer_->cellHalfH};
    system_.StepAll(&Integrate, frame, &ip);
    frame.out->quads.clear();
    system_.EmitQuads(layer_->cells, layer_->cellHalfW, layer_->cellHalfH, frame.out->quads);
}

} // namespace core::fx
