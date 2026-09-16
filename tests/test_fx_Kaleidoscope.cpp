#include "test_framework.h"

#include <cmath>

#include "../src/core/effects/KaleidoscopeFold.h"

using core::fx::FoldPoint;
using core::fx::Vec2;

namespace {
constexpr float kPi = 3.14159265358979323846f;

bool NearlyEqual(const Vec2& a, const Vec2& b, float eps = 1e-2f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps;
}
} // namespace

TEST_CASE(Kaleidoscope_FoldPointIsPeriodic) {
    const Vec2 c{960.0f, 540.0f};
    const int segments = 6;
    const float w = 2.0f * kPi / segments;
    const float phi = 0.4f;
    for (float alpha = 0.0f; alpha < 2.0f * kPi; alpha += 0.31f) {
        const Vec2 a = FoldPoint(200.0f, alpha, c, segments, phi, 1.0f, 0.0f, 1920.0f, 1080.0f);
        const Vec2 b = FoldPoint(200.0f, alpha + w, c, segments, phi, 1.0f, 0.0f, 1920.0f, 1080.0f);
        CHECK(NearlyEqual(a, b));
    }
}

TEST_CASE(Kaleidoscope_FoldPointIsMirrorSymmetric) {
    const Vec2 c{960.0f, 540.0f};
    const int segments = 8;
    const float w = 2.0f * kPi / segments;
    const float phi = 0.7f;
    for (float a = 0.0f; a < w; a += 0.1f) {
        const Vec2 p1 = FoldPoint(150.0f, phi + a, c, segments, phi, 1.0f, 0.0f, 1920.0f, 1080.0f);
        const Vec2 p2 = FoldPoint(150.0f, phi + (w - a), c, segments, phi, 1.0f, 0.0f, 1920.0f, 1080.0f);
        CHECK(NearlyEqual(p1, p2));
    }
}

TEST_CASE(Kaleidoscope_ZeroRadiusAlwaysMapsToCenter) {
    const Vec2 c{960.0f, 540.0f};
    for (float alpha = 0.0f; alpha < 6.0f; alpha += 0.5f) {
        const Vec2 p = FoldPoint(0.0f, alpha, c, 12, 0.3f, 1.0f, 0.9f, 1920.0f, 1080.0f);
        CHECK(NearlyEqual(p, c, 1e-3f));
    }
}

TEST_CASE(Kaleidoscope_ResultStaysWithinScreenRect) {
    const Vec2 c{960.0f, 540.0f};
    for (float r = 0.0f; r < 2000.0f; r += 137.0f) {
        for (float alpha = 0.0f; alpha < 6.0f; alpha += 0.9f) {
            const Vec2 p = FoldPoint(r, alpha, c, 6, 0.2f, 1.0f, 0.0f, 1920.0f, 1080.0f);
            CHECK(p.x >= 0.0f);
            CHECK(p.x <= 1920.0f);
            CHECK(p.y >= 0.0f);
            CHECK(p.y <= 1080.0f);
        }
    }
}
