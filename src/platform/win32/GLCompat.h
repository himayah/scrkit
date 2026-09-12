#pragma once
// The Windows SDK's <GL/gl.h> only declares OpenGL 1.1 constants (Microsoft
// never updated it). GL_CLAMP_TO_EDGE was promoted to core in OpenGL 1.2 but
// is supported by effectively every driver still in use, so it's safe to
// define its known constant value manually rather than pull in an external
// GL loader just for one enum (要件.txt: 固定機能版が基本、軽量さ優先).

#include <GL/gl.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
