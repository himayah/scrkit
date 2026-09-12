#pragma once
// Decodes BMP/JPEG/PNG/GIF images via the Windows Imaging Component (WIC),
// which ships with Windows -- so image loading needs no third-party
// library (要件.txt §3-3: AI は壁紙取得/読込方法を最適に判断してよい).

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "GLCompat.h"

namespace platform {

struct DecodedImage {
    std::vector<uint8_t> rgba; // width*height*4 bytes, top-down
    int width = 0;
    int height = 0;
};

// Attempts to decode the image at `path`. Returns false on any failure
// (missing file, unsupported format, decoder error) -- callers should fall
// back to a procedural placeholder rather than treat this as fatal
// (design doc: エラーハンドリング方針).
bool DecodeImageFile(const std::wstring& path, DecodedImage& out);

// Builds a flat-color placeholder image, used when no wallpaper/override
// image could be loaded.
DecodedImage MakeFallbackImage(uint8_t r, uint8_t g, uint8_t b);

// Uploads a DecodedImage to a new OpenGL texture (GL_RGBA / GL_UNSIGNED_BYTE,
// linear filtering, clamp-to-edge). Returns 0 on failure.
GLuint CreateTextureFromImage(const DecodedImage& image);

} // namespace platform
