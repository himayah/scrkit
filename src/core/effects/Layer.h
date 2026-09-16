#pragma once
// A layer's immutable rest state (DESIGN_EFFECTS.md §4.1). Built once at
// Initialize() and never mutated afterwards; effects read from this and
// write their current-frame output into a separate LayerGeometry (DrawList.h).

#include <vector>

#include "../ParticleGrid.h"
#include "EffectTypes.h"
#include "Mesh.h"

namespace core::fx {

struct LayerSource {
    LayerKind kind = LayerKind::Background;
    TextureRole texture = TextureRole::Background;
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    int gridN = 1;
    std::vector<int> cellIndices;      // which grid cells this layer fragments into
    std::vector<core::Particle> cells; // core::Particle entries matching cellIndices, 1:1
    float cellHalfW = 0.0f;
    float cellHalfH = 0.0f;
    Mesh restMesh;
    // No restRadialMesh field: EffectContext only exposes LayerSource by
    // const pointer, so Kaleidoscope/BackgroundKaleidoscope each build and
    // own their own radial mesh in Begin() instead (like NorenSwing's own
    // seamed mesh, §6.1.2) rather than lazily populating a shared one here.
    bool empty = false; // Foreground only: true iff emptyReason != NotEmpty
    EmptyReason emptyReason = EmptyReason::NotEmpty;
};

} // namespace core::fx
