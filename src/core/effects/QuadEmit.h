#pragma once
// Shared axis-aligned (no rotation/scale) quad emission for GeometryKind::
// Tiles effects (InfiniteScroll, InfiniteRotation) that don't need
// FragmentSystem's per-fragment rotation/velocity state -- just "this cell,
// translated to this position, as one quad" (§6.3.1's corner convention).

#include <vector>

#include "DrawList.h"
#include "EffectTypes.h"

namespace core::fx {

inline void EmitAxisAlignedQuad(Vec2 center, float halfW, float halfH, float u0, float v0, float u1, float v1,
                                 float alpha, float shade, std::vector<QuadVertex>& out) {
    out.push_back({{center.x - halfW, center.y - halfH}, {u0, v0}, alpha, shade});
    out.push_back({{center.x + halfW, center.y - halfH}, {u1, v0}, alpha, shade});
    out.push_back({{center.x + halfW, center.y + halfH}, {u1, v1}, alpha, shade});
    out.push_back({{center.x - halfW, center.y + halfH}, {u0, v1}, alpha, shade});
}

} // namespace core::fx
