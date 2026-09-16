#pragma once
// TEMPORARY diagnostic utility for the foreground-mask investigation
// (user feedback: "holes" appearing inside what should be solid captured
// icon/window rectangles). To be removed once root-caused, matching this
// project's usual policy for one-off debug instrumentation (see git history,
// e.g. DESIGN.md §9.7/§9.8's PerfTimer/debug_capture.bmp precedent).

#include <cstdint>
#include <string>

namespace platform {

// Writes an RGBA8 (top-down) buffer as an uncompressed 24-bit BMP (alpha
// dropped) for viewing in any image viewer. Returns false on failure.
bool WriteDebugBmp(const std::wstring& path, const uint8_t* rgba, int width, int height);

} // namespace platform
