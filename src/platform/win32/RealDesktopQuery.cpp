#include "RealDesktopQuery.h"

#include <algorithm>
#include <iterator>

// windows.h must come first: both commctrl.h and dwmapi.h use types
// (HRESULT, LPWSTR, ...) it defines and don't include it themselves
// (the same class of ordering bug fixed for <GL/gl.h> in GLCompat.h).
#include <windows.h>

#include <commctrl.h>
#include <dwmapi.h>

#include "../../core/Logger.h"
#include "ScreenCapture.h"
#include "StringConvert.h"

namespace platform {

namespace {

// Shrinks `rect` to fit within [0,screenWidth) x [0,screenHeight) (the
// primary monitor, whose origin is always (0,0) in virtual-screen
// coordinates). Returns false if it doesn't overlap that region at all
// (e.g. a window sitting entirely on a secondary monitor) -- this project
// intentionally only simulates the primary display (design doc §9).
bool ClampRectToScreen(RECT& rect, int screenWidth, int screenHeight) {
    if (rect.right <= 0 || rect.bottom <= 0 || rect.left >= screenWidth || rect.top >= screenHeight) {
        return false;
    }
    if (rect.left < 0) rect.left = 0;
    if (rect.top < 0) rect.top = 0;
    if (rect.right > screenWidth) rect.right = screenWidth;
    if (rect.bottom > screenHeight) rect.bottom = screenHeight;
    return rect.right > rect.left && rect.bottom > rect.top;
}

bool IsCloaked(HWND hwnd) {
    DWORD cloaked = 0;
    return SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked != 0;
}

// Shell/desktop-owned windows to never treat as a "real open window" -- most
// notably Progman itself, which spans the entire screen and (perhaps
// surprisingly) really is a visible, titled ("Program Manager") top-level
// window, which would otherwise show up as one giant fake window box.
bool IsShellOwnedClass(const wchar_t* className) {
    static const wchar_t* const kShellClasses[] = {
        L"Progman", L"WorkerW", L"Shell_TrayWnd", L"Shell_SecondaryTrayWnd", L"Button",
    };
    for (const wchar_t* shellClass : kShellClasses) {
        if (lstrcmpW(className, shellClass) == 0) return true;
    }
    return false;
}

struct EnumWindowsContext {
    int screenWidth = 0;
    int screenHeight = 0;
    float titleBarHeight = 0.0f;
    std::vector<RealWindowInfo>* out = nullptr;
};

BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    auto* ctx = reinterpret_cast<EnumWindowsContext*>(lParam);
    constexpr size_t kMaxWindows = 30;
    if (ctx->out->size() >= kMaxWindows) {
        return FALSE; // reached the cap; no need to keep enumerating
    }

    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;

    const LONG_PTR exStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (exStyle & WS_EX_TOOLWINDOW) return TRUE;

    wchar_t classBuf[64] = {};
    GetClassNameW(hwnd, classBuf, static_cast<int>(std::size(classBuf)));
    if (IsShellOwnedClass(classBuf)) return TRUE;

    wchar_t titleBuf[256] = {};
    const int titleLen = GetWindowTextW(hwnd, titleBuf, static_cast<int>(std::size(titleBuf)));
    if (titleLen <= 0) return TRUE; // untitled windows are almost always helper/background windows

    if (IsCloaked(hwnd)) return TRUE; // e.g. UWP windows parked on another virtual desktop

    // Plain GetWindowRect (not DWMWA_EXTENDED_FRAME_BOUNDS) so this stays in
    // the exact coordinate space PrintWindow itself uses below -- no
    // separate sub-rect math needed to line the two up. PrintWindow doesn't
    // render DWM's own drop-shadow decoration anyway (that's a compositor
    // overlay, not something the window draws itself), so this is not a
    // meaningful accuracy loss in practice.
    RECT rect{};
    if (!GetWindowRect(hwnd, &rect)) return TRUE;
    if (!ClampRectToScreen(rect, ctx->screenWidth, ctx->screenHeight)) return TRUE;

    RealWindowInfo info;
    info.element.x = static_cast<float>(rect.left);
    info.element.y = static_cast<float>(rect.top);
    info.element.width = static_cast<float>(rect.right - rect.left);
    const float totalHeight = static_cast<float>(rect.bottom - rect.top);
    info.element.titleBarHeight = std::min(ctx->titleBarHeight, totalHeight * 0.5f);
    info.element.height = totalHeight - info.element.titleBarHeight;
    info.element.title = WideToUtf8(std::wstring(titleBuf, static_cast<size_t>(titleLen)));

    // Captured here (before our own fullscreen window exists) so it shows
    // this window's own true content even where something else currently
    // overlaps it on the real screen (user feedback).
    info.hasCapture = CaptureWindowToImage(hwnd, info.capture);

    ctx->out->push_back(std::move(info));
    return TRUE;
}

// Finds the desktop's icon ListView. Normally
// Progman -> SHELLDLL_DefView -> SysListView32, but some shell
// configurations (certain wallpaper engines, "WorkerW" fallback used when
// showing desktop icons on top of a different wallpaper host) reparent
// SHELLDLL_DefView under a WorkerW window instead -- both are checked.
HWND FindDesktopIconListView() {
    HWND progman = FindWindowW(L"Progman", nullptr);
    HWND defView = progman ? FindWindowExW(progman, nullptr, L"SHELLDLL_DefView", nullptr) : nullptr;

    if (!defView) {
        HWND worker = nullptr;
        for (;;) {
            worker = FindWindowExW(nullptr, worker, L"WorkerW", nullptr);
            if (!worker) break;
            defView = FindWindowExW(worker, nullptr, L"SHELLDLL_DefView", nullptr);
            if (defView) break;
        }
    }
    if (!defView) return nullptr;

    return FindWindowExW(defView, nullptr, L"SysListView32", nullptr);
}

// RAII wrapper for a same-machine cross-process buffer, used because
// LVM_GETITEMPOSITION/LVM_GETITEMRECT/LVM_GETITEMTEXTW write their result
// directly into memory owned by the ListView's own process (explorer.exe),
// not ours -- this is the standard, well-documented way third-party desktop
// tools read icon layout (e.g. icon-restore utilities, alternative shells).
class RemoteBuffer {
public:
    RemoteBuffer(HANDLE process, size_t size) : process_(process) {
        ptr_ = VirtualAllocEx(process_, nullptr, size, MEM_COMMIT, PAGE_READWRITE);
    }
    ~RemoteBuffer() {
        if (ptr_) VirtualFreeEx(process_, ptr_, 0, MEM_RELEASE);
    }
    RemoteBuffer(const RemoteBuffer&) = delete;
    RemoteBuffer& operator=(const RemoteBuffer&) = delete;

    bool Valid() const { return ptr_ != nullptr; }
    LPVOID Ptr() const { return ptr_; }

    bool Write(const void* src, size_t size) {
        return ptr_ && WriteProcessMemory(process_, ptr_, src, size, nullptr);
    }
    bool Read(void* dst, size_t size) const {
        return ptr_ && ReadProcessMemory(process_, ptr_, dst, size, nullptr);
    }

private:
    HANDLE process_;
    LPVOID ptr_ = nullptr;
};

} // namespace

bool QueryRealOpenWindows(int screenWidth, int screenHeight, std::vector<RealWindowInfo>& out) {
    out.clear();
    EnumWindowsContext ctx;
    ctx.screenWidth = screenWidth;
    ctx.screenHeight = screenHeight;
    ctx.titleBarHeight = static_cast<float>(GetSystemMetrics(SM_CYCAPTION) + GetSystemMetrics(SM_CYFRAME));
    ctx.out = &out;

    EnumWindows(EnumWindowsProc, reinterpret_cast<LPARAM>(&ctx));

    if (out.empty()) {
        core::Logger::Warn("RealDesktopQuery: no real windows found; caller should fall back");
        return false;
    }
    return true;
}

bool QueryRealDesktopIcons(int screenWidth, int screenHeight, RealIconLayerInfo& out) {
    out = RealIconLayerInfo{};

    HWND listView = FindDesktopIconListView();
    if (!listView) {
        core::Logger::Warn("RealDesktopQuery: desktop icon ListView not found; caller should fall back");
        return false;
    }

    const int count = static_cast<int>(SendMessageW(listView, LVM_GETITEMCOUNT, 0, 0));
    if (count <= 0) {
        core::Logger::Warn("RealDesktopQuery: desktop reports 0 icons; caller should fall back");
        return false;
    }

    // One combined capture of the whole icon layer, taken before our own
    // window exists, so it shows every icon uncovered even ones a window is
    // currently sitting on top of (user feedback). A capture failure here
    // is non-fatal -- positions are still useful with a solid-color/
    // full-screen-capture fallback at the AppController level.
    RECT viewRect{};
    if (GetWindowRect(listView, &viewRect)) {
        out.captureOriginX = static_cast<float>(viewRect.left);
        out.captureOriginY = static_cast<float>(viewRect.top);
        out.hasCapture = CaptureWindowToImage(listView, out.capture);
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(listView, &pid);
    HANDLE process = OpenProcess(PROCESS_VM_OPERATION | PROCESS_VM_READ | PROCESS_VM_WRITE |
                                      PROCESS_QUERY_INFORMATION,
                                  FALSE, pid);
    if (!process) {
        core::Logger::Warn("RealDesktopQuery: OpenProcess(explorer.exe) failed; caller should fall back");
        return false;
    }

    constexpr size_t kTextBufChars = 260;
    RemoteBuffer rectBuf(process, sizeof(RECT));
    RemoteBuffer itemBuf(process, sizeof(LVITEMW) + kTextBufChars * sizeof(wchar_t));
    if (!rectBuf.Valid() || !itemBuf.Valid()) {
        CloseHandle(process);
        core::Logger::Warn("RealDesktopQuery: VirtualAllocEx failed; caller should fall back");
        return false;
    }

    auto* remoteTextPtr =
        reinterpret_cast<LPWSTR>(reinterpret_cast<BYTE*>(itemBuf.Ptr()) + sizeof(LVITEMW));

    for (int i = 0; i < count; ++i) {
        RECT req{};
        req.left = LVIR_BOUNDS;
        if (!rectBuf.Write(&req, sizeof(req))) continue;
        SendMessageW(listView, LVM_GETITEMRECT, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(rectBuf.Ptr()));
        RECT itemRect{};
        if (!rectBuf.Read(&itemRect, sizeof(itemRect))) continue;

        // LVM_GETITEMRECT returns client-area coordinates of the ListView;
        // convert to screen coordinates to match our capture/UV space.
        MapWindowPoints(listView, nullptr, reinterpret_cast<POINT*>(&itemRect), 2);
        if (!ClampRectToScreen(itemRect, screenWidth, screenHeight)) continue;

        LVITEMW item{};
        item.iSubItem = 0;
        item.cchTextMax = static_cast<int>(kTextBufChars);
        item.pszText = remoteTextPtr;
        if (!itemBuf.Write(&item, sizeof(item))) continue;
        SendMessageW(listView, LVM_GETITEMTEXTW, static_cast<WPARAM>(i), reinterpret_cast<LPARAM>(itemBuf.Ptr()));

        wchar_t localText[kTextBufChars] = {};
        if (!ReadProcessMemory(process, remoteTextPtr, localText, sizeof(localText) - sizeof(wchar_t), nullptr)) {
            localText[0] = L'\0';
        }

        core::IconElement icon;
        icon.x = static_cast<float>(itemRect.left);
        icon.y = static_cast<float>(itemRect.top);
        icon.width = static_cast<float>(itemRect.right - itemRect.left);
        icon.height = static_cast<float>(itemRect.bottom - itemRect.top);
        icon.label = WideToUtf8(localText);
        out.icons.push_back(std::move(icon));
    }

    CloseHandle(process);

    if (out.icons.empty()) {
        core::Logger::Warn("RealDesktopQuery: could not read any real icon positions; caller should fall back");
        return false;
    }
    return true;
}

} // namespace platform
