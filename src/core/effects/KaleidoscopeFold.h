#pragma once
// Radial fold used by Kaleidoscope / BackgroundKaleidoscope (DESIGN_EFFECTS.md
// §6.3.2). Pure function: screen coords -> folded-back source coords.

#include "EffectTypes.h"

namespace core::fx {

// r, alpha: polar coordinates of the destination point relative to c.
// segments: N wedges around the circle. patternRotation: phi(t), the live
// spin of the pattern. zoom: z(t). wedgeOriginRad: phi0, the fixed per-
// instance orientation of the reference wedge chosen at Begin (§6.1.13).
// Returns a source position already clamped to [0,screenW] x [0,screenH].
Vec2 FoldPoint(float r, float alpha, Vec2 c, int segments, float patternRotation, float zoom,
               float wedgeOriginRad, float screenW, float screenH);

} // namespace core::fx
