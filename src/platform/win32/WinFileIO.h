#pragma once
// Small helpers to read/write whole text files by wide (Unicode) path.
//
// core::LoadConfigFromFile/SaveConfigToFile take a narrow std::string path
// and use std::ifstream/ofstream, which on Windows interpret that path in
// the system ANSI codepage. %APPDATA% can contain non-ASCII characters (a
// non-English Windows username), so the platform layer reads/writes the
// config file itself via the wide-character Win32 API and only hands the
// resulting text to core::ParseConfigIni / core::SerializeConfigIni.

#include <string>

namespace platform {

// Reads the whole file as raw bytes into `out` (interpreted as UTF-8 text by
// the caller). Returns false if the file does not exist or cannot be read.
bool ReadTextFileW(const std::wstring& path, std::string& out);

// Writes `content` to `path`, overwriting any existing file. Returns false
// on failure.
bool WriteTextFileW(const std::wstring& path, const std::string& content);

// Returns the file size in bytes, or -1 if it cannot be determined.
long long GetFileSizeW(const std::wstring& path);

// Appends `content` to the file, creating it if necessary.
bool AppendTextFileW(const std::wstring& path, const std::string& content);

} // namespace platform
