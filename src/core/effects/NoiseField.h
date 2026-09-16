#pragma once
// Deterministic, table-free hash value noise (DESIGN_EFFECTS.md §6.0.2). No
// external dependency, <cmath> only. Used by LiquidDistort, NoiseRipple,
// NoiseDissolve, ZoomShake/ParallaxTilt's Noise1, etc.

#include <cstdint>

namespace core::fx {

// [-1, 1], continuous (C1) in x/y/z, deterministic per seed, grid points
// equal exactly 2*hash-1 for that lattice point.
float ValueNoise3(float x, float y, float z, uint32_t seed);

// Fractal Brownian motion: sum of octaves ValueNoise3 layers at increasing
// frequency (lacunarity) and decreasing amplitude (gain), normalized to stay
// within [-1, 1].
float Fbm3(float x, float y, float z, uint32_t seed, int octaves = 3, float lacunarity = 2.0f,
           float gain = 0.5f);

// 1D convenience wrapper for time-varying jitter (§6.2.3 ZoomShake, etc.).
inline float Noise1(float t, uint32_t seed) { return ValueNoise3(t, 0.37f, 0.71f, seed); }

} // namespace core::fx
