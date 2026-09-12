#include "SaverWindow.h"

#include <algorithm>
#include <string>

#include "../../core/ConfigModel.h"
#include "../../core/Logger.h"
#include "AppController.h"
#include "AppPaths.h"
#include "OpenGLContext.h"
#include "Renderer.h"
#include "StringConvert.h"
#include "WallpaperProvider.h"
#include "WinFileIO.h"

namespace platform {

namespace {

constexpr wchar_t kWindowClassName[] = L"SpiralSuctionSaverWindow";
constexpr double kTargetFrameSeconds = 1.0 / 60.0; // 要件.txt §2: 60fps目標
constexpr LONG kMouseMoveExitThresholdPx = 8;
constexpr UINT kPreviewParentWatchTimerId = 1;

enum class WindowMode { Fullscreen, Preview };

struct WindowContext {
    WindowMode mode = WindowMode::Fullscreen;
    HWND previewParent = nullptr;
    POINT initialCursorPos{};
    bool haveInitialCursorPos = false;
};

WindowContext* GetContext(HWND hwnd) {
    return reinterpret_cast<WindowContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
}

LRESULT CALLBACK SaverWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1; // we redraw the whole window every frame ourselves

        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_TIMER: {
            WindowContext* ctx = GetContext(hwnd);
            if (ctx && wParam == kPreviewParentWatchTimerId) {
                if (!ctx->previewParent || !IsWindow(ctx->previewParent)) {
                    DestroyWindow(hwnd);
                }
            }
            return 0;
        }

        default:
            break;
    }

    WindowContext* ctx = GetContext(hwnd);
    if (ctx && ctx->mode == WindowMode::Fullscreen) {
        switch (message) {
            case WM_KEYDOWN:
            case WM_SYSKEYDOWN:
            case WM_LBUTTONDOWN:
            case WM_RBUTTONDOWN:
            case WM_MBUTTONDOWN:
                DestroyWindow(hwnd);
                return 0;
            case WM_MOUSEMOVE: {
                POINT current{};
                GetCursorPos(&current);
                if (!ctx->haveInitialCursorPos) {
                    ctx->initialCursorPos = current;
                    ctx->haveInitialCursorPos = true;
                } else {
                    const LONG dx = current.x - ctx->initialCursorPos.x;
                    const LONG dy = current.y - ctx->initialCursorPos.y;
                    if (dx * dx + dy * dy > kMouseMoveExitThresholdPx * kMouseMoveExitThresholdPx) {
                        DestroyWindow(hwnd);
                        return 0;
                    }
                }
                break;
            }
            default:
                break;
        }
    }

    return DefWindowProcW(hwnd, message, wParam, lParam);
}

void EnsureWindowClassRegistered(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;

    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
    wc.lpfnWndProc = SaverWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // WM_ERASEBKGND handled manually
    wc.lpszClassName = kWindowClassName;
    RegisterClassW(&wc);
    registered = true;
}

core::ConfigModel LoadConfigOrDefault() {
    core::ConfigModel config;
    const std::wstring path = GetConfigFilePath();
    std::string text;
    if (!path.empty() && ReadTextFileW(path, text)) {
        config = core::ParseConfigIni(text);
    }
    return config;
}

std::wstring ResolveWallpaperPath(const core::ConfigModel& config) {
    if (!config.backgroundImageOverridePath.empty()) {
        return Utf8ToWide(config.backgroundImageOverridePath);
    }
    return GetSystemWallpaperPath();
}

// Shared render loop for both fullscreen and preview windows. Blocks until
// the window is destroyed (by user input in fullscreen mode, or by the
// preview parent going away).
void RunMessageLoop(HWND hwnd, OpenGLContext& gl, int width, int height) {
    core::ConfigModel config = LoadConfigOrDefault();
    std::wstring wallpaper = ResolveWallpaperPath(config);

    SetupOrthoProjection2D(width, height);

    AppController app;
    if (!app.Initialize(gl.GetHDC(), width, height, config, wallpaper)) {
        core::Logger::Error("SaverWindow: AppController::Initialize failed");
        return;
    }

    LARGE_INTEGER freq{};
    LARGE_INTEGER prevTime{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&prevTime);

    MSG msg{};
    bool running = true;
    while (running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                running = false;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!running || !IsWindow(hwnd)) {
            break;
        }

        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        double dt = freq.QuadPart > 0
                        ? static_cast<double>(now.QuadPart - prevTime.QuadPart) / freq.QuadPart
                        : kTargetFrameSeconds;
        prevTime = now;
        if (dt > 0.25) dt = 0.25; // clamp huge gaps (e.g. after being stalled)

        app.Update(static_cast<float>(dt));
        app.Draw();
        gl.SwapBuffers();

        LARGE_INTEGER afterRender{};
        QueryPerformanceCounter(&afterRender);
        const double elapsed = freq.QuadPart > 0
                                    ? static_cast<double>(afterRender.QuadPart - now.QuadPart) / freq.QuadPart
                                    : 0.0;
        const double remaining = kTargetFrameSeconds - elapsed;
        if (remaining > 0.0) {
            Sleep(static_cast<DWORD>(remaining * 1000.0));
        }
    }

    app.Shutdown();
}

} // namespace

void RunFullScreenSaver(HINSTANCE instance) {
    EnsureWindowClassRegistered(instance);

    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);

    WindowContext ctx;
    ctx.mode = WindowMode::Fullscreen;

    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, kWindowClassName, L"Spiral Suction Saver",
                                 WS_POPUP | WS_VISIBLE, 0, 0, width, height, nullptr, nullptr,
                                 instance, nullptr);
    if (!hwnd) {
        core::Logger::Error("RunFullScreenSaver: CreateWindowExW failed");
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));

    ShowCursor(FALSE);
    SetForegroundWindow(hwnd);
    SetFocus(hwnd);

    OpenGLContext gl;
    if (gl.Create(hwnd)) {
        RunMessageLoop(hwnd, gl, width, height);
        gl.Destroy();
    } else {
        core::Logger::Error("RunFullScreenSaver: OpenGL context creation failed");
        MessageBoxW(hwnd, L"Failed to initialize OpenGL. The screensaver cannot run.",
                    L"Spiral Suction Saver", MB_OK | MB_ICONERROR);
    }

    ShowCursor(TRUE);
    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}

void RunPreview(HINSTANCE instance, HWND previewParent) {
    if (!previewParent || !IsWindow(previewParent)) {
        return;
    }
    EnsureWindowClassRegistered(instance);

    RECT clientRect{};
    GetClientRect(previewParent, &clientRect);
    const int width = std::max<int>(1, clientRect.right - clientRect.left);
    const int height = std::max<int>(1, clientRect.bottom - clientRect.top);

    WindowContext ctx;
    ctx.mode = WindowMode::Preview;
    ctx.previewParent = previewParent;

    HWND hwnd = CreateWindowExW(0, kWindowClassName, L"", WS_CHILD | WS_VISIBLE, 0, 0, width, height,
                                 previewParent, nullptr, instance, nullptr);
    if (!hwnd) {
        core::Logger::Error("RunPreview: CreateWindowExW failed");
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    SetTimer(hwnd, kPreviewParentWatchTimerId, 250, nullptr);

    OpenGLContext gl;
    if (gl.Create(hwnd)) {
        RunMessageLoop(hwnd, gl, width, height);
        gl.Destroy();
    }

    if (IsWindow(hwnd)) {
        KillTimer(hwnd, kPreviewParentWatchTimerId);
        DestroyWindow(hwnd);
    }
}

} // namespace platform
