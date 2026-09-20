#include "DemoHost.h"

#include "../../platform/win32/WinFileIO.h"

namespace demo {

namespace {
constexpr int kMaxLinesPerFrame = 64; // bounds the work a chatty viewer can cost one frame

// ScrApiDemo keeps its own data folder, deliberately separate from ScrKit's
// (%APPDATA%\ScrKit): it is its own independent SCRAPI implementation, not a ScrKit feature, and
// proving that means not quietly borrowing ScrKit's identity for anything, including this.
std::wstring GetDemoDataDir() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) return L"";
    std::wstring dir = buffer;
    dir += L"\\ScrApiDemo";
    if (!CreateDirectoryW(dir.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return L"";
    return dir;
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

core::DemoHostHooks MakeHooks() {
    core::DemoHostHooks h;
    h.saveToConfig = [](const std::string& iniText, const std::string& outputFolder) {
        std::wstring dir = outputFolder.empty() ? GetDemoDataDir() : Widen(outputFolder);
        if (dir.empty()) return;
        platform::WriteTextFileW(dir + L"\\demo.ini", iniText);
    };
    return h;
}
} // namespace

DemoHost::DemoHost(HWND viewport, const std::wstring& pipeName, const std::string& version) : binder_(MakeHooks()) {
    server_ = std::make_unique<scrapi::ServerCore>(core::BuildDemoManifest(version), binder_.Hooks(),
                                                   [this](const std::string& line) { pipe_.SendLine(line); });
    binder_.Attach(*server_);
    binder_.ApplyInitialState();
    server_->SetViewportHandle(static_cast<int64_t>(reinterpret_cast<intptr_t>(viewport)));
    pipe_.Start(pipeName);
}

DemoHost::~DemoHost() { pipe_.Stop(); }

void DemoHost::Tick(float realDt, double nowSeconds) {
    if (!active_) return;
    if (pipe_.IsFailed() || server_->closed()) {
        active_ = false;
        pipe_.Stop();
        return;
    }
    std::string line;
    for (int i = 0; i < kMaxLinesPerFrame && pipe_.PopLine(line); ++i) server_->HandleLine(line);
    binder_.Tick(realDt);
    server_->Flush(nowSeconds);
}

} // namespace demo
