#include "ParticleGrid.h"

#include <algorithm>
#include <cmath>

namespace core {

int ComputeGridDimensionForParticleCount(int desiredParticleCount) {
    if (desiredParticleCount < 1) {
        return 1;
    }
    int n = static_cast<int>(std::sqrt(static_cast<double>(desiredParticleCount)));
    return std::max(1, n);
}

std::vector<Particle> BuildParticleGrid(const ParticleGridConfig& config) {
    std::vector<Particle> particles;
    const int n = std::max(1, config.gridN);
    particles.reserve(static_cast<size_t>(n) * static_cast<size_t>(n));

    const float cellW = config.screenWidth / static_cast<float>(n);
    const float cellH = config.screenHeight / static_cast<float>(n);
    const float uvStep = 1.0f / static_cast<float>(n);

    for (int row = 0; row < n; ++row) {
        for (int col = 0; col < n; ++col) {
            if (static_cast<int>(particles.size()) >= config.particleCount &&
                config.particleCount > 0) {
                return particles;
            }
            Particle p;
            p.x = (static_cast<float>(col) + 0.5f) * cellW;
            p.y = (static_cast<float>(row) + 0.5f) * cellH;
            p.u0 = static_cast<float>(col) * uvStep;
            p.v0 = static_cast<float>(row) * uvStep;
            p.u1 = p.u0 + uvStep;
            p.v1 = p.v0 + uvStep;
            particles.push_back(p);
        }
    }
    return particles;
}

} // namespace core
