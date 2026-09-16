#include "test_framework.h"

#include "../src/core/effects/Mesh.h"
#include "../src/core/effects/MeshDeformer.h"

using core::fx::ApplyDisplacement;
using core::fx::Mesh;
using core::fx::MeshBuilder::BuildGrid;
using core::fx::MeshVertex;

namespace {
MeshVertex Identity(const MeshVertex& rest, float, float, const void*, float, float) { return rest; }

MeshVertex OutOfRangeAlphaShade(const MeshVertex& rest, float, float, const void*, float, float) {
    MeshVertex v = rest;
    v.alpha = 5.0f;
    v.shade = -3.0f;
    return v;
}
} // namespace

TEST_CASE(MeshDeformer_IdentityFieldMatchesRest) {
    const std::vector<bool> activeCells = {true, false, true, true};
    const Mesh rest = BuildGrid(1920.0f, 1080.0f, 2, 2, {}, activeCells);
    Mesh out;
    ApplyDisplacement(rest, out, 3.5f, 1.0f, &Identity, nullptr, 1920.0f, 1080.0f);

    CHECK_EQ(out.vertices.size(), rest.vertices.size());
    for (size_t i = 0; i < rest.vertices.size(); ++i) {
        CHECK_NEAR(out.vertices[i].pos.x, rest.vertices[i].pos.x, 1e-6f);
        CHECK_NEAR(out.vertices[i].pos.y, rest.vertices[i].pos.y, 1e-6f);
        CHECK_NEAR(out.vertices[i].uv.x, rest.vertices[i].uv.x, 1e-6f);
        CHECK_NEAR(out.vertices[i].uv.y, rest.vertices[i].uv.y, 1e-6f);
    }
}

TEST_CASE(MeshDeformer_QuadActiveIsPreserved) {
    const std::vector<bool> activeCells = {true, false, true, true};
    const Mesh rest = BuildGrid(1920.0f, 1080.0f, 2, 2, {}, activeCells);
    Mesh out;
    ApplyDisplacement(rest, out, 0.0f, 1.0f, &Identity, nullptr, 1920.0f, 1080.0f);
    CHECK_EQ(out.quadActive.size(), rest.quadActive.size());
    for (size_t i = 0; i < rest.quadActive.size(); ++i) CHECK_EQ(out.quadActive[i], rest.quadActive[i]);
}

TEST_CASE(MeshDeformer_ClampsAlphaAndShadeToUnitRange) {
    const Mesh rest = BuildGrid(1920.0f, 1080.0f, 2, 2, {}, {});
    Mesh out;
    ApplyDisplacement(rest, out, 0.0f, 1.0f, &OutOfRangeAlphaShade, nullptr, 1920.0f, 1080.0f);
    for (const auto& v : out.vertices) {
        CHECK_NEAR(v.alpha, 1.0f, 1e-6f);
        CHECK_NEAR(v.shade, 0.0f, 1e-6f);
    }
}
