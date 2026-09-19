#include "Renderer.h"

#include <cmath>

namespace platform {

namespace {

constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;

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

void DrawFullscreenBlackOverlay(int screenWidthPx, int screenHeightPx, float alpha) {
    if (alpha <= 0.0f) return;
    glColor4f(0.0f, 0.0f, 0.0f, alpha);
    glBegin(GL_QUADS);
    glVertex2f(0.0f, 0.0f);
    glVertex2f(static_cast<float>(screenWidthPx), 0.0f);
    glVertex2f(static_cast<float>(screenWidthPx), static_cast<float>(screenHeightPx));
    glVertex2f(0.0f, static_cast<float>(screenHeightPx));
    glEnd();
}

void DrawColoredRect(float x, float y, float w, float h, float r, float g, float b, float a) {
    glDisable(GL_TEXTURE_2D);
    glColor4f(r, g, b, a);
    glBegin(GL_QUADS);
    glVertex2f(x, y);
    glVertex2f(x + w, y);
    glVertex2f(x + w, y + h);
    glVertex2f(x, y + h);
    glEnd();
}

void DrawParticlesBatched(GLuint texture, const std::vector<DrawParticle>& particles, float halfWidthPx,
                           float halfHeightPx) {
    if (particles.empty() || texture == 0) return;

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, texture);
    glColor4f(1.0f, 1.0f, 1.0f, 1.0f);

    glBegin(GL_QUADS); // single batch for all particles, however many (要件.txt §7)
    for (const auto& p : particles) {
        EmitTexturedQuad(p.x - halfWidthPx, p.y - halfHeightPx, halfWidthPx * 2.0f, halfHeightPx * 2.0f,
                          p.u0, p.v0, p.u1, p.v1);
    }
    glEnd();

    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

GLuint EffectTextureTable::Resolve(core::fx::TextureRole role, int index) const {
    switch (role) {
        case core::fx::TextureRole::Background:
            return background;
        case core::fx::TextureRole::Foreground:
            return foreground;
        case core::fx::TextureRole::HueRing:
            // §6.2.8: ring[0] *is* the background texture itself (0deg
            // rotation, never separately generated) -- hueRings[] holds only
            // the K-1 generated rotations, at index-1.
            if (index == 0) return background;
            return (index >= 1 && static_cast<size_t>(index - 1) < hueRings.size())
                       ? hueRings[static_cast<size_t>(index - 1)]
                       : 0;
    }
    return 0;
}

namespace {

inline void EmitVertex(const core::fx::QuadVertex& v, float batchAlpha) {
    glColor4f(v.shade, v.shade, v.shade, v.alpha * batchAlpha);
    glTexCoord2f(v.uv.x, v.uv.y);
    glVertex2f(v.pos.x, v.pos.y);
}

inline void EmitMeshVertex(const core::fx::MeshVertex& v, float batchAlpha) {
    glColor4f(v.shade, v.shade, v.shade, v.alpha * batchAlpha);
    glTexCoord2f(v.uv.x, v.uv.y);
    glVertex2f(v.pos.x, v.pos.y);
}

} // namespace

void ExecuteDrawList(const core::fx::FrameDrawList& list, const EffectTextureTable& textures) {
    glEnable(GL_TEXTURE_2D);

    for (const auto& batch : list.batches) {
        const GLuint tex = textures.Resolve(batch.texture, batch.textureIndex);
        if (tex == 0) continue; // not ready yet (e.g. a HueRing still generating) -- skip silently

        glBindTexture(GL_TEXTURE_2D, tex);
        const GLenum dstFactor = batch.blend == core::fx::BlendMode::Additive ? GL_ONE : GL_ONE_MINUS_SRC_ALPHA;
        glBlendFunc(GL_SRC_ALPHA, dstFactor);
        glColorMask(batch.colorMask.r, batch.colorMask.g, batch.colorMask.b, GL_TRUE);

        const auto& t = batch.transform;
        glPushMatrix();
        glTranslatef(t.pivot.x + t.translate.x, t.pivot.y + t.translate.y, 0.0f);
        glRotatef(t.rotateRad * kRadToDeg, 0.0f, 0.0f, 1.0f);
        glScalef(t.scale, t.scale, 1.0f);
        glTranslatef(-t.pivot.x, -t.pivot.y, 0.0f);

        glBegin(GL_QUADS);
        if (batch.mesh) {
            const core::fx::Mesh& mesh = *batch.mesh;
            for (size_t q = 0; q < mesh.quads.size(); ++q) {
                if (q < mesh.quadActive.size() && !mesh.quadActive[q]) continue;
                for (int idx : mesh.quads[q]) {
                    EmitMeshVertex(mesh.vertices[static_cast<size_t>(idx)], batch.alpha);
                }
            }
        } else if (batch.quads) {
            for (const auto& v : *batch.quads) {
                EmitVertex(v, batch.alpha);
            }
        }
        glEnd();

        glPopMatrix();
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindTexture(GL_TEXTURE_2D, 0);
    glDisable(GL_TEXTURE_2D);
}

} // namespace platform
