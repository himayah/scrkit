#pragma once
// Decodes BMP/JPEG/PNG/GIF images via the Windows Imaging Component (WIC),
// which ships with Windows -- so image loading needs no third-party
// library (要件.txt §3-3: AI は壁紙取得/読込方法を最適に判断してよい).

#include <cstdint>
#include <string>
#include <vector>

#include <windows.h>

#include "../../core/ContentMask.h"
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

// docs/DESIGN_EFFECTS.md §7.4/§3.2: builds the foreground layer's RGBA
// texture by copying `image` (the real desktop capture) and zeroing the
// alpha of every pixel in a grid cell `mask` doesn't flag as differing from
// the wallpaper. `mask` is gridN*gridN, row-major -- the same layout
// core::ComputeContentMask/core::BuildParticleGrid use, so a cell's index in
// `mask` matches its core::Particle's position in the grid. Cell boundaries
// use the same "(index*count)/total" formula as core::ComputeContentMask
// (NOT a fixed image.width/gridN cell width, which drifts from the mask's
// actual boundaries whenever width isn't an exact multiple of gridN -- see
// the .cpp), so the alpha edge lines up with each Particle's own u0..v1
// rect (§7.4).
//
// `refinement`, when non-null, is consulted for any pixel that falls inside
// a cell core::RefineBoundaryMask actually subdivided: that cell's alpha
// then traces the finer per-leaf shape instead of the flat whole-cell fill,
// following a boundary cell's real pixel-level edge instead of a blocky
// one. Every other cell (not present in `refinement`) keeps today's flat
// behavior unchanged. This is the only place the finer boundary resolution
// is ever consulted -- the resulting texture is a single flat RGBA image
// from here on; nothing downstream (the effect system's gridN x gridN
// block grid) is aware refinement happened at all.
//
// Returns 0 on failure.
GLuint CreateMaskedTextureFromImage(const DecodedImage& image, const std::vector<bool>& mask, int gridN,
                                     const core::BoundaryRefinement* refinement = nullptr);

// Uploads a raw RGBA8 buffer directly (§6.2.8's HueRing textures). Same GL
// texture parameters as CreateTextureFromImage. Returns 0 on failure.
GLuint CreateTextureFromRgba(int width, int height, const uint8_t* rgba);

} // namespace platform
