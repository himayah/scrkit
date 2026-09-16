#include "Ripple.h"

#include <cmath>

#include "../CoverScale.h"
#include "../EffectMath.h"
#include "../MeshDeformer.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

struct DeformParams {
    RippleParams params;
    const std::vector<RippleDrop>* drops;
};

MeshVertex Deform(const MeshVertex& rest, float t, float intensity, const void* paramsVoid, float, float) {
    const auto& dp = *static_cast<const DeformParams*>(paramsVoid);
    const Vec2 d = RippleDisplace(rest.pos, t, *dp.drops, intensity, dp.params);
    MeshVertex out = rest;
    out.pos = {rest.pos.x + d.x, rest.pos.y + d.y};
    return out;
}
} // namespace

Vec2 RippleDisplace(Vec2 rest, float t, const std::vector<RippleDrop>& drops, float intensity,
                     const RippleParams& params) {
    float sumX = 0.0f, sumY = 0.0f;
    for (const auto& drop : drops) {
        const float tau = t - drop.spawnTime;
        if (tau < 0.0f) continue; // no contribution before the drop lands
        const float dx = rest.x - drop.origin.x;
        const float dy = rest.y - drop.origin.y;
        const float r = std::sqrt(dx * dx + dy * dy);
        const float invR = 1.0f / std::max(r, 1.0f);
        const float a = params.ampPx * std::sin(2.0f * kPi * r / params.wavelengthPx - 2.0f * kPi * params.hz * tau) *
                        std::exp(-r / params.decayPx) * std::exp(-tau / params.decaySec) * SmoothStep01(tau / 0.2f);
        sumX += a * dx * invR;
        sumY += a * dy * invR;
    }
    return {intensity * sumX, intensity * sumY};
}

void RippleEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    drops_.clear();
    nextSpawnTime_ = params_.spawnMinSec + rng_.NextFloat01() * (params_.spawnMaxSec - params_.spawnMinSec);
}

void RippleEffect::Step(EffectFrame& frame) {
    while (frame.t >= nextSpawnTime_) {
        drops_.push_back({{rng_.NextFloat01() * layer_->screenW, rng_.NextFloat01() * layer_->screenH},
                           nextSpawnTime_});
        if (static_cast<int>(drops_.size()) > params_.maxDrops) drops_.erase(drops_.begin());
        nextSpawnTime_ += params_.spawnMinSec + rng_.NextFloat01() * (params_.spawnMaxSec - params_.spawnMinSec);
    }

    DeformParams dp{params_, &drops_};
    ApplyDisplacement(layer_->restMesh, frame.out->mesh, frame.t, frame.intensity, &Deform, &dp, layer_->screenW,
                       layer_->screenH);

    Transform2D transform;
    transform.pivot = {layer_->screenW / 2.0f, layer_->screenH / 2.0f};
    transform.scale =
        CoverScaleForDisplacement(layer_->screenW, layer_->screenH,
                                   frame.intensity * params_.ampPx * static_cast<float>(params_.maxDrops) * 0.5f);
    frame.out->transform = transform;
}

} // namespace core::fx
