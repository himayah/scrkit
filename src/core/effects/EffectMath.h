#pragma once
// Small generic math helpers shared by many effects (DESIGN_EFFECTS.md §6.0
// notation table: sigma(u), etc.). Kept separate from EffectTypes.h since
// these are free functions, not types.

namespace core::fx {

// Smoothstep, sigma(u) = 3u^2 - 2u^3, with u first clamped to [0,1].
inline float SmoothStep01(float u) {
    if (u <= 0.0f) return 0.0f;
    if (u >= 1.0f) return 1.0f;
    return u * u * (3.0f - 2.0f * u);
}

inline float Clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

inline float Lerp(float a, float b, float u) { return a + (b - a) * u; }

} // namespace core::fx
