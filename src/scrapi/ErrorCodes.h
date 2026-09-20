#pragma once
// Error codes of docs/SCRAPI_SPEC.md §4.4.

namespace scrapi::err {
constexpr const char* kUnknownId = "unknownId";
constexpr const char* kTypeMismatch = "typeMismatch";
constexpr const char* kReadOnly = "readOnly";
constexpr const char* kUnsupported = "unsupported";
constexpr const char* kNotReady = "notReady";
constexpr const char* kBadRequest = "badRequest";
constexpr const char* kInternal = "internal";
} // namespace scrapi::err
