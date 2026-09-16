#include "MeshDeformer.h"

#include "EffectMath.h"

namespace core::fx {

void ApplyDisplacement(const Mesh& rest, Mesh& out, float t, float intensity, VertexDeformFn fn,
                        const void* params, float screenW, float screenH) {
    out.cols = rest.cols;
    out.rows = rest.rows;
    out.quads = rest.quads;
    out.quadActive = rest.quadActive;
    out.vertices.resize(rest.vertices.size());
    for (size_t i = 0; i < rest.vertices.size(); ++i) {
        MeshVertex v = fn(rest.vertices[i], t, intensity, params, screenW, screenH);
        v.alpha = Clamp(v.alpha, 0.0f, 1.0f);
        v.shade = Clamp(v.shade, 0.0f, 1.0f);
        out.vertices[i] = v;
    }
}

} // namespace core::fx
