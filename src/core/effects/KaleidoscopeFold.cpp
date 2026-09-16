#include "KaleidoscopeFold.h"

#include <cmath>

#include "EffectMath.h"

namespace core::fx {

Vec2 FoldPoint(float r, float alpha, Vec2 c, int segments, float patternRotation, float zoom,
               float wedgeOriginRad, float screenW, float screenH) {
    const float kPi = 3.14159265358979323846f;
    const float wedge = 2.0f * kPi / static_cast<float>(segments);

    float a = std::fmod(alpha - patternRotation, wedge);
    if (a < 0.0f) a += wedge;
    if (a > wedge * 0.5f) a = wedge - a;

    const float radius = r / (zoom > 1e-4f ? zoom : 1e-4f);
    const float angle = a + wedgeOriginRad;
    Vec2 src{c.x + radius * std::cos(angle), c.y + radius * std::sin(angle)};
    src.x = Clamp(src.x, 0.0f, screenW);
    src.y = Clamp(src.y, 0.0f, screenH);
    return src;
}

} // namespace core::fx
