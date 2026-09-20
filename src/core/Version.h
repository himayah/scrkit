#pragma once
// Reported to SCRAPI viewers in the manifest. Releases are tag-driven (a `v*` tag
// builds the .scr), so this string is informational: bump it when cutting a release
// if you want viewers to show it; "dev" marks a build off a feature branch.

namespace core {
constexpr const char* kAppVersion = "3.0.0";
} // namespace core
