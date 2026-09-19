#pragma once
// A minimal 24-bit BMP encoder, for the diagnostic image dumps.

#include <cstdint>
#include <vector>

namespace core {

// Encodes a top-down RGBA8 buffer as a bottom-up 24-bit BMP file image (alpha is dropped).
std::vector<uint8_t> EncodeBmp24(const uint8_t* rgba, int width, int height);

} // namespace core
