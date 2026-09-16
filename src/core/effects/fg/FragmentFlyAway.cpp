#include "FragmentFlyAway.h"

#include <cmath>

#include "../EffectMath.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

struct IntegrateParams {
    FragmentFlyAwayParams params;
    float screenW, screenH, cellHalfW, cellHalfH;
};

void Integrate(Fragment& f, const FragmentGroup*, const EffectFrame& frame, const void* paramsVoid) {
    const auto& ip = *static_cast<const IntegrateParams*>(paramsVoid);
    if (frame.t < f.delay) return; // §4.7 common contract: static before delay

    const float tau = frame.t - f.delay;
    f.vel.y += ip.params.gravity * frame.dt;
    const float dragFactor = std::exp(-ip.params.drag * frame.dt);
    f.vel.x *= dragFactor;
    f.vel.y *= dragFactor;
    f.pos.x += f.vel.x * frame.dt;
    f.pos.y += f.vel.y * frame.dt;
    f.rot += f.angVel * frame.dt;
    f.alpha = 1.0f - SmoothStep01(tau / ip.params.fadeSeconds);

    const bool withinScreen = f.pos.x > -2.0f * ip.cellHalfW && f.pos.x < ip.screenW + 2.0f * ip.cellHalfW &&
                               f.pos.y > -2.0f * ip.cellHalfH && f.pos.y < ip.screenH + 2.0f * ip.cellHalfH;
    f.alive = f.alpha > 0.0f && withinScreen;
}
} // namespace

void FragmentFlyAwayEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    system_.InitFromCells(ctx.layer->cells);

    // Terminal effects run at full envelope from the first frame (§6.0.1),
    // so ctx.params->intensity *is* the live "I" this Begin-time formula needs.
    const float intensity = ctx.params ? ctx.params->intensity : 0.7f;
    const Vec2 blastCenter{rng_.NextFloat01() * ctx.screenW, rng_.NextFloat01() * ctx.screenH};

    for (auto& f : system_.fragments) {
        const float dx = f.restPos.x - blastCenter.x;
        const float dy = f.restPos.y - blastCenter.y;
        const float dist = std::sqrt(dx * dx + dy * dy);
        const float baseAngle = std::atan2(dy, dx);
        const float jitterDeg = (rng_.NextFloat01() * 2.0f - 1.0f) * 20.0f; // +-20 degrees
        const float angle = baseAngle + jitterDeg * kPi / 180.0f;

        const float speed = (params_.speedMin + rng_.NextFloat01() * (params_.speedMax - params_.speedMin)) *
                             (0.5f + 0.5f * intensity);
        f.vel = {speed * std::cos(angle), speed * std::sin(angle)};
        f.angVel = (rng_.NextFloat01() * 2.0f - 1.0f) * params_.spinMax;
        f.delay = dist / params_.propagation;
    }
}

void FragmentFlyAwayEffect::Step(EffectFrame& frame) {
    IntegrateParams ip{params_, layer_->screenW, layer_->screenH, layer_->cellHalfW, layer_->cellHalfH};
    system_.StepAll(&Integrate, frame, &ip);
    frame.out->quads.clear();
    system_.EmitQuads(layer_->cells, layer_->cellHalfW, layer_->cellHalfH, frame.out->quads);
}

} // namespace core::fx
