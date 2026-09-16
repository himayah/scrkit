#pragma once
// core's rendering output (DESIGN_EFFECTS.md §4.6). platform::Renderer reads
// this and translates it into fixed-function GL calls (§7.2); nothing in this
// header depends on GL/Windows.

#include <vector>

#include "EffectTypes.h"
#include "Mesh.h"

namespace core::fx {

struct QuadVertex {
    Vec2 pos;
    Vec2 uv;
    float alpha = 1.0f;
    float shade = 1.0f;
};

// Scratch buffer an IEffect::Step writes into every frame (one per layer,
// reused across frames -- no per-frame allocation, §7 / A.7 policy).
struct LayerGeometry {
    Mesh mesh;                     // valid for Mesh / RadialMesh geometry
    std::vector<QuadVertex> quads; // valid for Tiles / Bands / Fragments geometry (4 per quad)
    Transform2D transform;         // overall transform (Transform geometry, or a shared one for Tiles)
    float alpha = 1.0f;
    int textureIndex = 0;          // HueRing index when texture == TextureRole::HueRing
    // GeometryKind::Bands only (GlitchShift, §6.2.9): half the R/G channel
    // pixel offset EffectEngine splits into 3 colorMask-restricted batches
    // around `quads` (0 = no split, drawn as one ordinary batch instead).
    float bandsRgbSplitPx = 0.0f;

    void Clear() {
        mesh = Mesh{};
        quads.clear();
        transform = Transform2D{};
        alpha = 1.0f;
        textureIndex = 0;
        bandsRgbSplitPx = 0.0f;
    }
};

struct DrawBatch {
    TextureRole texture = TextureRole::Background;
    int textureIndex = 0;
    Transform2D transform;
    float alpha = 1.0f;
    ColorMask colorMask;
    BlendMode blend = BlendMode::Normal;
    // Exactly one of these is non-null.
    const Mesh* mesh = nullptr;
    const std::vector<QuadVertex>* quads = nullptr;
};

struct FrameDrawList {
    std::vector<DrawBatch> batches; // background batches first, then foreground (§4.6)

    void Clear() { batches.clear(); }
};

} // namespace core::fx
