#pragma once
// Resolves the per-user data directory used for settings and logs
// (要件.txt §6: 「レジストリではなく .ini ファイルに保存する」
//  例: %APPDATA%/ScrKit/config.ini).

#include <string>

namespace platform {

// Returns "%APPDATA%\ScrKit", creating the directory if it does
// not exist yet. Returns an empty string if APPDATA could not be resolved.
std::wstring GetAppDataDirectory();

std::wstring GetConfigFilePath(); // .../config.ini
std::wstring GetLogFilePath();    // .../saver.log

} // namespace platform
