#pragma once
// Per-cell fragment state for GeometryKind::Fragments effects (DESIGN_EFFECTS.md
// §4.7). Struct definitions only; FragmentSystem (StepAll/AllDead/EmitQuads)
// is implemented alongside the other Step-3 geometry helpers.

#include "../SpiralMath.h"
#include "EffectTypes.h"

namespace core::fx {

struct EffectFrame; // fwd decl, defined in IEffect.h

struct Fragment {
    int cellIndex = -1; // index into LayerSource::cells
    Vec2 restPos;
    Vec2 pos;
    Vec2 vel{0.0f, 0.0f};
    float rot = 0.0f;
    float angVel = 0.0f;
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float alpha = 1.0f;
    float shade = 1.0f;
    int group = -1;   // shard/strip membership, -1 = independent
    float delay = 0.0f; // seconds before this fragment starts moving (§4.7:
                        // while t - delay < 0 it stays at restPos/rot=0/scale=1,
                        // the FragmentSystem-wide default contract, 外部レビュー ISSUE-6)
    bool alive = true;
    // VortexSuction / BackgroundSuction only (reuses core::SpiralMath as-is)
    core::SpiralState spiral;
    core::SpiralParams spiralParams;
};

struct FragmentGroup {
    Vec2 centroid;
    Vec2 vel{0.0f, 0.0f};
    float rot = 0.0f;
    float angVel = 0.0f;
    float delay = 0.0f;
};

// Pure function advancing one fragment by one frame; side effects on f only.
using FragmentIntegrator = void (*)(Fragment& f, const FragmentGroup* g, const EffectFrame& frame,
                                     const void* params);

} // namespace core::fx
