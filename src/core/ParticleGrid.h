#pragma once
// Splits the background image into an NxN grid of particles (要件.txt §5).

#include <cstdint>
#include <vector>

namespace core {

struct Particle {
    float x = 0.0f;       // initial screen-space center X
    float y = 0.0f;       // initial screen-space center Y
    float u0 = 0.0f;      // texture coords of this particle's cell (top-left)
    float v0 = 0.0f;
    float u1 = 0.0f;      // texture coords (bottom-right)
    float v1 = 0.0f;
};

struct ParticleGridConfig {
    float screenWidth = 1920.0f;
    float screenHeight = 1080.0f;
    int gridN = 64;      // grid is gridN x gridN cells
    int particleCount = 4096; // upper bound; grid cells beyond this are dropped
};

// Computes gridN chosen so that gridN*gridN is close to (but not exceeding)
// desiredParticleCount, with a minimum grid of 1x1.
int ComputeGridDimensionForParticleCount(int desiredParticleCount);

// Lays out particles covering the full screen in a regular grid, each one
// mapped to the corresponding cell of the source image's UV space.
std::vector<Particle> BuildParticleGrid(const ParticleGridConfig& config);

} // namespace core
