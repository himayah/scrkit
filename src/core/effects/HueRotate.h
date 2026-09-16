#pragma once
// Whole-buffer hue rotation (DESIGN_EFFECTS.md §6.2.8). Fixed-function GL 1.1
// cannot mix color channels at draw time, so HueShift pre-generates rotated
// copies of the background on the CPU; this is the pure per-pixel math.

#include <cstdint>

namespace core::fx {

// Rotates every pixel's RGB by `degrees` around the (1,1,1)/sqrt(3) gray
// axis (Rodrigues rotation) -- a standard hue-rotation approximation. Alpha
// passes through unchanged. src/dst are w*h RGBA8 buffers of the same size;
// dst may not alias src.
void HueRotateRgba(const uint8_t* src, uint8_t* dst, int w, int h, float degrees);

} // namespace core::fx
