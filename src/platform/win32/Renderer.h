#pragma once
// Batched fixed-function OpenGL drawing helpers (要件.txt §7: 固定機能版での
// 最適化 -- glBegin/glEnd を種別ごとに1回だけ呼ぶ、色計算はCPUで行わない).
// This module knows nothing about the state machine or spiral math; it only
// draws the vertex data AppController hands it each frame.

#include <vector>

#include "../../core/effects/DrawList.h"
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

// Draws one full-screen solid black quad blended with the given alpha (the
// STATE_FADEOUT overlay: drawn on top of whatever the effect engine just
// rendered this frame, ramping 0->1 to fade the whole screen to black
// without disturbing either layer's own animation underneath).
void DrawFullscreenBlackOverlay(int screenWidthPx, int screenHeightPx, float alpha);

// A flat, alpha-blended rectangle in screen pixels (debug overlays).
void DrawColoredRect(float x, float y, float w, float h, float r, float g, float b, float a);

// Draws every particle in one glBegin(GL_QUADS)/glEnd batch, sampling from
// `texture`. `halfWidthPx`/`halfHeightPx` are the fixed half-width/height of
// every particle quad (要件.txt §7: 粒子サイズは固定にする) -- kept separate
// per axis, not a single square size, so a quad matches its source grid
// cell's own (generally non-square, screen-aspect-ratio) shape instead of
// stretching whatever it samples into a square. No-op when `texture` is 0
// (e.g. no desktop capture was available for the content phase).
void DrawParticlesBatched(GLuint texture, const std::vector<DrawParticle>& particles, float halfWidthPx,
                           float halfHeightPx);

// Resolves a core::fx::TextureRole(+index) to the actual GL texture
// ExecuteDrawList should bind (§7.2). Returns 0 (silently skipped) for a
// HueRing index that isn't ready yet or is out of range.
struct EffectTextureTable {
    GLuint background = 0;
    GLuint foreground = 0;
    std::vector<GLuint> hueRings; // index-addressed; a 0 entry means "not generated yet"

    GLuint Resolve(core::fx::TextureRole role, int index) const;
};

// Executes one core::fx::FrameDrawList (§4.6/§7.2): one glPushMatrix/
// transform/glColorMask/glBlendFunc/glBegin(GL_QUADS)...glEnd/glPopMatrix
// per batch, in list order (core already puts background batches before
// foreground ones). A batch whose texture isn't ready (Resolve returns 0)
// is skipped rather than drawn untextured.
void ExecuteDrawList(const core::fx::FrameDrawList& list, const EffectTextureTable& textures);

} // namespace platform
