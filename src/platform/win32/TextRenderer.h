#pragma once
// Draws icon labels / window titles using classic fixed-function bitmap-font
// display lists (wglUseFontBitmaps). This keeps text rendering inside the
// glBegin/glEnd-era pipeline without pulling in a shader-based text/atlas
// system (要件.txt §2: 固定機能版を基本とする).

#include <string>

#include <windows.h>

#include "GLCompat.h"

namespace platform {

class TextRenderer {
public:
    TextRenderer() = default;
    ~TextRenderer();

    TextRenderer(const TextRenderer&) = delete;
    TextRenderer& operator=(const TextRenderer&) = delete;

    // Builds display lists for ASCII 0-255 using `hdc`'s selected font.
    // Must be called once after the GL context is current.
    bool Init(HDC hdc);

    void Shutdown();

    // Draws `text` with its baseline-left corner at (x, y) in the current
    // 2D projection (screen pixel space, y-down as set up by Renderer).
    void DrawText(float x, float y, const std::string& text) const;

    bool IsValid() const { return listBase_ != 0; }

private:
    GLuint listBase_ = 0;
    static constexpr int kGlyphCount = 224; // printable ASCII 32..255
    static constexpr int kFirstGlyph = 32;
};

} // namespace platform
