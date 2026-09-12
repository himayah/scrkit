#include "AppPaths.h"

#include <windows.h>

namespace platform {

std::wstring GetAppDataDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    const DWORD len = GetEnvironmentVariableW(L"APPDATA", buffer, MAX_PATH);
    if (len == 0 || len >= MAX_PATH) {
        return L"";
    }

    std::wstring dir = buffer;
    dir += L"\\SpiralSuctionSaver";

    // Best-effort create; ERROR_ALREADY_EXISTS is fine.
    if (!CreateDirectoryW(dir.c_str(), nullptr)) {
        const DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            return L"";
        }
    }
    return dir;
}

std::wstring GetConfigFilePath() {
    const std::wstring dir = GetAppDataDirectory();
    if (dir.empty()) return L"";
    return dir + L"\\config.ini";
}

std::wstring GetLogFilePath() {
    const std::wstring dir = GetAppDataDirectory();
    if (dir.empty()) return L"";
    return dir + L"\\saver.log";
}

} // namespace platform
