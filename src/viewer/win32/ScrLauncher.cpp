#include "ScrLauncher.h"

#include <objbase.h>

#include <vector>

namespace viewer {

std::wstring MakePipeName() {
    GUID guid{};
    if (FAILED(CoCreateGuid(&guid))) {
        return L"scrapi-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount());
    }
    wchar_t buffer[64] = {};
    StringFromGUID2(guid, buffer, 64); // "{XXXXXXXX-XXXX-...}"
    std::wstring text = buffer;
    std::wstring name = L"scrapi-";
    for (wchar_t c : text) {
        if (c != L'{' && c != L'}') name += c;
    }
    return name;
}

bool ScrLauncher::Launch(const std::wstring& scrPath, HWND previewParent, const std::wstring& pipeName,
                          std::wstring* error) {
    Terminate(0);

    std::wstring command = L"\"" + scrPath + L"\" /p " + std::to_wstring(static_cast<long long>(reinterpret_cast<intptr_t>(previewParent))) +
                           L" /scrapi:" + pipeName;
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0'); // CreateProcessW may modify the buffer

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        if (error) *error = L"CreateProcess failed (error " + std::to_wstring(GetLastError()) + L")";
        return false;
    }
    CloseHandle(pi.hThread);
    process_ = pi.hProcess;
    return true;
}

bool ScrLauncher::IsRunning() const {
    return process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT;
}

void ScrLauncher::Terminate(DWORD graceMs) {
    if (!process_) return;
    if (graceMs > 0 && WaitForSingleObject(process_, graceMs) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 0);
        WaitForSingleObject(process_, 1000);
    } else if (graceMs == 0 && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
        TerminateProcess(process_, 0);
        WaitForSingleObject(process_, 1000);
    }
    CloseHandle(process_);
    process_ = nullptr;
}

} // namespace viewer
