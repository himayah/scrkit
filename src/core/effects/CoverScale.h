#pragma once
// Scale factors so a rotated/shifted/displaced full-screen background layer
// never exposes a screen-edge gap (DESIGN_EFFECTS.md §6.0.4).

#include <cmath>
#include <algorithm>

namespace core::fx {

inline float CoverScaleForRotation(float w, float h, float thetaRad) {
    const float c = std::fabs(std::cos(thetaRad));
    const float s = std::fabs(std::sin(thetaRad));
    return std::max((w * c + h * s) / w, (w * s + h * c) / h);
}

inline float CoverScaleForShift(float w, float h, float dx, float dy) {
    return std::max(1.0f + 2.0f * std::fabs(dx) / w, 1.0f + 2.0f * std::fabs(dy) / h);
}

inline float CoverScaleForDisplacement(float w, float h, float maxDispPx) {
    return 1.0f + 2.0f * maxDispPx / std::min(w, h);
}

} // namespace core::fx
