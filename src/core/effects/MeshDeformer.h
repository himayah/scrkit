#pragma once
// Applies a per-vertex deformation function to a rest Mesh to produce the
// current-frame Mesh (DESIGN_EFFECTS.md §3.1 MeshDeformer). Kept generic over
// a plain function pointer (not std::function) to avoid allocation on the
// hot path (§3.1).

#include "Mesh.h"

namespace core::fx {

// Computes one output vertex from the corresponding rest vertex. `params` is
// an effect-specific struct (and anything else the effect needs, e.g. a
// noise seed, bundled into that struct) cast back inside the function.
using VertexDeformFn = MeshVertex (*)(const MeshVertex& rest, float t, float intensity,
                                       const void* params, float screenW, float screenH);

// Copies rest's topology (cols/rows/quads/quadActive) into out and fills
// out.vertices by calling fn on every rest vertex. alpha is clamped to
// [0,1] and shade to [0,1] after the call (safety net: pseudo-shading only
// darkens, per §6.1's formulas, never brightens past the unlit vertex).
void ApplyDisplacement(const Mesh& rest, Mesh& out, float t, float intensity, VertexDeformFn fn,
                        const void* params, float screenW, float screenH);

} // namespace core::fx
