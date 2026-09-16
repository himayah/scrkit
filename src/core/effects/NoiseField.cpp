#include "NoiseField.h"

#include <cmath>

#include "EffectMath.h"

namespace core::fx {

namespace {

// 32-bit integer hash of a 3D lattice point + seed (xorshift-multiply mix).
uint32_t HashLattice(int32_t ix, int32_t iy, int32_t iz, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(ix) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(iy) * 0xd8163841u;
    h ^= static_cast<uint32_t>(iz) * 0xcb1ab31fu;
    h = (h ^ (h >> 15)) * 0x2c1b3c6du;
    h = (h ^ (h >> 12)) * 0x297a2d39u;
    h ^= (h >> 15);
    return h;
}

// [0, 1)
float Hash01(int32_t ix, int32_t iy, int32_t iz, uint32_t seed) {
    return static_cast<float>(HashLattice(ix, iy, iz, seed)) / 4294967296.0f;
}

int FloorToInt(float v) { return static_cast<int>(std::floor(v)); }

} // namespace

float ValueNoise3(float x, float y, float z, uint32_t seed) {
    const int ix = FloorToInt(x);
    const int iy = FloorToInt(y);
    const int iz = FloorToInt(z);
    const float fx = x - static_cast<float>(ix);
    const float fy = y - static_cast<float>(iy);
    const float fz = z - static_cast<float>(iz);
    const float wx = SmoothStep01(fx);
    const float wy = SmoothStep01(fy);
    const float wz = SmoothStep01(fz);

    const float c000 = Hash01(ix, iy, iz, seed);
    const float c100 = Hash01(ix + 1, iy, iz, seed);
    const float c010 = Hash01(ix, iy + 1, iz, seed);
    const float c110 = Hash01(ix + 1, iy + 1, iz, seed);
    const float c001 = Hash01(ix, iy, iz + 1, seed);
    const float c101 = Hash01(ix + 1, iy, iz + 1, seed);
    const float c011 = Hash01(ix, iy + 1, iz + 1, seed);
    const float c111 = Hash01(ix + 1, iy + 1, iz + 1, seed);

    const float x00 = Lerp(c000, c100, wx);
    const float x10 = Lerp(c010, c110, wx);
    const float x01 = Lerp(c001, c101, wx);
    const float x11 = Lerp(c011, c111, wx);
    const float y0 = Lerp(x00, x10, wy);
    const float y1 = Lerp(x01, x11, wy);
    const float v = Lerp(y0, y1, wz); // [0, 1)

    return 2.0f * v - 1.0f;
}

float Fbm3(float x, float y, float z, uint32_t seed, int octaves, float lacunarity, float gain) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float normalizer = 0.0f;
    float freq = 1.0f;
    for (int k = 0; k < octaves; ++k) {
        sum += amplitude * ValueNoise3(x * freq, y * freq, z * freq, seed + static_cast<uint32_t>(k));
        normalizer += amplitude;
        amplitude *= gain;
        freq *= lacunarity;
    }
    return normalizer > 0.0f ? sum / normalizer : 0.0f;
}

} // namespace core::fx
