#include "WaveZoom.h"

#include <cmath>

#include "../MeshDeformer.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

struct DeformParams {
    WaveZoomParams params;
    bool horizontal;
};

MeshVertex Deform(const MeshVertex& rest, float t, float intensity, const void* paramsVoid, float screenW,
                   float screenH) {
    const auto& dp = *static_cast<const DeformParams*>(paramsVoid);
    MeshVertex out = rest;
    out.pos = WaveZoomDisplace(rest.pos, t, intensity, dp.params, dp.horizontal, screenW, screenH);
    return out;
}
} // namespace

Vec2 WaveZoomDisplace(Vec2 rest, float t, float intensity, const WaveZoomParams& params, bool horizontal,
                       float screenW, float screenH) {
    const float u = horizontal ? rest.x / screenW : rest.y / screenH;
    const float s = 1.0f + intensity * params.zoomAmp * std::sin(2.0f * kPi * params.wavesAcross * u -
                                                                   2.0f * kPi * params.hz * t);
    const float coverScale = 1.0f + intensity * params.zoomAmp;
    const Vec2 c{screenW / 2.0f, screenH / 2.0f};
    return {c.x + (rest.x - c.x) * s * coverScale, c.y + (rest.y - c.y) * s * coverScale};
}

void WaveZoomEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    horizontal_ = rng_.NextFloat01() < 0.5f;
}

void WaveZoomEffect::Step(EffectFrame& frame) {
    DeformParams dp{params_, horizontal_};
    ApplyDisplacement(layer_->restMesh, frame.out->mesh, frame.t, frame.intensity, &Deform, &dp, layer_->screenW,
                       layer_->screenH);
}

} // namespace core::fx
