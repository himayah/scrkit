#include "TextRenderer.h"

#include "../../core/Logger.h"

namespace platform {

bool TextRenderer::Init(HDC hdc) {
    HFONT font = CreateFontA(
        -14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_TT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, FF_DONTCARE, "Segoe UI");
    if (!font) {
        core::Logger::Warn("TextRenderer: CreateFontA failed, labels will be skipped");
        return false;
    }

    HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, font));

    listBase_ = glGenLists(kGlyphCount);
    const BOOL ok = wglUseFontBitmapsA(hdc, kFirstGlyph, kGlyphCount, listBase_);

    SelectObject(hdc, oldFont);
    DeleteObject(font);

    if (!ok) {
        core::Logger::Warn("TextRenderer: wglUseFontBitmapsA failed, labels will be skipped");
        glDeleteLists(listBase_, kGlyphCount);
        listBase_ = 0;
        return false;
    }
    return true;
}

void TextRenderer::Shutdown() {
    if (listBase_ != 0) {
        glDeleteLists(listBase_, kGlyphCount);
        listBase_ = 0;
    }
}

TextRenderer::~TextRenderer() { Shutdown(); }

void TextRenderer::DrawText(float x, float y, const std::string& text) const {
    if (!IsValid() || text.empty()) {
        return;
    }
    glRasterPos2f(x, y);
    glListBase(static_cast<GLuint>(listBase_ - kFirstGlyph));
    glCallLists(static_cast<GLsizei>(text.size()), GL_UNSIGNED_BYTE, text.c_str());
}

} // namespace platform
