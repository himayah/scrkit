#pragma once
// UTF-8 <-> UTF-16 helpers. core::ConfigModel stores paths as UTF-8
// std::string (kept portable); Win32 wide APIs need std::wstring, so all
// conversion happens here at the platform boundary.

#include <string>
#include <windows.h>

namespace platform {

inline std::wstring Utf8ToWide(const std::string& utf8) {
    if (utf8.empty()) return L"";
    const int needed = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, nullptr, 0);
    if (needed <= 0) return L"";
    std::wstring wide(static_cast<size_t>(needed) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), -1, wide.data(), needed);
    return wide;
}

inline std::string WideToUtf8(const std::wstring& wide) {
    if (wide.empty()) return "";
    const int needed = WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "";
    std::string utf8(static_cast<size_t>(needed) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), -1, utf8.data(), needed, nullptr, nullptr);
    return utf8;
}

} // namespace platform
