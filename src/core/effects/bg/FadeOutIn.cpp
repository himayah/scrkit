#include "FadeOutIn.h"

#include <algorithm>

#include "../EffectMath.h"

namespace core::fx {

float FadeOutInAlpha(float t, float durationSeconds, float intensity, const FadeOutInParams& params) {
    const float T = durationSeconds > 1e-3f ? durationSeconds : 1e-3f;
    const float u = t / T;
    const float halfOut = (1.0f - params.holdRatio) / 2.0f;

    float a;
    if (u < halfOut) {
        a = 1.0f - SmoothStep01(u / halfOut);
    } else if (u > 1.0f - halfOut) {
        a = SmoothStep01((u - (1.0f - halfOut)) / halfOut);
    } else {
        a = 0.0f;
    }
    return 1.0f - intensity * (1.0f - std::max(a, params.floor));
}

void FadeOutInEffect::Begin(const EffectContext& ctx) { durationSeconds_ = ctx.durationSeconds; }

void FadeOutInEffect::Step(EffectFrame& frame) {
    frame.out->alpha = FadeOutInAlpha(frame.t, durationSeconds_, frame.intensity, params_);
}

} // namespace core::fx
