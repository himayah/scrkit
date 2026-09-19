#pragma once
// Plain geometry for the viewer's custom-drawn UI (docs/DESIGN_VIEWER_UI.md §5).

namespace viewer {

struct Point {
    float x = 0.0f;
    float y = 0.0f;
};

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    float Right() const { return x + w; }
    float Bottom() const { return y + h; }
    bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    Rect Inset(float d) const { return {x + d, y + d, w - 2 * d, h - 2 * d}; }
    Rect Offset(float dx, float dy) const { return {x + dx, y + dy, w, h}; }
    Point Center() const { return {x + w / 2, y + h / 2}; }
};

} // namespace viewer
