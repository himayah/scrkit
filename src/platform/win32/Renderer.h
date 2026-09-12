#pragma once
// Batched fixed-function OpenGL drawing helpers (要件.txt §7: 固定機能版での
// 最適化 -- glBegin/glEnd を種別ごとに1回だけ呼ぶ、色計算はCPUで行わない).
// This module knows nothing about the state machine or spiral math; it only
// draws the vertex data AppController hands it each frame.

#include <string>
#include <vector>

#include "GLCompat.h"
#include "TextRenderer.h"

namespace platform {

// A rectangle to draw as-is (icon body, or a window's client area / title
// bar). `x,y` is the top-left corner in screen pixel space.
struct DrawRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// A rectangle plus the text label to draw at its top-left corner.
struct DrawLabeledRect {
    DrawRect rect;
    std::string label;
};

// A rectangle to draw, plus the UV coordinates it should sample from a
// "captured desktop" texture (i.e. what was really at this screen location
// when the saver started) instead of a flat placeholder color. u0v0/u1v1
// are ignored when the draw call is given a texture of 0.
struct DrawCapturedRect {
    DrawRect rect;
    float u0 = 0.0f;
    float v0 = 0.0f;
    float u1 = 0.0f;
    float v1 = 0.0f;
};

// A single particle: current center position + the UV cell it samples from
// the background texture. Particle size is fixed (passed once, not per
// particle) per 要件.txt §7.
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

// Draws every icon body in a single glBegin(GL_QUADS)/glEnd batch. When
// `captureTexture` is non-zero, each icon is textured with its captured
// desktop clipping (user feedback: plain color boxes looked too bare);
// when it is 0 (e.g. no capture available, or preview mode), falls back to
// one fixed solid color (要件.txt §7: 固定色) as before.
void DrawIconBodiesBatched(GLuint captureTexture, const std::vector<DrawCapturedRect>& icons);

// Draws every window's client area, then every window's title bar, each as
// its own single batch. Same captureTexture fallback rule as above.
void DrawWindowBodiesBatched(GLuint captureTexture, const std::vector<DrawCapturedRect>& clientAreas,
                              const std::vector<DrawCapturedRect>& titleBars);

// Draws every particle in one glBegin(GL_QUADS)/glEnd batch, sampling from
// `texture`. `halfSizePx` is the fixed half-width/height of every particle
// quad (要件.txt §7: 粒子サイズは固定にする).
void DrawParticlesBatched(GLuint texture, const std::vector<DrawParticle>& particles, float halfSizePx);

// Draws text labels for a set of rectangles (icon labels / window titles).
// Not batched -- there are at most a few dozen of these per frame, far below
// the particle counts §7 is concerned about.
void DrawLabels(const TextRenderer& textRenderer, const std::vector<DrawLabeledRect>& items,
                 float offsetX, float offsetY);

} // namespace platform
