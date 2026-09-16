#include "ParallaxTilt.h"

#include <cmath>

#include "../CoverScale.h"
#include "../NoiseField.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

Transform2D ParallaxTiltTransform(float t, float intensity, const ParallaxTiltParams& params, uint32_t seed,
                                   float screenW, float screenH) {
    const Vec2 d{intensity * params.driftRatio * screenW * Noise1(params.driftHz * t, seed + 4),
                 intensity * params.driftRatio * screenW * Noise1(params.driftHz * t, seed + 5) * screenH / screenW};
    const float theta = intensity * params.rotDeg * (kPi / 180.0f) * Noise1(params.driftHz * t, seed + 6);
    const float coverScale = CoverScaleForShift(screenW, screenH, params.driftRatio * screenW, params.driftRatio * screenH) *
                              CoverScaleForRotation(screenW, screenH, params.rotDeg * kPi / 180.0f);

    Transform2D transform;
    transform.pivot = {screenW / 2.0f, screenH / 2.0f};
    transform.translate = d;
    transform.rotateRad = theta;
    transform.scale = 1.0f + intensity * (coverScale - 1.0f);
    return transform;
}

void ParallaxTiltEffect::Begin(const EffectContext& ctx) {
    seed_ = ctx.seed;
    screenW_ = ctx.screenW;
    screenH_ = ctx.screenH;
}

void ParallaxTiltEffect::Step(EffectFrame& frame) {
    frame.out->transform = ParallaxTiltTransform(frame.t, frame.intensity, params_, seed_, screenW_, screenH_);
}

} // namespace core::fx
