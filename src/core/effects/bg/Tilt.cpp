#include "Tilt.h"

#include <cmath>

#include "../CoverScale.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

Transform2D TiltTransform(float t, float intensity, const TiltParams& params, float screenW, float screenH) {
    const float theta = intensity * params.maxDeg * (kPi / 180.0f) * std::sin(2.0f * kPi * t / params.periodSec);
    // §6.2.4 / D-19: coverScale fixed at the *maximum* angle (always safe,
    // since it's monotonic in |theta| and |theta(t)| <= I*maxDeg <= maxDeg),
    // then scale interpolates 1->coverScale with I so I=0 stays a true
    // static match instead of a constant over-scale.
    const float coverScale = CoverScaleForRotation(screenW, screenH, params.maxDeg * kPi / 180.0f);
    const float scale = 1.0f + intensity * (coverScale - 1.0f);

    Transform2D transform;
    transform.pivot = {screenW / 2.0f, screenH / 2.0f};
    transform.rotateRad = theta;
    transform.scale = scale;
    return transform;
}

void TiltEffect::Begin(const EffectContext& ctx) {
    screenW_ = ctx.screenW;
    screenH_ = ctx.screenH;
}

void TiltEffect::Step(EffectFrame& frame) { frame.out->transform = TiltTransform(frame.t, frame.intensity, params_, screenW_, screenH_); }

} // namespace core::fx
