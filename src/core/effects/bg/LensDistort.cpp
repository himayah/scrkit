#include "LensDistort.h"

#include <algorithm>
#include <cmath>

#include "../MeshDeformer.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

struct DeformParams {
    float k;
};

MeshVertex Deform(const MeshVertex& rest, float, float, const void* paramsVoid, float screenW, float screenH) {
    const auto& dp = *static_cast<const DeformParams*>(paramsVoid);
    MeshVertex out = rest;
    out.pos = LensDisplace(rest.pos, dp.k, screenW, screenH);
    return out;
}
} // namespace

Vec2 LensDisplace(Vec2 rest, float k, float screenW, float screenH) {
    const Vec2 c{screenW / 2.0f, screenH / 2.0f};
    const float nx = (rest.x - c.x) / (screenH / 2.0f);
    const float ny = (rest.y - c.y) / (screenH / 2.0f);
    const float r2 = nx * nx + ny * ny;
    const float r2Corner = 1.0f + (screenW / screenH) * (screenW / screenH);
    const float f = (1.0f + k * r2) / (1.0f + k * r2Corner);
    return {c.x + (rest.x - c.x) * f, c.y + (rest.y - c.y) * f};
}

void LensDistortEffect::Begin(const EffectContext& ctx) { layer_ = ctx.layer; }

void LensDistortEffect::Step(EffectFrame& frame) {
    const float r2Corner = 1.0f + (layer_->screenW / layer_->screenH) * (layer_->screenW / layer_->screenH);
    const float kMaxEffective = std::min(params_.kMax, 0.8f / r2Corner);
    const float k = frame.intensity * kMaxEffective * std::sin(2.0f * kPi * frame.t / params_.periodSec);

    DeformParams dp{k};
    ApplyDisplacement(layer_->restMesh, frame.out->mesh, frame.t, frame.intensity, &Deform, &dp, layer_->screenW,
                       layer_->screenH);
}

} // namespace core::fx
