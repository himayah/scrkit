#include "ClothBend.h"

#include <cmath>

#include "../MeshDeformer.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

MeshVertex Deform(const MeshVertex& rest, float t, float intensity, const void* paramsVoid, float screenW,
                   float screenH) {
    const auto& params = *static_cast<const ClothBendParams*>(paramsVoid);
    const Vec2 d = ClothBendDisplace(rest.pos, t, intensity, params, screenW, screenH);
    MeshVertex out = rest;
    out.pos = {rest.pos.x + d.x, rest.pos.y + d.y};
    out.shade = ClothBendShade(rest.pos, t, intensity, params, screenW, screenH);
    return out;
}
} // namespace

Vec2 ClothBendDisplace(Vec2 rest, float t, float intensity, const ClothBendParams& params, float screenW,
                        float screenH) {
    const float u = rest.x / screenW;
    const float v = rest.y / screenH;
    const float sag = params.sagRatio * screenH * (0.8f + 0.2f * std::sin(2.0f * kPi * params.hz * t));
    const float dy = intensity * sag * std::sin(kPi * u) * std::sqrt(std::max(v, 0.0f));
    const float dx = -intensity * params.contractRatio * screenW * std::sin(2.0f * kPi * u) * v;
    return {dx, dy};
}

float ClothBendShade(Vec2 rest, float t, float intensity, const ClothBendParams& params, float screenW,
                      float screenH) {
    const float u = rest.x / screenW;
    const float v = rest.y / screenH;
    return 1.0f - intensity * params.shadeDepth * std::sin(kPi * u) * v *
                       (0.5f + 0.5f * std::cos(2.0f * kPi * params.hz * t));
}

void ClothBendEffect::Begin(const EffectContext& ctx) { layer_ = ctx.layer; }

void ClothBendEffect::Step(EffectFrame& frame) {
    ApplyDisplacement(layer_->restMesh, frame.out->mesh, frame.t, frame.intensity, &Deform, &params_,
                       layer_->screenW, layer_->screenH);
}

} // namespace core::fx
