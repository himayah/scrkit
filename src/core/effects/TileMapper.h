#pragma once
// Infinite-scroll/rotation tiling helpers (DESIGN_EFFECTS.md §6.0.3).

#include <vector>

#include "EffectTypes.h"

namespace core::fx {

// Wraps p into [0, periodW) x [0, periodH).
Vec2 WrapPosition(Vec2 p, float periodW, float periodH);

// Which of the 8 neighboring-period offsets a cell rect centered at pWrapped
// (half-size halfW x halfH) needs an extra duplicate drawn at, so it doesn't
// visibly pop at the screen edge while wrapping (§6.0.3).
std::vector<Vec2> EdgeDuplicates(Vec2 pWrapped, float halfW, float halfH, float screenW, float screenH);

// Which of the 3x3 tile replicas (i,j) in {-1,0,1}^2 -- each the WxH source
// tile translated by (i*tileW, j*tileH) and then passed through `transform`
// -- have an AABB that intersects the screen rect [0,W]x[0,H]. Falls back to
// a 5x5 search (i,j in {-2..2}) for extreme aspect ratios where 3x3 isn't
// provably sufficient (§6.0.3: min(W,H) >= max(W,H)/sqrt(8)).
std::vector<std::pair<int, int>> VisibleTileOffsets(const Transform2D& transform, float tileW, float tileH,
                                                     float screenW, float screenH);

} // namespace core::fx
