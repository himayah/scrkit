#include "SaverWindow.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../../core/ConfigModel.h"
#include "../../core/Logger.h"
#include "AppController.h"
#include "AppPaths.h"
#include "OpenGLContext.h"
#include "Renderer.h"
#include "ScrApiHost.h"
#include "ScreenCapture.h"
#include "StringConvert.h"
#include "WallpaperProvider.h"
#include "WinFileIO.h"

namespace platform {

namespace {

constexpr wchar_t kWindowClassName[] = L"ScrKitWindow";
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
// preview parent going away). `desktopCapture`, when non-null, must have
// been captured against exactly this same width x height (only meaningful
// for the real fullscreen size -- callers never pass it for the scaled-down
// preview).
// Fits the logical `width` x `height` picture into the window's client area, keeping its
// aspect ratio (letterboxed with the black the frame is cleared to). Used only for the
// SCRAPI preview, where the viewer may give the window any size.
void ApplyLetterboxedViewport(HWND hwnd, int logicalW, int logicalH, RECT& lastClient) {
    RECT client{};
    GetClientRect(hwnd, &client);
    if (client.right == lastClient.right && client.bottom == lastClient.bottom) return;
    lastClient = client;
    const int winW = std::max<int>(1, client.right - client.left);
    const int winH = std::max<int>(1, client.bottom - client.top);
    const double scale = std::min(static_cast<double>(winW) / logicalW, static_cast<double>(winH) / logicalH);
    const int vpW = std::max(1, static_cast<int>(logicalW * scale));
    const int vpH = std::max(1, static_cast<int>(logicalH * scale));
    glViewport((winW - vpW) / 2, (winH - vpH) / 2, vpW, vpH);
}

void RunMessageLoop(HWND hwnd, OpenGLContext& gl, int width, int height,
                    const DecodedImage* desktopCapture = nullptr, bool isPreviewMode = false,
                    const std::wstring& scrapiPipeName = std::wstring()) {
    SetupOrthoProjection2D(width, height);

    // Show the just-captured real desktop immediately, before doing any of
    // the slower work below (wallpaper decode, content diff, texture
    // uploads) -- otherwise the window sits there undrawn (black) for
    // however long that takes (user feedback: ~2s of black screen at
    // startup). This is just a plain full-screen quad of the capture
    // itself, so it looks identical to the real desktop it's covering;
    // AppController::Initialize below reuses the very same capture for the
    // real content-phase textures, so the transition into the actual
    // animation is seamless once it's ready.
    GLuint previewTexture = desktopCapture ? CreateTextureFromImage(*desktopCapture) : 0;
    if (previewTexture != 0) {
        DrawFullscreenTexturedQuad(previewTexture, width, height, 1.0f);
        gl.SwapBuffers();
    }

    core::ConfigModel config = LoadConfigOrDefault();
    std::wstring wallpaper = ResolveWallpaperPath(config);

    AppController app;
    const bool initialized =
        app.Initialize(gl.GetHDC(), width, height, config, wallpaper, desktopCapture, isPreviewMode);
    if (previewTexture != 0) {
        glDeleteTextures(1, &previewTexture);
    }
    if (!initialized) {
        core::Logger::Error("SaverWindow: AppController::Initialize failed");
        return;
    }

    // SCRAPI preview: hand the frame loop to the viewer's controls. The normal blackout cycle
    // is held off so an effect pinned for inspection isn't interrupted; the phase machine
    // simply stays in STATE_CONTENT.
    std::unique_ptr<ScrApiHost> scrapiHost;
    RECT lastClient{};
    if (!scrapiPipeName.empty()) {
        app.SetAutoCycle(false);
        scrapiHost = std::make_unique<ScrApiHost>(app, config, hwnd, scrapiPipeName);
        core::Logger::Info("SaverWindow: SCRAPI session requested by the viewer");
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

        float simDt = static_cast<float>(dt);
        if (scrapiHost) {
            simDt = scrapiHost->BeginFrame(simDt);
            ApplyLetterboxedViewport(hwnd, width, height, lastClient);
        }
        app.Update(simDt);
        app.Draw();
        if (scrapiHost) scrapiHost->EndFrame();
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

    scrapiHost.reset(); // before the app it holds a reference into
    app.Shutdown();
}

} // namespace

void RunFullScreenSaver(HINSTANCE instance) {
    EnsureWindowClassRegistered(instance);

    const int width = GetSystemMetrics(SM_CXSCREEN);
    const int height = GetSystemMetrics(SM_CYSCREEN);

    // Capture the real desktop (wallpaper + whatever icons/windows are
    // really showing right now) *before* our own window covers the screen.
    // AppController diffs this against the wallpaper to find the "content"
    // to suck away (see core::ContentMask) -- replaces the earlier approach
    // of querying/capturing individual windows and icons, which did not
    // hold up in practice (user feedback). A capture failure is non-fatal --
    // AppController just skips the content phase when passed nullptr.
    DecodedImage desktopCapture;
    const bool haveCapture = CaptureScreenToImage(width, height, desktopCapture);
    if (!haveCapture) {
        core::Logger::Warn("RunFullScreenSaver: desktop capture failed; content phase will be skipped");
    }

    WindowContext ctx;
    ctx.mode = WindowMode::Fullscreen;

    // The window is created 1px taller than the display, not exactly
    // matching it. A window whose bounds exactly match the monitor can
    // trigger an OS/GPU-driver fullscreen-detection heuristic that switches
    // to a different presentation/flip path -- confirmed on real hardware
    // to cause a ~2s black screen at startup (every step measured
    // in-process consistently completed in under 200ms, so the delay was
    // entirely in that OS-level transition, not our own code). The extra
    // row falls off the bottom edge of the monitor and is never drawn to
    // (the GL viewport below still uses the real width x height), so this
    // has no visible effect beyond avoiding that transition.
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, kWindowClassName, L"ScrKit",
                                 WS_POPUP | WS_VISIBLE, 0, 0, width, height + 1, nullptr, nullptr,
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
        RunMessageLoop(hwnd, gl, width, height, haveCapture ? &desktopCapture : nullptr, /*isPreviewMode=*/false);
        gl.Destroy();
    } else {
        core::Logger::Error("RunFullScreenSaver: OpenGL context creation failed");
        MessageBoxW(hwnd, L"Failed to initialize OpenGL. The screensaver cannot run.",
                    L"ScrKit", MB_OK | MB_ICONERROR);
    }

    ShowCursor(TRUE);
    if (IsWindow(hwnd)) {
        DestroyWindow(hwnd);
    }
}

void RunPreview(HINSTANCE instance, HWND previewParent, const std::wstring& scrapiPipeName) {
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

    // An OpenGL window should clip against its siblings/children, or the driver may paint
    // over neighboring windows; that matters once the viewer puts other controls beside it.
    const DWORD clipStyle = scrapiPipeName.empty() ? 0 : (WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
    HWND hwnd = CreateWindowExW(0, kWindowClassName, L"", WS_CHILD | WS_VISIBLE | clipStyle, 0, 0, width, height,
                                 previewParent, nullptr, instance, nullptr);
    if (!hwnd) {
        core::Logger::Error("RunPreview: CreateWindowExW failed");
        return;
    }
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&ctx));
    SetTimer(hwnd, kPreviewParentWatchTimerId, 250, nullptr);

    // SCRAPI: render at a fixed logical resolution (the primary monitor's) regardless of
    // the window size, so the viewer can resize the window freely without the effects'
    // pixel-based math or the grid changing; the viewport scales the picture to fit.
    const bool scrapi = !scrapiPipeName.empty();
    const int logicalW = scrapi ? GetSystemMetrics(SM_CXSCREEN) : width;
    const int logicalH = scrapi ? GetSystemMetrics(SM_CYSCREEN) : height;

    OpenGLContext gl;
    if (gl.Create(hwnd)) {
        RunMessageLoop(hwnd, gl, logicalW, logicalH, /*desktopCapture=*/nullptr, /*isPreviewMode=*/true,
                       scrapiPipeName);
        gl.Destroy();
    }

    if (IsWindow(hwnd)) {
        KillTimer(hwnd, kPreviewParentWatchTimerId);
        DestroyWindow(hwnd);
    }
}

} // namespace platform
