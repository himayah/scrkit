#include "NoiseRipple.h"

#include <algorithm>
#include <cmath>

#include "../CoverScale.h"
#include "../MeshDeformer.h"
#include "../NoiseField.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

struct DeformParams {
    NoiseRippleParams params;
    uint32_t seed;
};

MeshVertex Deform(const MeshVertex& rest, float t, float intensity, const void* paramsVoid, float screenW,
                   float screenH) {
    const auto& dp = *static_cast<const DeformParams*>(paramsVoid);
    const Vec2 d = NoiseRippleDisplace(rest.pos, t, intensity, dp.params, dp.seed, screenW, screenH);
    MeshVertex out = rest;
    out.pos = {rest.pos.x + d.x, rest.pos.y + d.y};
    return out;
}
} // namespace

Vec2 NoiseRippleDisplace(Vec2 rest, float t, float intensity, const NoiseRippleParams& params, uint32_t seed,
                          float screenW, float screenH) {
    const Vec2 c{screenW / 2.0f, screenH / 2.0f};
    const float dx = rest.x - c.x;
    const float dy = rest.y - c.y;
    const float r = std::sqrt(dx * dx + dy * dy);
    const float invR = 1.0f / std::max(r, 1.0f);
    const float nu = Fbm3(rest.x / params.noiseScalePx, rest.y / params.noiseScalePx, params.noiseTime * t, seed);
    const float a = params.ampPx * (0.6f + 0.4f * nu) *
                    std::sin(2.0f * kPi * r / params.wavelengthPx - 2.0f * kPi * params.hz * t + params.phaseNoise * nu);
    return {intensity * a * dx * invR, intensity * a * dy * invR};
}

void NoiseRippleEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    seed_ = ctx.seed;
}

void NoiseRippleEffect::Step(EffectFrame& frame) {
    DeformParams dp{params_, seed_};
    ApplyDisplacement(layer_->restMesh, frame.out->mesh, frame.t, frame.intensity, &Deform, &dp, layer_->screenW,
                       layer_->screenH);

    Transform2D transform;
    transform.pivot = {layer_->screenW / 2.0f, layer_->screenH / 2.0f};
    transform.scale = CoverScaleForDisplacement(layer_->screenW, layer_->screenH, frame.intensity * params_.ampPx);
    frame.out->transform = transform;
}

} // namespace core::fx
