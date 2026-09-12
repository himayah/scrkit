#include "Renderer.h"

namespace platform {

namespace {
// Fixed colors per 要件.txt §7 (色計算はCPUで行わない -- these are compile-time
// constants, never computed per-frame/per-object).
constexpr float kIconColor[3] = {0.30f, 0.55f, 0.85f};
constexpr float kWindowBodyColor[4] = {0.90f, 0.90f, 0.90f, 0.92f};
constexpr float kWindowTitleBarColor[3] = {0.12f, 0.30f, 0.55f};
constexpr float kLabelColor[3] = {0.05f, 0.05f, 0.05f};

inline void EmitQuad(float x, float y, float w, float h) {
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
}

inline void EmitTexturedQuad(float x, float y, float w, float h, float u0, float v0, float u1,
                              float v1) {
    glTexCoord2f(u0, v0);
    glVertex2f(x, y);
    glTexCoord2f(u1, v0);
    glVertex2f(x + w, y);
    glTexCoord2f(u1, v1);
    glVertex2f(x + w, y + h);
    glTexCoord2f(u0, v1);
    glVertex2f(x, y + h);
}
} // namespace

void SetupOrthoProjection2D(int screenWidthPx, int screenHeightPx) {
    glViewport(0, 0, screenWidthPx, screenHeightPx);

    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    // top=0, bottom=height so screen-space (0,0) is the top-left corner.
    glOrtho(0.0, screenWidthPx, screenHeightPx, 0.0, -1.0, 1.0);

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
}

void ClearBlack() {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
}

void DrawFullscreenTexturedQuad(GLuint texture, int screenWidthPx, int screenHeightPx, float alpha) {
    if (texture == 0) return;
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor4f(1.0f, 1.0f, 1.0f, alpha);
    glBegin(GL_QUADS);
    EmitTexturedQuad(0.0f, 0.0f, static_cast<float>(screenWidthPx), static_cast<float>(screenHeightPx),
                      0.0f, 0.0f, 1.0f, 1.0f);
    glEnd();
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

namespace {
// Draws one batch of rectangles either textured (sampling `captureTexture`
// at each rect's own UV) or as a single flat fixed color, depending on
// whether a capture texture is available. Shared by icon and window body
// drawing so both get the same fallback behavior.
void DrawCapturedOrSolidBatch(GLuint captureTexture, const std::vector<DrawCapturedRect>& rects,
                               const float solidColor[4]) {
    if (rects.empty()) return;

    if (captureTexture != 0) {
        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, captureTexture);
        glColor4f(1.0f, 1.0f, 1.0f, solidColor[3]);
        glBegin(GL_QUADS);
        for (const auto& item : rects) {
            EmitTexturedQuad(item.rect.x, item.rect.y, item.rect.w, item.rect.h, item.u0, item.v0,
                              item.u1, item.v1);
        }
        glEnd();
        glBindTexture(GL_TEXTURE_2D, 0);
        glDisable(GL_TEXTURE_2D);
    } else {
        glDisable(GL_TEXTURE_2D);
        glColor4f(solidColor[0], solidColor[1], solidColor[2], solidColor[3]);
        glBegin(GL_QUADS); // one glBegin/glEnd for every rect (要件.txt §7)
        for (const auto& item : rects) {
            EmitQuad(item.rect.x, item.rect.y, item.rect.w, item.rect.h);
        }
        glEnd();
    }
}
} // namespace

void DrawIconBodiesBatched(GLuint captureTexture, const std::vector<DrawCapturedRect>& icons) {
    const float color[4] = {kIconColor[0], kIconColor[1], kIconColor[2], 1.0f};
    DrawCapturedOrSolidBatch(captureTexture, icons, color);
}

void DrawWindowBodiesBatched(GLuint captureTexture, const std::vector<DrawCapturedRect>& clientAreas,
                              const std::vector<DrawCapturedRect>& titleBars) {
    const float bodyColor[4] = {kWindowBodyColor[0], kWindowBodyColor[1], kWindowBodyColor[2],
                                 kWindowBodyColor[3]};
    const float titleColor[4] = {kWindowTitleBarColor[0], kWindowTitleBarColor[1], kWindowTitleBarColor[2],
                                  1.0f};
    DrawCapturedOrSolidBatch(captureTexture, clientAreas, bodyColor);
    DrawCapturedOrSolidBatch(captureTexture, titleBars, titleColor);
}

void DrawParticlesBatched(GLuint texture, const std::vector<DrawParticle>& particles, float halfSizePx) {
    if (particles.empty() || texture == 0) return;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS); // single batch for all particles, however many (要件.txt §7)
    for (const auto& p : particles) {
        EmitTexturedQuad(p.x - halfSizePx, p.y - halfSizePx, halfSizePx * 2.0f, halfSizePx * 2.0f,
                          p.u0, p.v0, p.u1, p.v1);
    }
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

void DrawLabels(const TextRenderer& textRenderer, const std::vector<DrawLabeledRect>& items,
                float offsetX, float offsetY) {
    if (!textRenderer.IsValid() || items.empty()) return;
    glDisable(GL_TEXTURE_2D);
    glColor4f(kLabelColor[0], kLabelColor[1], kLabelColor[2], 1.0f);
    for (const auto& item : items) {
        textRenderer.DrawText(item.rect.x + offsetX, item.rect.y + offsetY, item.label);
    }
}

} // namespace platform
