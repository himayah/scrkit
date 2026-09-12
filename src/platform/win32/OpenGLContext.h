#pragma once
// WGL context setup with double buffering (要件.txt §2, Step 2).

#include <string>

#include <windows.h>

namespace platform {

class OpenGLContext {
public:
    OpenGLContext() = default;
    ~OpenGLContext();

    OpenGLContext(const OpenGLContext&) = delete;
    OpenGLContext& operator=(const OpenGLContext&) = delete;

    // Sets a double-buffered pixel format on hwnd's DC and creates + makes
    // current a WGL rendering context. Returns false (and logs) on failure.
    bool Create(HWND hwnd);

    void SwapBuffers() const;
    void Destroy();

    bool IsValid() const { return hglrc_ != nullptr; }
    HDC GetHDC() const { return hdc_; }

private:
    HWND hwnd_ = nullptr;
    HDC hdc_ = nullptr;
    HGLRC hglrc_ = nullptr;
};

// Queries glGetString(GL_VENDOR/RENDERER/VERSION) on the currently-current
// GL context and returns a suggested particle count via
// core::ClassifyGpuParticleCount (要件.txt §6: 自動判定).
int ResolveAutoParticleCountFromCurrentContext();

} // namespace platform
