#pragma once
// Grid/radial mesh data (DESIGN_EFFECTS.md §4.4). MeshBuilder (BuildGrid /
// BuildRadial) lives in Mesh.cpp; this header is just the data shape so
// other Step-1 headers (DrawList.h, Layer.h) can reference it.

#include <array>
#include <vector>

#include "EffectTypes.h"

namespace core::fx {

struct MeshVertex {
    Vec2 pos;
    Vec2 uv;
    float alpha = 1.0f;
    float shade = 1.0f;
};

struct Mesh {
    int cols = 0; // valid only for grid meshes; 0 for radial meshes
    int rows = 0;
    std::vector<MeshVertex> vertices;
    std::vector<std::array<int, 4>> quads; // counter-clockwise vertex indices
    std::vector<bool> quadActive;          // false quads are not drawn (Fg: unchanged cells)
};

namespace MeshBuilder {

// §4.4.1. activeCells indexes a gridN x gridN parent grid (row-major); empty
// means "all active" (used for the background mesh, which is independent of
// the foreground diff grid). cellSubdiv > 1 splits each parent cell into
// cellSubdiv x cellSubdiv child quads (gridN < 48 case, §4.4.1); cols/rows
// must equal gridN * cellSubdiv when activeCells is non-empty (外部レビュー
// ISSUE-4 / D-20: exact mapping formula in DESIGN_EFFECTS.md §4.4.1).
Mesh BuildGrid(float screenW, float screenH, int cols, int rows,
               const std::vector<int>& seamColumns, const std::vector<bool>& activeCells,
               int cellSubdiv = 1);

// §4.4.2. Kaleidoscope radial mesh: 2*segments half-wedges x rings x subdiv
// quads. Positions are fixed for the mesh's lifetime; UV is filled in with a
// placeholder (identity fold) here because Kaleidoscope/BackgroundKaleidoscope
// always overwrite every vertex's UV every Step() (§6.1.13) from the live
// per-instance rotation/zoom/wedge-origin, so the initial UV is never drawn
// as-is (Entering/Exiting use Crossfade against the plain rest quad, not this
// mesh's own rest UV -- §5.2, §6.1.13).
Mesh BuildRadial(float screenW, float screenH, int segments, int rings, int subdiv);

} // namespace MeshBuilder

} // namespace core::fx
