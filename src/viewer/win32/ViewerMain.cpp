// ScrViewer: hosts a SCRAPI-capable .scr's preview in its own window and builds
// controllers for the controls the .scr declares (docs/DESIGN_VIEWER.md §C).
//
//   ScrViewer.exe [path\to\saver.scr]     (or File > Open, or drop a .scr on the window)

// <windows.h> must come first: <commctrl.h>/<shellapi.h> depend on its types.
#include <windows.h>

#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "../../platform/win32/ScrApiPipe.h"
#include "../../scrapi/ClientCore.h"
#include "ThemedPanel.h"
#include "ScrLauncher.h"

// Common Controls v6 (visual styles) without a separate manifest file: the linker merges
// this dependency into the executable's own manifest. MSVC only; MinGW builds get the
// classic look, which works the same.
#if defined(_MSC_VER)
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif

namespace {

constexpr wchar_t kMainClass[] = L"ScrViewerMainWindow";
constexpr wchar_t kHostClass[] = L"ScrViewerPreviewHost";
constexpr UINT WM_APP_PIPE = WM_APP + 1;
constexpr UINT_PTR kPollTimerId = 1;
constexpr int kIdOpen = 100;
constexpr int kIdExit = 101;
constexpr int kPanelWidth = 400;
constexpr int kStatusHeight = 22;
constexpr DWORD kConnectGraceMs = 3500;

enum class Phase { None, Waiting, Ready, PlainPreview, Failed };

struct Viewer {
    HINSTANCE instance = nullptr;
    HWND main = nullptr;
    HWND host = nullptr;   // the /p parent the .scr renders into
    HWND status = nullptr;
    viewer::ThemedPanel panel;

    viewer::ScrLauncher launcher;
    std::unique_ptr<platform::ScrApiPipe> pipe;
    std::unique_ptr<scrapi::ClientCore> client;
    Phase phase = Phase::None;
    bool sessionStarted = false;
    DWORD launchedAt = 0;
    // Where the time goes between "open" and "controls visible", shown in the status bar.
    DWORD connectedAt = 0;
    DWORD panelMs = 0;
    std::wstring scrPath;
};

Viewer g;

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

void SetStatus(const std::wstring& text) { SetWindowTextW(g.status, text.c_str()); }

void LayoutChildren() {
    RECT client{};
    GetClientRect(g.main, &client);
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;
    const int panelW = std::min(kPanelWidth, std::max(0, w / 2));
    const int bodyH = std::max(0, h - kStatusHeight);
    const int hostW = std::max(1, w - panelW);
    MoveWindow(g.host, 0, 0, hostW, bodyH, TRUE);
    MoveWindow(g.panel.hwnd(), hostW, 0, panelW, bodyH, TRUE);
    MoveWindow(g.status, 0, bodyH, w, kStatusHeight, TRUE);
    InvalidateRect(g.status, nullptr, TRUE);

    // Let the saver's window follow the host if it advertises viewport.resize.
    if (g.client && g.client->state() == scrapi::ClientCore::State::Ready && g.client->viewportHandle() != 0 &&
        g.client->model() && g.client->model()->manifest().HasCapability("viewport.resize")) {
        HWND scr = reinterpret_cast<HWND>(static_cast<intptr_t>(g.client->viewportHandle()));
        if (IsWindow(scr)) {
            SetWindowPos(scr, nullptr, 0, 0, hostW, bodyH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_ASYNCWINDOWPOS);
        }
    }
}

void CloseSession() {
    KillTimer(g.main, kPollTimerId);
    g.panel.Clear();
    if (g.client && g.client->state() != scrapi::ClientCore::State::Closed &&
        g.client->state() != scrapi::ClientCore::State::Idle && g.pipe && g.pipe->IsConnected()) {
        g.client->Bye();
    }
    g.launcher.Terminate(1000);
    g.client.reset();
    if (g.pipe) g.pipe->Stop();
    g.pipe.reset();
    g.phase = Phase::None;
    g.sessionStarted = false;
}

// "folder\\file.scr": enough of the path to tell builds apart in the title bar.
std::wstring ShortPath(const std::wstring& path) {
    const size_t last = path.find_last_of(L"\\/");
    if (last == std::wstring::npos) return path;
    const size_t prev = last == 0 ? std::wstring::npos : path.find_last_of(L"\\/", last - 1);
    return prev == std::wstring::npos ? path : path.substr(prev + 1);
}

void OpenSaver(const std::wstring& path) {
    CloseSession();
    g.scrPath = path;
    SetWindowTextW(g.main, (L"ScrViewer - " + ShortPath(path)).c_str());

    const std::wstring pipeName = viewer::MakePipeName();
    g.pipe = std::make_unique<platform::ScrApiPipe>();
    HWND main = g.main;
    g.pipe->SetIncomingCallback([main] { PostMessageW(main, WM_APP_PIPE, 0, 0); });
    if (!g.pipe->CreateServerPipe(pipeName)) {
        SetStatus(L"Could not create the control pipe.");
        g.phase = Phase::Failed;
        return;
    }

    g.client = std::make_unique<scrapi::ClientCore>([](const std::string& line) {
        if (g.pipe) g.pipe->SendLine(line);
    });
    g.client->onManifestChanged = [] {
        const DWORD t0 = GetTickCount();
        g.panel.Rebuild(g.client->model());
        g.panelMs = GetTickCount() - t0;
    };
    g.client->onValuesChanged = [](const std::vector<std::string>& ids) { g.panel.Refresh(ids); };
    g.client->onReady = [] {
        g.phase = Phase::Ready;
        const auto& s = g.client->saver();
        const DWORD now = GetTickCount();
        SetStatus(L"Connected: " + Widen(s.name) + L" " + Widen(s.version) + L"   (saver started in " +
                  std::to_wstring(g.connectedAt - g.launchedAt) + L" ms, handshake " +
                  std::to_wstring(now - g.connectedAt - g.panelMs) + L" ms, panel built in " +
                  std::to_wstring(g.panelMs) + L" ms)");
        LayoutChildren();
    };
    g.client->onFailed = [](const std::string& reason) {
        g.phase = Phase::Failed;
        SetStatus(L"SCRAPI session failed: " + Widen(reason));
    };
    g.client->onLog = [](const std::string& level, const std::string& message) {
        OutputDebugStringW(Widen("[saver " + level + "] " + message + "\n").c_str());
    };
    g.panel.SetCallbacks(
        [](const std::string& id, const scrapi::JsonValue& value) {
            if (g.client) g.client->Set({{id, value}});
        },
        [](const std::string& id) {
            if (g.client) g.client->Invoke(id);
        });

    std::wstring error;
    LayoutChildren(); // give the host its final size before the saver reads it
    if (!g.launcher.Launch(path, g.host, pipeName, &error)) {
        SetStatus(error);
        g.phase = Phase::Failed;
        return;
    }
    g.pipe->StartServing(kConnectGraceMs);
    g.phase = Phase::Waiting;
    g.launchedAt = GetTickCount();
    SetStatus(L"Waiting for the screensaver...");
    SetTimer(g.main, kPollTimerId, 100, nullptr);
}

void Poll() {
    if (!g.pipe || !g.client) return;
    if (g.phase == Phase::Waiting) {
        if (g.pipe->IsConnected() && !g.sessionStarted) {
            g.sessionStarted = true;
            g.connectedAt = GetTickCount();
            g.client->StartSession("ScrViewer/0.1");
        } else if (!g.pipe->IsConnected() && (g.pipe->IsFailed() || GetTickCount() - g.launchedAt > kConnectGraceMs + 500)) {
            g.phase = Phase::PlainPreview;
            SetStatus(g.launcher.IsRunning() ? L"This screensaver does not support SCRAPI: standard preview only."
                                             : L"The screensaver exited before connecting.");
        }
    }
    if (!g.launcher.IsRunning() && g.phase != Phase::None && g.phase != Phase::Failed) {
        if (g.phase != Phase::PlainPreview) SetStatus(L"The screensaver has exited.");
        g.panel.Clear();
        g.phase = Phase::Failed;
    }
    if (g.pipe && g.phase == Phase::Ready && g.pipe->IsFailed()) {
        SetStatus(L"Connection to the screensaver was lost.");
        g.phase = Phase::Failed;
    }
}

void DrainIncoming() {
    if (!g.pipe || !g.client) return;
    std::string line;
    while (g.pipe->PopLine(line)) g.client->OnLine(line);
}

// The folder ScrViewer.exe lives in: builds are unpacked side by side with their .scr, so this is
// where the matching screensaver is (the dialog otherwise reopens on whatever folder was used last,
// which is how an older build's .scr gets opened by mistake).
std::wstring ExeFolder() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring s = path;
    const size_t slash = s.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : s.substr(0, slash);
}

void ChooseAndOpen() {
    wchar_t file[MAX_PATH] = {};
    OPENFILENAMEW ofn{};
    const std::wstring startDir = ExeFolder();
    ofn.lpstrInitialDir = startDir.empty() ? nullptr : startDir.c_str();
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g.main;
    const wchar_t filter[] = L"Screensavers (*.scr)\0*.scr\0All files\0*.*\0\0";
    ofn.lpstrFilter = filter;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&ofn)) OpenSaver(file);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            if (g.host && g.status) LayoutChildren();
            return 0;
        case WM_COMMAND:
            if (LOWORD(wParam) == kIdOpen) ChooseAndOpen();
            else if (LOWORD(wParam) == kIdExit) DestroyWindow(hwnd);
            return 0;
        case WM_APP_PIPE:
            DrainIncoming();
            return 0;
        case WM_TIMER:
            if (wParam == kPollTimerId) Poll();
            return 0;
        case WM_DROPFILES: {
            wchar_t file[MAX_PATH] = {};
            if (DragQueryFileW(reinterpret_cast<HDROP>(wParam), 0, file, MAX_PATH)) OpenSaver(file);
            DragFinish(reinterpret_cast<HDROP>(wParam));
            return 0;
        }
        case WM_CLOSE:
            CloseSession();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

} // namespace

int APIENTRY WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_BAR_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);
    g.instance = instance;

    WNDCLASSW main{};
    main.lpfnWndProc = MainProc;
    main.hInstance = instance;
    main.hCursor = LoadCursor(nullptr, IDC_ARROW);
    main.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    main.lpszClassName = kMainClass;
    RegisterClassW(&main);

    WNDCLASSW host{};
    host.lpfnWndProc = DefWindowProcW;
    host.hInstance = instance;
    host.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    host.lpszClassName = kHostClass;
    RegisterClassW(&host);

    HMENU menu = CreateMenu();
    HMENU file = CreatePopupMenu();
    AppendMenuW(file, MF_STRING, kIdOpen, L"&Open screensaver...");
    AppendMenuW(file, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(file, MF_STRING, kIdExit, L"E&xit");
    AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(file), L"&File");

    g.main = CreateWindowExW(WS_EX_ACCEPTFILES, kMainClass, L"ScrViewer", WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                             CW_USEDEFAULT, CW_USEDEFAULT, 1280, 720, nullptr, menu, instance, nullptr);
    if (!g.main) return 1;
    g.host = CreateWindowExW(0, kHostClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS, 0, 0, 100, 100,
                             g.main, nullptr, instance, nullptr);
    g.status = CreateWindowExW(0, L"STATIC", L"Open a screensaver (.scr) with File > Open, or drop one on this window.",
                               WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_SUNKEN, 0, 0, 100, kStatusHeight, g.main,
                               nullptr, instance, nullptr);
    SendMessageW(g.status, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    if (!g.panel.Create(g.main, instance)) {
        MessageBoxW(g.main, L"Could not initialize Direct2D/DirectWrite.", L"ScrViewer", MB_OK | MB_ICONERROR);
        return 1;
    }
    ShowWindow(g.main, showCommand);
    UpdateWindow(g.main);
    LayoutChildren();

    int argc = 0;
    if (LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc)) {
        if (argc > 1) OpenSaver(argv[1]);
        LocalFree(argv);
    }

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    CoUninitialize();
    return static_cast<int>(msg.wParam);
}
