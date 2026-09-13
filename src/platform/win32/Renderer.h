#pragma once
// Batched fixed-function OpenGL drawing helpers (要件.txt §7: 固定機能版での
// 最適化 -- glBegin/glEnd を種別ごとに1回だけ呼ぶ、色計算はCPUで行わない).
// This module knows nothing about the state machine or spiral math; it only
// draws the vertex data AppController hands it each frame.

#include <vector>

#include "GLCompat.h"

namespace platform {

// A single particle: current center position + the UV cell it samples from
// a texture. Particle size is fixed (passed once, not per particle) per
// 要件.txt §7. Used both for the "content" phase (real-desktop-capture
// texture, only the grid cells core::ContentMask flagged as differing from
// the wallpaper) and the background phase (wallpaper texture, every cell).
struct DrawParticle {
    float x = 0.0f;
    float y = 0.0f;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

// Sets an orthographic 2D projection matching the screen in pixels, with
// (0,0) at the top-left -- the natural coordinate space for the desktop
// elements this saver simulates. Call once after the GL context is created
// and again if the window is resized.
void SetupOrthoProjection2D(int screenWidthPx, int screenHeightPx);

void ClearBlack();

// Draws one full-screen textured quad (the static wallpaper, or the
// fade-in overlay) blended with the given alpha.
void DrawFullscreenTexturedQuad(GLuint texture, int screenWidthPx, int screenHeightPx, float alpha);

// Draws every particle in one glBegin(GL_QUADS)/glEnd batch, sampling from
// `texture`. `halfSizePx` is the fixed half-width/height of every particle
// quad (要件.txt §7: 粒子サイズは固定にする). No-op when `texture` is 0 (e.g.
// no desktop capture was available for the content phase).
void DrawParticlesBatched(GLuint texture, const std::vector<DrawParticle>& particles, float halfSizePx);

} // namespace platform
