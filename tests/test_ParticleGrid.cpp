#include "test_framework.h"

#include "../src/core/ParticleGrid.h"

using core::BuildParticleGrid;
using core::ComputeGridDimensionForParticleCount;
using core::ParticleGridConfig;

TEST_CASE(ParticleGrid_DimensionIsSqrtOfDesiredCount) {
    CHECK_EQ(ComputeGridDimensionForParticleCount(1000), 31); // 31*31=961
    CHECK_EQ(ComputeGridDimensionForParticleCount(3000), 54); // 54*54=2916
    CHECK_EQ(ComputeGridDimensionForParticleCount(0), 1);
    CHECK_EQ(ComputeGridDimensionForParticleCount(-5), 1);
}

TEST_CASE(ParticleGrid_BuildsExpectedParticleCountUpToCap) {
    ParticleGridConfig config;
    config.screenWidth = 1920.0f;
    config.screenHeight = 1080.0f;
    config.gridN = 10; // 100 cells
    config.particleCount = 100;
    auto particles = BuildParticleGrid(config);
    CHECK_EQ(particles.size(), static_cast<size_t>(100));
}

TEST_CASE(ParticleGrid_RespectsParticleCountCapBelowGridSize) {
    ParticleGridConfig config;
    config.screenWidth = 1920.0f;
    config.screenHeight = 1080.0f;
    config.gridN = 10; // 100 cells available
    config.particleCount = 42; // but caller only wants 42
    auto particles = BuildParticleGrid(config);
    CHECK_EQ(particles.size(), static_cast<size_t>(42));
}

TEST_CASE(ParticleGrid_ParticlesCoverScreenAndUvRange) {
    ParticleGridConfig config;
    config.screenWidth = 1000.0f;
    config.screenHeight = 500.0f;
    config.gridN = 4;
    config.particleCount = 16;
    auto particles = BuildParticleGrid(config);
    CHECK_EQ(particles.size(), static_cast<size_t>(16));
    for (const auto& p : particles) {
        CHECK(p.x >= 0.0f);
        CHECK(p.x <= config.screenWidth);
        CHECK(p.y >= 0.0f);
        CHECK(p.y <= config.screenHeight);
        CHECK(p.u0 >= 0.0f);
        CHECK(p.u1 <= 1.0001f);
        CHECK(p.v0 >= 0.0f);
        CHECK(p.v1 <= 1.0001f);
    }
}
