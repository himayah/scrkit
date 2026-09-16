#include "ZoomShake.h"

#include <cmath>

#include "../CoverScale.h"
#include "../NoiseField.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

Transform2D ZoomShakeTransform(float t, float intensity, const ZoomShakeParams& params, uint32_t seed,
                                float screenW, float screenH) {
    const float s = 1.0f + intensity * (params.zoomAmp * std::sin(2.0f * kPi * params.zoomHz * t) +
                                          params.jitterAmp * Noise1(params.jitterHz * t, seed + 1));
    const Vec2 d{intensity * params.shakePx * Noise1(params.shakeHz * t, seed + 2),
                 intensity * params.shakePx * Noise1(params.shakeHz * t, seed + 3)};
    const float coverScale =
        CoverScaleForShift(screenW, screenH, params.shakePx, params.shakePx) * (1.0f + params.zoomAmp + params.jitterAmp);

    Transform2D transform;
    transform.pivot = {screenW / 2.0f, screenH / 2.0f};
    transform.scale = s * coverScale;
    transform.translate = d;
    return transform;
}

void ZoomShakeEffect::Begin(const EffectContext& ctx) {
    seed_ = ctx.seed;
    screenW_ = ctx.screenW;
    screenH_ = ctx.screenH;
}

void ZoomShakeEffect::Step(EffectFrame& frame) {
    frame.out->transform = ZoomShakeTransform(frame.t, frame.intensity, params_, seed_, screenW_, screenH_);
}

} // namespace core::fx
