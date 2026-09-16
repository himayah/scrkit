#include "Mesh.h"

#include <cmath>

namespace core::fx {

namespace MeshBuilder {

Mesh BuildGrid(float screenW, float screenH, int cols, int rows, const std::vector<int>& seamColumns,
               const std::vector<bool>& activeCells, int cellSubdiv) {
    Mesh mesh;
    mesh.cols = cols;
    mesh.rows = rows;

    const int baseVertsPerRow = cols + 1;
    const int baseVertCount = baseVertsPerRow * (rows + 1);
    mesh.vertices.resize(static_cast<size_t>(baseVertCount));
    for (int r = 0; r <= rows; ++r) {
        for (int c = 0; c <= cols; ++c) {
            MeshVertex v;
            v.pos = {c * screenW / cols, r * screenH / rows};
            v.uv = {static_cast<float>(c) / cols, static_cast<float>(r) / rows};
            mesh.vertices[static_cast<size_t>(r * baseVertsPerRow + c)] = v;
        }
    }

    // asLeft[c][r] / asRight[c][r]: vertex index used when column c is the
    // left edge of the quad to its right, resp. the right edge of the quad
    // to its left. Equal (shared vertex) except at seam columns, where a
    // duplicate is created so strips on either side of the seam move
    // independently (§4.4.1, NorenSwing).
    std::vector<int> asLeft(static_cast<size_t>(baseVertsPerRow) * (rows + 1));
    for (int r = 0; r <= rows; ++r)
        for (int c = 0; c <= cols; ++c) asLeft[static_cast<size_t>(r * baseVertsPerRow + c)] = r * baseVertsPerRow + c;
    std::vector<int> asRight = asLeft;

    for (int sc : seamColumns) {
        if (sc < 0 || sc > cols) continue;
        for (int r = 0; r <= rows; ++r) {
            const MeshVertex original = mesh.vertices[static_cast<size_t>(r * baseVertsPerRow + sc)];
            const int dupIndex = static_cast<int>(mesh.vertices.size());
            mesh.vertices.push_back(original);
            asRight[static_cast<size_t>(r * baseVertsPerRow + sc)] = dupIndex;
        }
    }

    mesh.quads.reserve(static_cast<size_t>(cols) * rows);
    mesh.quadActive.assign(static_cast<size_t>(cols) * rows, true);

    const bool haveActiveCells = !activeCells.empty();
    const int gridN = cellSubdiv > 0 ? cols / cellSubdiv : cols;

    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            const int bl = asLeft[static_cast<size_t>(r * baseVertsPerRow + c)];
            const int br = asRight[static_cast<size_t>(r * baseVertsPerRow + (c + 1))];
            const int tr = asRight[static_cast<size_t>((r + 1) * baseVertsPerRow + (c + 1))];
            const int tl = asLeft[static_cast<size_t>((r + 1) * baseVertsPerRow + c)];
            mesh.quads.push_back({bl, br, tr, tl});

            const size_t quadIndex = static_cast<size_t>(r * cols + c);
            if (haveActiveCells) {
                const int parentRow = r / (cellSubdiv > 0 ? cellSubdiv : 1);
                const int parentCol = c / (cellSubdiv > 0 ? cellSubdiv : 1);
                const size_t parentIndex = static_cast<size_t>(parentRow) * gridN + parentCol;
                mesh.quadActive[quadIndex] = parentIndex < activeCells.size() && activeCells[parentIndex];
            }
        }
    }

    return mesh;
}

Mesh BuildRadial(float screenW, float screenH, int segments, int rings, int subdiv) {
    Mesh mesh; // cols/rows stay 0: this is not a grid mesh (§4.4)

    const float kPi = 3.14159265358979323846f;
    const Vec2 center{screenW / 2.0f, screenH / 2.0f};
    const float outerRadius = 0.5f * std::sqrt(screenW * screenW + screenH * screenH);
    const int halfWedges = 2 * segments;
    const float halfWedgeWidth = kPi / static_cast<float>(segments);
    const int vertsPerRing = subdiv + 1;
    const int vertsPerHalfWedge = (rings + 1) * vertsPerRing;

    mesh.vertices.resize(static_cast<size_t>(halfWedges) * vertsPerHalfWedge);
    for (int hw = 0; hw < halfWedges; ++hw) {
        for (int ring = 0; ring <= rings; ++ring) {
            const float radius = outerRadius * static_cast<float>(ring) / static_cast<float>(rings);
            for (int j = 0; j <= subdiv; ++j) {
                const float angle = hw * halfWedgeWidth + (halfWedgeWidth * j) / subdiv;
                MeshVertex v;
                v.pos = {center.x + radius * std::cos(angle), center.y + radius * std::sin(angle)};
                // Placeholder UV (identity fold); always overwritten by
                // Kaleidoscope/BackgroundKaleidoscope's Step() before drawing.
                v.uv = {v.pos.x / screenW, v.pos.y / screenH};
                const int index = hw * vertsPerHalfWedge + ring * vertsPerRing + j;
                mesh.vertices[static_cast<size_t>(index)] = v;
            }
        }
    }

    mesh.quads.reserve(static_cast<size_t>(halfWedges) * rings * subdiv);
    for (int hw = 0; hw < halfWedges; ++hw) {
        for (int ring = 0; ring < rings; ++ring) {
            for (int j = 0; j < subdiv; ++j) {
                const int base = hw * vertsPerHalfWedge;
                const int bl = base + ring * vertsPerRing + j;
                const int br = base + ring * vertsPerRing + (j + 1);
                const int tr = base + (ring + 1) * vertsPerRing + (j + 1);
                const int tl = base + (ring + 1) * vertsPerRing + j;
                mesh.quads.push_back({bl, br, tr, tl});
            }
        }
    }
    mesh.quadActive.assign(mesh.quads.size(), true);

    return mesh;
}

} // namespace MeshBuilder

} // namespace core::fx
