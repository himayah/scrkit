#pragma once
// Noren-curtain swaying mesh distortion with strip seams, foreground
// continuous (DESIGN_EFFECTS.md §6.1.2).

#include "../EffectParams.h"
#include "../IEffect.h"
#include "../Mesh.h"

namespace core::fx {

// Pure math: strip is the vertex's [0, params.strips) band index.
Vec2 NorenDisplace(Vec2 rest, int strip, float t, float intensity, const NorenSwingParams& params,
                    float screenW, float screenH);

class NorenSwingEffect : public IEffect {
public:
    EffectId Id() const override { return EffectId::NorenSwing; }
    EffectKind Kind() const override { return EffectKind::Continuous; }
    GeometryKind Geometry() const override { return GeometryKind::Mesh; }
    ExitStrategy Exit() const override { return ExitStrategy::Envelope; }

    void Begin(const EffectContext& ctx) override;
    void Step(EffectFrame& frame) override;
    bool IsFinished() const override { return false; }

private:
    const LayerSource* layer_ = nullptr;
    NorenSwingParams params_;
    // Unlike every other Mesh-geometry effect, this needs its own rest mesh
    // (with seam columns at strip boundaries) rather than the layer's shared
    // one -- see the file comment in NorenSwing.cpp for why a generic
    // per-vertex MeshDeformer pass can't tell the two coincident seam
    // vertices apart, and why this effect writes to its mesh per-quad
    // instead.
    Mesh seamedRestMesh_;
};

} // namespace core::fx
