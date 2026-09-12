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

void DrawIconBodiesBatched(const std::vector<DrawRect>& icons) {
    if (icons.empty()) return;
    glDisable(GL_TEXTURE_2D);
    glColor4f(kIconColor[0], kIconColor[1], kIconColor[2], 1.0f);
    glBegin(GL_QUADS); // one glBegin/glEnd for every icon (要件.txt §7)
    for (const auto& r : icons) {
        EmitQuad(r.x, r.y, r.w, r.h);
    }
    glEnd();
}

void DrawWindowBodiesBatched(const std::vector<DrawRect>& clientAreas,
                              const std::vector<DrawRect>& titleBars) {
    glDisable(GL_TEXTURE_2D);

    if (!clientAreas.empty()) {
        glColor4f(kWindowBodyColor[0], kWindowBodyColor[1], kWindowBodyColor[2], kWindowBodyColor[3]);
        glBegin(GL_QUADS);
        for (const auto& r : clientAreas) {
            EmitQuad(r.x, r.y, r.w, r.h);
        }
        glEnd();
    }

    if (!titleBars.empty()) {
        glColor4f(kWindowTitleBarColor[0], kWindowTitleBarColor[1], kWindowTitleBarColor[2], 1.0f);
        glBegin(GL_QUADS);
        for (const auto& r : titleBars) {
            EmitQuad(r.x, r.y, r.w, r.h);
        }
        glEnd();
    }
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
