// ScrApiDemo.scr: a deliberately trivial, self-contained reference screensaver whose only real
// purpose is to exercise every SCRAPI v1 control pattern -- including the ones ScrKit.scr itself
// never happens to use (string/color/path control types, scrapi.fps/scrapi.saveToConfig,
// apply:"restart", and the in/all/any/not condition kinds) -- through a real process, a real named
// pipe and a real embedded preview, not just unit tests. See docs/SCRAPI_V2_DRAFT.md for the survey
// that found these gaps, and src/core/DemoControls.{h,cpp} for the saver-agnostic manifest/state
// logic (unit-tested on Linux, like the rest of src/core/).
//
// Deliberately independent of ScrKit.scr's own WinMain.cpp/SaverWindow.cpp: this file re-implements
// the same standard .scr contract (/s /c /p, docs/SCRAPI_V2_DRAFT.md §A.1) from scratch, in one
// small file, so it can double as minimal example code for a third-party SCRAPI implementer who
// doesn't want to read ScrKit's full renderer/effect-engine machinery just to see how the wiring
// works.
//
//   /s          run fullscreen (no SCRAPI: docs/SCRAPI_SPEC.md §7 says /s never accepts it)
//   /c          show a short explanation (this demo has no settings UI -- everything is SCRAPI-only)
//   /p <hwnd> [/scrapi:<pipeName>]   embed a live preview, optionally under SCRAPI control

#include <algorithm>
#include <cwctype>
#include <memory>
#include <string>
#include <windows.h>
#include <shellapi.h>

#include "../../core/DemoControls.h"
#include "../../core/Version.h"
#include "DemoHost.h"
#include "DemoRenderer.h"

namespace {

constexpr wchar_t kWindowClassName[] = L"ScrApiDemoWindow";
constexpr UINT kTickTimerId = 1;
constexpr UINT kTickIntervalMs = 33; // ~30fps -- plenty for a GDI-only demo
constexpr LONG kMouseMoveExitThresholdPx = 8;
constexpr UINT kPreviewParentWatchTimerId = 2;

enum class Mode { RunFullScreen, Configure, Preview };

struct ParsedArgs {
    Mode mode = Mode::Configure;
    HWND parentOrPreviewHwnd = nullptr;
    std::wstring scrapiPipeName;
};

bool StartsWithSwitch(const std::wstring& token, wchar_t letter) {
    if (token.size() < 2) return false;
    if (token[0] != L'/' && token[0] != L'-') return false;
    return std::towlower(token[1]) == letter;
}

HWND ExtractHwndFromColonSuffix(const std::wstring& token) {
    const auto colon = token.find(L':');
    if (colon == std::wstring::npos) return nullptr;
    try {
        return reinterpret_cast<HWND>(static_cast<intptr_t>(std::stoll(token.substr(colon + 1))));
    } catch (...) {
        return nullptr;
    }
}

ParsedArgs ParseCommandLine() {
    ParsedArgs result;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return result;

    for (int i = 1; i < argc; ++i) {
        const std::wstring token = argv[i];
        constexpr wchar_t kScrapiPrefix[] = L"/scrapi:";
        if (token.size() > 8 && _wcsnicmp(token.c_str(), kScrapiPrefix, 8) == 0) {
            result.scrapiPipeName = token.substr(8);
            continue;
        }
        if (StartsWithSwitch(token, L's')) {
            result.mode = Mode::RunFullScreen;
        } else if (StartsWithSwitch(token, L'c')) {
            result.mode = Mode::Configure;
        } else if (StartsWithSwitch(token, L'p')) {
            result.mode = Mode::Preview;
            if (HWND h = ExtractHwndFromColonSuffix(token)) {
                result.parentOrPreviewHwnd = h;
            } else if (i + 1 < argc) {
                try {
                    result.parentOrPreviewHwnd = reinterpret_cast<HWND>(static_cast<intptr_t>(std::stoll(argv[i + 1])));
                    ++i;
                } catch (...) {
                }
            }
        }
    }
    LocalFree(argv);
    return result;
}

struct WindowContext {
    Mode mode = Mode::RunFullScreen;
    HWND previewParent = nullptr;
    std::unique_ptr<demo::DemoHost> host; // null in fullscreen mode (/s never runs SCRAPI)
    core::DemoState fullscreenState;      // used instead of host->state() when host is null
    ULONGLONG lastTickMs = 0;
    POINT initialCursorPos{};
    bool haveInitialCursorPos = false;
};

WindowContext* GetContext(HWND hwnd) { return reinterpret_cast<WindowContext*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA)); }

void Tick(HWND hwnd, WindowContext& ctx) {
    const ULONGLONG now = GetTickCount64();
    const float realDt = ctx.lastTickMs == 0 ? 0.0f : std::min(0.25f, (now - ctx.lastTickMs) / 1000.0f);
    ctx.lastTickMs = now;

    if (ctx.host) {
        ctx.host->Tick(realDt, static_cast<double>(now) / 1000.0);
    } else {
        ctx.fullscreenState.elapsedSeconds += realDt;
    }
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK DemoWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_PAINT: {
            WindowContext* ctx = GetContext(hwnd);
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client;
            GetClientRect(hwnd, &client);
            if (ctx) {
                const core::DemoState& state = ctx->host ? ctx->host->state() : ctx->fullscreenState;
                const bool burst = ctx->host && ctx->host->burstActive();
                demo::PaintDemoFrame(hdc, client, state, burst);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }
        case WM_TIMER: {
            WindowContext* ctx = GetContext(hwnd);
            if (!ctx) return 0;
            if (wParam == kPreviewParentWatchTimerId) {
                if (!ctx->previewParent || !IsWindow(ctx->previewParent)) DestroyWindow(hwnd);
                return 0;
            }
            if (wParam == kTickTimerId) {
                Tick(hwnd, *ctx);
                return 0;
            }
            return 0;
        }
        default:
            break;
    }

    WindowContext* ctx = GetContext(hwnd);
    if (ctx && ctx->mode == Mode::RunFullScreen) {
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
                    const LONG dx = current.x - ctx->initialCursorPos.x, dy = current.y - ctx->initialCursorPos.y;
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

void RegisterWindowClass(HINSTANCE instance) {
    static bool registered = false;
    if (registered) return;
    WNDCLASSW wc{};
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = DemoWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr; // WM_ERASEBKGND handled manually
    wc.lpszClassName = kWindowClassName;
    RegisterClassW(&wc);
    registered = true;
}

void RunWindow(HINSTANCE instance, const ParsedArgs& args) {
    RegisterWindowClass(instance);

    auto ctx = std::make_unique<WindowContext>();
    ctx->mode = args.mode;
    ctx->previewParent = args.parentOrPreviewHwnd;

    RECT rect{0, 0, 800, 600};
    DWORD style = WS_POPUP | WS_VISIBLE;
    HWND parent = nullptr;
    if (args.mode == Mode::Preview && args.parentOrPreviewHwnd && IsWindow(args.parentOrPreviewHwnd)) {
        GetClientRect(args.parentOrPreviewHwnd, &rect);
        style = WS_CHILD | WS_VISIBLE;
        parent = args.parentOrPreviewHwnd;
    } else if (args.mode == Mode::RunFullScreen) {
        rect.right = GetSystemMetrics(SM_CXSCREEN);
        rect.bottom = GetSystemMetrics(SM_CYSCREEN);
    }

    HWND hwnd = CreateWindowExW(args.mode == Mode::RunFullScreen ? WS_EX_TOPMOST : 0, kWindowClassName, L"ScrApiDemo",
                                style, 0, 0, rect.right - rect.left, rect.bottom - rect.top, parent, nullptr, instance,
                                nullptr);
    if (!hwnd) return;
    SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(ctx.get()));

    if (args.mode == Mode::Preview && !args.scrapiPipeName.empty()) {
        ctx->host = std::make_unique<demo::DemoHost>(hwnd, args.scrapiPipeName, core::kAppVersion);
    }
    if (args.mode == Mode::Preview) {
        SetTimer(hwnd, kPreviewParentWatchTimerId, 500, nullptr);
    }
    if (args.mode == Mode::RunFullScreen) {
        ShowCursor(FALSE);
        SetForegroundWindow(hwnd);
    }
    SetTimer(hwnd, kTickTimerId, kTickIntervalMs, nullptr);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    if (args.mode == Mode::RunFullScreen) ShowCursor(TRUE);
}

} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    const ParsedArgs args = ParseCommandLine();
    if (args.mode == Mode::Configure) {
        MessageBoxW(nullptr,
                    L"ScrApiDemo has no settings dialog: every value is controlled through SCRAPI "
                    L"(open it in ScrViewer to see the full control panel).",
                    L"ScrApiDemo", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    RunWindow(hInstance, args);
    return 0;
}
