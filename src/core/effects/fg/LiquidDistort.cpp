#include "LiquidDistort.h"

#include "../MeshDeformer.h"
#include "../NoiseField.h"

namespace core::fx {

namespace {
// Bundles what the generic VertexDeformFn's single void* can carry.
struct DeformParams {
    LiquidDistortParams params;
    uint32_t seed;
};

MeshVertex Deform(const MeshVertex& rest, float t, float intensity, const void* paramsVoid, float screenW,
                   float screenH) {
    const auto& dp = *static_cast<const DeformParams*>(paramsVoid);
    return LiquidDistortVertex(rest, t, intensity, dp.params, dp.seed, screenW, screenH);
}
} // namespace

MeshVertex LiquidDistortVertex(const MeshVertex& rest, float t, float intensity,
                                const LiquidDistortParams& params, uint32_t seed, float screenW, float screenH) {
    const float lambda = params.wavelengthRatio * screenH;
    const float qx = rest.pos.x / lambda;
    const float qy = rest.pos.y / lambda;
    const float dx = intensity * params.ampRatio * screenH * Fbm3(qx, qy, params.timeScale * t, seed);
    const float dy = intensity * params.ampRatio * screenH * Fbm3(qx + 17.0f, qy + 31.0f, params.timeScale * t, seed);

    MeshVertex out = rest;
    out.pos = {rest.pos.x + dx, rest.pos.y + dy};
    out.uv = {rest.uv.x + params.refract * (dx / screenW), rest.uv.y + params.refract * (dy / screenH)};
    return out;
}

void LiquidDistortEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    seed_ = ctx.seed;
}

void LiquidDistortEffect::Step(EffectFrame& frame) {
    DeformParams dp{params_, seed_};
    ApplyDisplacement(layer_->restMesh, frame.out->mesh, frame.t, frame.intensity, &Deform, &dp, layer_->screenW,
                       layer_->screenH);
}

} // namespace core::fx
