#include "test_framework.h"

#include <cmath>

#include "../src/core/effects/TileMapper.h"

using core::fx::EdgeDuplicates;
using core::fx::Transform2D;
using core::fx::Vec2;
using core::fx::VisibleTileOffsets;
using core::fx::WrapPosition;

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

TEST_CASE(TileMapper_WrapPositionHandlesNegativeAndMultiples) {
    const Vec2 a = WrapPosition({-10.0f, 5.0f}, 100.0f, 50.0f);
    CHECK_NEAR(a.x, 90.0f, 1e-4f);
    CHECK_NEAR(a.y, 5.0f, 1e-4f);

    const Vec2 b = WrapPosition({250.0f, 125.0f}, 100.0f, 50.0f);
    CHECK_NEAR(b.x, 50.0f, 1e-4f);
    CHECK_NEAR(b.y, 25.0f, 1e-4f);

    const Vec2 c = WrapPosition({-250.0f, -30.0f}, 100.0f, 50.0f);
    CHECK_NEAR(c.x, 50.0f, 1e-4f);
    CHECK_NEAR(c.y, 20.0f, 1e-4f);
}

TEST_CASE(TileMapper_EdgeDuplicatesEmptyAtCenter) {
    const auto dups = EdgeDuplicates({960.0f, 540.0f}, 10.0f, 10.0f, 1920.0f, 1080.0f);
    CHECK_EQ(dups.size(), static_cast<size_t>(0));
}

TEST_CASE(TileMapper_EdgeDuplicatesOneAtSingleEdge) {
    const auto left = EdgeDuplicates({5.0f, 540.0f}, 10.0f, 10.0f, 1920.0f, 1080.0f);
    CHECK_EQ(left.size(), static_cast<size_t>(1));
    CHECK_NEAR(left[0].x, 1920.0f, 1e-4f);
    CHECK_NEAR(left[0].y, 0.0f, 1e-4f);

    const auto right = EdgeDuplicates({1918.0f, 540.0f}, 10.0f, 10.0f, 1920.0f, 1080.0f);
    CHECK_EQ(right.size(), static_cast<size_t>(1));
    CHECK_NEAR(right[0].x, -1920.0f, 1e-4f);
}

TEST_CASE(TileMapper_EdgeDuplicatesThreeAtCorner) {
    const auto corner = EdgeDuplicates({3.0f, 3.0f}, 10.0f, 10.0f, 1920.0f, 1080.0f);
    CHECK_EQ(corner.size(), static_cast<size_t>(3));
}

TEST_CASE(TileMapper_VisibleTileOffsetsAtZeroRotationIsJustCenter) {
    Transform2D t;
    t.pivot = {960.0f, 540.0f};
    const auto offsets = VisibleTileOffsets(t, 1920.0f, 1080.0f, 1920.0f, 1080.0f);
    CHECK_EQ(offsets.size(), static_cast<size_t>(1));
    CHECK_EQ(offsets[0].first, 0);
    CHECK_EQ(offsets[0].second, 0);
}

TEST_CASE(TileMapper_VisibleTileOffsetsAt45DegreesNeedsMoreThanJustCenter) {
    Transform2D t;
    t.pivot = {960.0f, 540.0f};
    t.rotateRad = kPi / 4.0f;
    const auto offsets = VisibleTileOffsets(t, 1920.0f, 1080.0f, 1920.0f, 1080.0f);
    // At 45 degrees the rotated tile's own AABB already spills past the
    // screen edges, so neighboring replicas are needed too (unlike the
    // zero-rotation case, which needs only the center tile). §6.0.3 only
    // guarantees the 3x3 block is *sufficient* to cover the screen, not
    // that every one of the 9 offsets individually intersects it.
    CHECK(offsets.size() > 1);
    CHECK(offsets.size() <= 9);
    bool hasCenter = false;
    for (const auto& o : offsets) {
        if (o.first == 0 && o.second == 0) hasCenter = true;
    }
    CHECK(hasCenter);
}

TEST_CASE(TileMapper_ThreeByThreeSufficiencyCondition) {
    // 16:9 and 21:9 both satisfy min(W,H) >= max(W,H)/sqrt(8) per §6.0.3.
    CHECK(1080.0f >= 1920.0f / std::sqrt(8.0f));
    CHECK(1080.0f >= 2560.0f / std::sqrt(8.0f));
}
