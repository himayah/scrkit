#pragma once
// Text width, supplied by the platform (DirectWrite on Windows; a fixed-width fake in tests).

#include <string>

namespace viewer {

class ITextMeasurer {
public:
    virtual ~ITextMeasurer() = default;
    // Width in layout units of `utf8` set at `fontSize` (bold or regular), on one line.
    virtual float Width(const std::string& utf8, float fontSize, bool bold) = 0;
};

} // namespace viewer
