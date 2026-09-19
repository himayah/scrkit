#pragma once
// Colors and metrics of the ScrViewer look (docs/DESIGN_VIEWER_UI.md §3).

#include <cstdint>

namespace viewer {

struct Color {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    static constexpr Color Rgb(uint32_t hex, uint8_t alpha = 255) {
        return Color{static_cast<uint8_t>(hex >> 16), static_cast<uint8_t>(hex >> 8), static_cast<uint8_t>(hex), alpha};
    }
    // The same color at `factor` of its alpha (used for disabled controls).
    Color Faded(float factor) const {
        return Color{r, g, b, static_cast<uint8_t>(a * factor + 0.5f)};
    }
    bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    bool operator!=(const Color& o) const { return !(*this == o); }
};

struct Theme {
    Color bg, panel, card, cardBorder, text, text2, line;
    Color accent, accentText, onAccent, accentSoft;
    Color ctl, ctlBorder, ctlBorderBottom;
    Color segTrack, segSel, segSelText;
    Color track, ok, warn, err, hover, pressed;

    float radius = 4.0f;      // controls
    float cardRadius = 8.0f;  // cards
    float fontSize = 13.0f;
    float smallFontSize = 12.0f;
    float disabledAlpha = 0.45f;

    static Theme WindowsLight();
    static Theme WindowsDark();
};

} // namespace viewer
