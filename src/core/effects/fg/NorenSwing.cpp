#include "NorenSwing.h"

#include <algorithm>
#include <cmath>

#include "../ForegroundMeshBuilder.h"

namespace core::fx {

// Why this effect can't use the generic per-vertex MeshDeformer::ApplyDisplacement
// pipeline every other Mesh-geometry effect uses: NorenSwing's displacement
// depends on which strip a vertex belongs to, and at a strip *boundary* the
// mesh has two coincident vertices (built with a seam column, §4.4.1) that
// must move independently -- but they're byte-identical MeshVertex values
// (same rest pos/uv), so a pure function of "the vertex's own value" has no
// way to tell them apart. The fix here is to walk the mesh per-quad instead:
// each quad unambiguously belongs to exactly one strip (from its column), so
// writing that quad's computed corner positions into *its own* referenced
// vertex indices naturally lands on the correct one of the two seam
// duplicates, without either needing to know about the other.
Vec2 NorenDisplace(Vec2 rest, int strip, float t, float intensity, const NorenSwingParams& params,
                    float screenW, float screenH) {
    const float kPi = 3.14159265358979323846f;
    const float v = rest.y / screenH;
    const float dx = intensity * params.ampRatio * screenW *
                      std::sin(2.0f * kPi * params.hz * t + params.phaseStep * strip + 0.4f * std::sin(0.7f * t)) *
                      std::pow(v, params.pinPow);
    const float dy = -0.12f * std::fabs(dx) * v;
    return {dx, dy};
}

void NorenSwingEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    std::vector<float> seamFractions;
    for (int k = 1; k < params_.strips; ++k) {
        seamFractions.push_back(static_cast<float>(k) / static_cast<float>(params_.strips));
    }
    seamedRestMesh_ = BuildForegroundRestMesh(*layer_, seamFractions);
}

void NorenSwingEffect::Step(EffectFrame& frame) {
    Mesh& out = frame.out->mesh;
    out.cols = seamedRestMesh_.cols;
    out.rows = seamedRestMesh_.rows;
    out.quads = seamedRestMesh_.quads;
    out.quadActive = seamedRestMesh_.quadActive;
    out.vertices = seamedRestMesh_.vertices; // start as rest copies; overwritten per-quad below

    const int cols = seamedRestMesh_.cols > 0 ? seamedRestMesh_.cols : 1;
    const int stripWidth = std::max(1, cols / params_.strips);
    for (size_t qi = 0; qi < seamedRestMesh_.quads.size(); ++qi) {
        const int col = static_cast<int>(qi) % cols;
        const int strip = std::min(params_.strips - 1, col / stripWidth);
        for (int idx : seamedRestMesh_.quads[qi]) {
            const MeshVertex& rv = seamedRestMesh_.vertices[static_cast<size_t>(idx)];
            const Vec2 d = NorenDisplace(rv.pos, strip, frame.t, frame.intensity, params_, layer_->screenW,
                                          layer_->screenH);
            out.vertices[static_cast<size_t>(idx)].pos = {rv.pos.x + d.x, rv.pos.y + d.y};
        }
    }
}

} // namespace core::fx
