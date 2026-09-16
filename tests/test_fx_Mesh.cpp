#include "test_framework.h"

#include <cmath>

#include "../src/core/effects/Mesh.h"

using core::fx::Mesh;
using core::fx::MeshBuilder::BuildGrid;
using core::fx::MeshBuilder::BuildRadial;

TEST_CASE(Mesh_BuildGrid_VertexAndQuadCounts) {
    const Mesh mesh = BuildGrid(1920.0f, 1080.0f, 10, 6, {}, {});
    CHECK_EQ(mesh.vertices.size(), static_cast<size_t>(11 * 7));
    CHECK_EQ(mesh.quads.size(), static_cast<size_t>(10 * 6));
    CHECK_EQ(mesh.quadActive.size(), static_cast<size_t>(10 * 6));
}

TEST_CASE(Mesh_BuildGrid_SeamColumnsAddDuplicateVertices) {
    const Mesh noSeam = BuildGrid(1920.0f, 1080.0f, 10, 6, {}, {});
    const Mesh withSeam = BuildGrid(1920.0f, 1080.0f, 10, 6, {4}, {});
    // One duplicate vertex per row (rows+1 rows) for the single seam column.
    CHECK_EQ(withSeam.vertices.size(), noSeam.vertices.size() + 7);
}

TEST_CASE(Mesh_BuildGrid_SeamSplitsLeftAndRightQuadsToDifferentIndices) {
    const int cols = 10, rows = 6;
    const Mesh mesh = BuildGrid(1920.0f, 1080.0f, cols, rows, {4}, {});
    for (int r = 0; r < rows; ++r) {
        const auto& leftQuad = mesh.quads[static_cast<size_t>(r * cols + 3)];  // spans columns 3-4
        const auto& rightQuad = mesh.quads[static_cast<size_t>(r * cols + 4)]; // spans columns 4-5
        // leftQuad's right edge (indices 1,2) must differ from rightQuad's left edge (indices 0,3)
        CHECK(leftQuad[1] != rightQuad[0]);
        CHECK(leftQuad[2] != rightQuad[3]);
        // but they still start out at the same screen position
        const auto& a = mesh.vertices[static_cast<size_t>(leftQuad[1])];
        const auto& b = mesh.vertices[static_cast<size_t>(rightQuad[0])];
        CHECK_NEAR(a.pos.x, b.pos.x, 1e-4f);
        CHECK_NEAR(a.pos.y, b.pos.y, 1e-4f);
    }
}

TEST_CASE(Mesh_BuildGrid_UvStaysInUnitRange) {
    const Mesh mesh = BuildGrid(1920.0f, 1080.0f, 7, 5, {}, {});
    for (const auto& v : mesh.vertices) {
        CHECK(v.uv.x >= 0.0f);
        CHECK(v.uv.x <= 1.0f);
        CHECK(v.uv.y >= 0.0f);
        CHECK(v.uv.y <= 1.0f);
    }
}

TEST_CASE(Mesh_BuildGrid_QuadIndicesInRange) {
    const Mesh mesh = BuildGrid(1920.0f, 1080.0f, 5, 4, {2}, {});
    for (const auto& q : mesh.quads) {
        for (int idx : q) {
            CHECK(idx >= 0);
            CHECK(static_cast<size_t>(idx) < mesh.vertices.size());
        }
    }
}

TEST_CASE(Mesh_BuildGrid_SubdivisionCopiesParentActiveFlagToAllChildren) {
    // gridN=4, cellSubdiv=2 -> cols=rows=8. Checkerboard parent activity.
    const int gridN = 4, subdiv = 2, cols = gridN * subdiv;
    std::vector<bool> activeCells(static_cast<size_t>(gridN) * gridN);
    for (int r = 0; r < gridN; ++r)
        for (int c = 0; c < gridN; ++c) activeCells[static_cast<size_t>(r) * gridN + c] = (r + c) % 2 == 0;

    const Mesh mesh = BuildGrid(1920.0f, 1080.0f, cols, cols, {}, activeCells, subdiv);

    for (int r = 0; r < cols; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int parentRow = r / subdiv, parentCol = c / subdiv;
            const bool expected = activeCells[static_cast<size_t>(parentRow) * gridN + parentCol];
            CHECK_EQ(mesh.quadActive[static_cast<size_t>(r * cols + c)], expected);
        }
    }
}

TEST_CASE(Mesh_BuildGrid_SubdivisionChildUvSubdividesParentUvExactly) {
    const int gridN = 4, subdiv = 3, cols = gridN * subdiv;
    const std::vector<bool> activeCells(static_cast<size_t>(gridN) * gridN, true);
    const Mesh mesh = BuildGrid(1920.0f, 1080.0f, cols, cols, {}, activeCells, subdiv);

    for (int r = 0; r < cols; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int parentRow = r / subdiv, parentCol = c / subdiv;
            const float parentU0 = static_cast<float>(parentCol) / gridN;
            const float parentU1 = static_cast<float>(parentCol + 1) / gridN;
            const float parentV0 = static_cast<float>(parentRow) / gridN;
            const float parentV1 = static_cast<float>(parentRow + 1) / gridN;
            const auto& quad = mesh.quads[static_cast<size_t>(r * cols + c)];
            for (int idx : quad) {
                const auto& v = mesh.vertices[static_cast<size_t>(idx)];
                CHECK(v.uv.x >= parentU0 - 1e-5f);
                CHECK(v.uv.x <= parentU1 + 1e-5f);
                CHECK(v.uv.y >= parentV0 - 1e-5f);
                CHECK(v.uv.y <= parentV1 + 1e-5f);
            }
        }
    }
}

TEST_CASE(Mesh_BuildRadial_QuadCountMatchesFormula) {
    const int segments = 6, rings = 8, subdiv = 3;
    const Mesh mesh = BuildRadial(1920.0f, 1080.0f, segments, rings, subdiv);
    CHECK_EQ(mesh.quads.size(), static_cast<size_t>(2 * segments * rings * subdiv));
}

TEST_CASE(Mesh_BuildRadial_AllVerticesLieWithinTheirHalfWedge) {
    const int segments = 5, rings = 4, subdiv = 3;
    const Mesh mesh = BuildRadial(1920.0f, 1080.0f, segments, rings, subdiv);
    const float kPi = 3.14159265358979323846f;
    const float cx = 960.0f, cy = 540.0f;
    const float halfWedgeWidth = kPi / segments;
    const int vertsPerRing = subdiv + 1;
    const int vertsPerHalfWedge = (rings + 1) * vertsPerRing;

    for (int hw = 0; hw < 2 * segments; ++hw) {
        const float lo = hw * halfWedgeWidth;
        const float hi = (hw + 1) * halfWedgeWidth;
        for (int idx = 0; idx < vertsPerHalfWedge; ++idx) {
            const auto& v = mesh.vertices[static_cast<size_t>(hw * vertsPerHalfWedge + idx)];
            const float dx = v.pos.x - cx, dy = v.pos.y - cy;
            const float r = std::sqrt(dx * dx + dy * dy);
            if (r < 1e-3f) continue; // center vertices have an undefined angle
            float angle = std::atan2(dy, dx);
            if (angle < 0.0f) angle += 2.0f * kPi;
            // atan2 can only return values in [0, 2pi); nudge back up by a
            // full turn if this half-wedge's range straddles the 2pi wrap
            // point and atan2 happened to report the low (near-zero) side.
            if (angle < lo - 1e-3f) angle += 2.0f * kPi;
            CHECK(angle >= lo - 1e-3f);
            CHECK(angle <= hi + 1e-3f);
        }
    }
}
