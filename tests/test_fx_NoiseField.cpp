#include "test_framework.h"

#include <cmath>

#include "../src/core/effects/NoiseField.h"

using core::fx::Fbm3;
using core::fx::Noise1;
using core::fx::ValueNoise3;

TEST_CASE(NoiseField_ValueNoise3StaysInRange) {
    for (int i = -20; i < 20; ++i) {
        for (int j = -20; j < 20; ++j) {
            const float v = ValueNoise3(i * 0.37f, j * 0.61f, 1.5f, 42);
            CHECK(v >= -1.0f);
            CHECK(v <= 1.0f);
        }
    }
}

TEST_CASE(NoiseField_ValueNoise3IsDeterministic) {
    for (float x = 0.0f; x < 5.0f; x += 0.7f) {
        const float a = ValueNoise3(x, 2.3f, -1.1f, 7);
        const float b = ValueNoise3(x, 2.3f, -1.1f, 7);
        CHECK_EQ(a, b);
    }
}

TEST_CASE(NoiseField_ValueNoise3IsContinuous) {
    // Small step in any axis should not produce a large jump (C1-ish check).
    const float eps = 1e-3f;
    for (float x = 0.1f; x < 5.0f; x += 0.9f) {
        const float a = ValueNoise3(x, 3.0f, 0.0f, 13);
        const float b = ValueNoise3(x + eps, 3.0f, 0.0f, 13);
        CHECK(std::fabs(a - b) < 0.05f);
    }
}

TEST_CASE(NoiseField_DifferentSeedsDiffer) {
    bool anyDifferent = false;
    for (float x = 0.0f; x < 5.0f; x += 0.5f) {
        if (ValueNoise3(x, 1.0f, 1.0f, 1) != ValueNoise3(x, 1.0f, 1.0f, 2)) {
            anyDifferent = true;
            break;
        }
    }
    CHECK(anyDifferent);
}

TEST_CASE(NoiseField_Fbm3StaysInRange) {
    for (float x = -3.0f; x < 3.0f; x += 0.3f) {
        const float v = Fbm3(x, 0.5f, 0.2f, 5);
        CHECK(v >= -1.0f);
        CHECK(v <= 1.0f);
    }
}

TEST_CASE(NoiseField_Noise1StaysInRangeAndDeterministic) {
    for (float t = 0.0f; t < 10.0f; t += 0.25f) {
        const float v = Noise1(t, 9);
        CHECK(v >= -1.0f);
        CHECK(v <= 1.0f);
        CHECK_EQ(v, Noise1(t, 9));
    }
}
