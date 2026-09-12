#include "WinFileIO.h"

#include <windows.h>

namespace platform {

bool ReadTextFileW(const std::wstring& path, std::string& out) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > (1024LL * 1024 * 16)) {
        CloseHandle(file);
        return false;
    }

    out.assign(static_cast<size_t>(size.QuadPart), '\0');
    DWORD bytesRead = 0;
    BOOL ok = TRUE;
    if (size.QuadPart > 0) {
        ok = ReadFile(file, out.data(), static_cast<DWORD>(size.QuadPart), &bytesRead, nullptr);
    }
    CloseHandle(file);
    if (!ok) {
        return false;
    }
    out.resize(bytesRead);
    return true;
}

bool WriteTextFileW(const std::wstring& path, const std::string& content) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == content.size();
}

long long GetFileSizeW(const std::wstring& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
        return -1;
    }
    LARGE_INTEGER size;
    size.HighPart = data.nFileSizeHigh;
    size.LowPart = data.nFileSizeLow;
    return size.QuadPart;
}

bool AppendTextFileW(const std::wstring& path, const std::string& content) {
    HANDLE file = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                               OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == content.size();
}

} // namespace platform
