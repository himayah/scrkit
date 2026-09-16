#include "HueRotate.h"

#include <cmath>

#include "EffectMath.h"

namespace core::fx {

void HueRotateRgba(const uint8_t* src, uint8_t* dst, int w, int h, float degrees) {
    const float kPi = 3.14159265358979323846f;
    const float theta = degrees * kPi / 180.0f;
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    const float sqrt3 = 1.7320508075688772f;
    const float oneMinusC = (1.0f - c) / 3.0f;
    const float sOverSqrt3 = s / sqrt3;

    // Rotation matrix around the (1,1,1)/sqrt(3) axis (Rodrigues formula).
    const float m00 = c + oneMinusC, m01 = oneMinusC - sOverSqrt3, m02 = oneMinusC + sOverSqrt3;
    const float m10 = oneMinusC + sOverSqrt3, m11 = c + oneMinusC, m12 = oneMinusC - sOverSqrt3;
    const float m20 = oneMinusC - sOverSqrt3, m21 = oneMinusC + sOverSqrt3, m22 = c + oneMinusC;

    const int pixelCount = w * h;
    for (int i = 0; i < pixelCount; ++i) {
        const uint8_t* p = src + i * 4;
        uint8_t* q = dst + i * 4;
        const float r = static_cast<float>(p[0]);
        const float g = static_cast<float>(p[1]);
        const float b = static_cast<float>(p[2]);

        const float r2 = m00 * r + m01 * g + m02 * b;
        const float g2 = m10 * r + m11 * g + m12 * b;
        const float b2 = m20 * r + m21 * g + m22 * b;

        q[0] = static_cast<uint8_t>(Clamp(r2, 0.0f, 255.0f) + 0.5f);
        q[1] = static_cast<uint8_t>(Clamp(g2, 0.0f, 255.0f) + 0.5f);
        q[2] = static_cast<uint8_t>(Clamp(b2, 0.0f, 255.0f) + 0.5f);
        q[3] = p[3];
    }
}

} // namespace core::fx
