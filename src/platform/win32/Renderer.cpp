#include "Renderer.h"

namespace platform {

namespace {

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

} // namespace platform
