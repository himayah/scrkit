#include "FlagWave.h"

#include <cmath>

#include "../EffectMath.h"
#include "../MeshDeformer.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

float Gain(float u) { return 0.15f + 0.85f * u * u; }

float Phase(float u, float t, const FlagWaveParams& p) {
    return 2.0f * kPi * p.wavesAcross * u - 2.0f * kPi * p.hz * t;
}

MeshVertex Deform(const MeshVertex& rest, float t, float intensity, const void* paramsVoid, float screenW,
                   float screenH) {
    const auto& params = *static_cast<const FlagWaveParams*>(paramsVoid);
    const Vec2 d = FlagWaveDisplace(rest.pos, t, intensity, params, screenW, screenH);
    MeshVertex out = rest;
    out.pos = {rest.pos.x + d.x, rest.pos.y + d.y};
    out.shade = FlagWaveShade(rest.pos, t, intensity, params, screenW);
    return out;
}
} // namespace

Vec2 FlagWaveDisplace(Vec2 rest, float t, float intensity, const FlagWaveParams& params, float screenW,
                       float screenH) {
    const float u = rest.x / screenW;
    const float g = Gain(u);
    const float phi = Phase(u, t, params);
    const float d = std::sin(phi) + 0.5f * std::sin(2.3f * phi + 1.3f);
    const float dy = intensity * params.ampRatio * screenH * g * d / 1.5f;
    const float dx = params.shear * dy * (u - 0.5f);
    return {dx, dy};
}

float FlagWaveShade(Vec2 rest, float t, float intensity, const FlagWaveParams& params, float screenW) {
    const float u = rest.x / screenW;
    const float g = Gain(u);
    const float phi = Phase(u, t, params);
    return 1.0f - intensity * params.shadeDepth * Clamp(std::cos(phi), 0.0f, 1.0f) * g;
}

void FlagWaveEffect::Begin(const EffectContext& ctx) { layer_ = ctx.layer; }

void FlagWaveEffect::Step(EffectFrame& frame) {
    ApplyDisplacement(layer_->restMesh, frame.out->mesh, frame.t, frame.intensity, &Deform, &params_,
                       layer_->screenW, layer_->screenH);
}

} // namespace core::fx
