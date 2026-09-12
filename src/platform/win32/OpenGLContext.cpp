#include "OpenGLContext.h"

#include <GL/gl.h>

#include "../../core/GpuTierClassifier.h"
#include "../../core/Logger.h"

namespace platform {

bool OpenGLContext::Create(HWND hwnd) {
    hwnd_ = hwnd;
    hdc_ = GetDC(hwnd_);
    if (!hdc_) {
        core::Logger::Error("OpenGLContext: GetDC failed");
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd{};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER; // 要件2: ダブルバッファリング
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 0; // pure 2D overlay rendering; no depth buffer needed
    pfd.cAlphaBits = 8;
    pfd.iLayerType = PFD_MAIN_PLANE;

    const int format = ChoosePixelFormat(hdc_, &pfd);
    if (format == 0 || !SetPixelFormat(hdc_, format, &pfd)) {
        core::Logger::Error("OpenGLContext: SetPixelFormat failed");
        Destroy();
        return false;
    }

    hglrc_ = wglCreateContext(hdc_);
    if (!hglrc_) {
        core::Logger::Error("OpenGLContext: wglCreateContext failed");
        Destroy();
        return false;
    }

    if (!wglMakeCurrent(hdc_, hglrc_)) {
        core::Logger::Error("OpenGLContext: wglMakeCurrent failed");
        Destroy();
        return false;
    }

    return true;
}

void OpenGLContext::SwapBuffers() const {
    if (hdc_) {
        ::SwapBuffers(hdc_);
    }
}

void OpenGLContext::Destroy() {
    if (hglrc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(hglrc_);
        hglrc_ = nullptr;
    }
    if (hdc_ && hwnd_) {
        ReleaseDC(hwnd_, hdc_);
    }
    hdc_ = nullptr;
    hwnd_ = nullptr;
}

OpenGLContext::~OpenGLContext() { Destroy(); }

int ResolveAutoParticleCountFromCurrentContext() {
    const GLubyte* vendor = glGetString(GL_VENDOR);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    const GLubyte* version = glGetString(GL_VERSION);

    const std::string v = vendor ? reinterpret_cast<const char*>(vendor) : "";
    const std::string r = renderer ? reinterpret_cast<const char*>(renderer) : "";
    const std::string ver = version ? reinterpret_cast<const char*>(version) : "";

    return core::ClassifyGpuParticleCount(v, r, ver);
}

} // namespace platform
