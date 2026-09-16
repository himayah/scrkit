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
    Mesh restRadialMesh;      // lazily built; only needed by Kaleidoscope effects
    bool restRadialMeshBuilt = false;
    bool empty = false;       // Foreground only: true iff emptyReason != NotEmpty
    EmptyReason emptyReason = EmptyReason::NotEmpty;
};

} // namespace core::fx
