#pragma once
// <GL/gl.h> on Windows (MSVC's Windows Kit in particular) depends on types
// and macros (HDC, HGLRC, WINGDIAPI, APIENTRY, ...) declared by <windows.h>
// and does not define fallbacks itself, so <windows.h> must be included
// first in every translation unit that uses GL -- this header is the single
// choke point all GL-using files go through, so it takes care of that order
// itself rather than relying on every includer to get it right.
//
// NOMINMAX avoids the classic conflict where windows.h's own `max`/`min`
// function-like macros silently mangle any `std::max`/`std::min` call
// elsewhere in the file (MSVC reports this as a confusing
// "illegal token on right side of '::'" syntax error).
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// The Windows SDK's <GL/gl.h> only declares OpenGL 1.1 constants (Microsoft
// never updated it). GL_CLAMP_TO_EDGE was promoted to core in OpenGL 1.2 but
// is supported by effectively every driver still in use, so it's safe to
// define its known constant value manually rather than pull in an external
// GL loader just for one enum (要件.txt: 固定機能版が基本、軽量さ優先).
#include <GL/gl.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
