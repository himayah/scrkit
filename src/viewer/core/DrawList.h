#pragma once
// The display list the panel controller produces and a platform renderer executes
// (docs/DESIGN_VIEWER_UI.md §5). Keeping "what gets drawn" as data is what makes the look
// of the panel testable without Windows.

#include <string>
#include <vector>

#include "Geometry.h"
#include "Theme.h"

namespace viewer {

enum class TextAlign { Left, Center, Right };
enum class IconKind { ChevronDown, ChevronRight, Check };

struct DrawCmd {
    enum class Kind { FillRoundRect, StrokeRoundRect, FillEllipse, StrokeEllipse, Line, Polyline, Arc, Text, PushClip, PopClip };
    Kind kind = Kind::FillRoundRect;
    Rect rect;              // rects, ellipses (bounding box), text box, clip
    float radius = 0.0f;    // corner radius
    Color color;
    float strokeWidth = 1.0f;
    std::vector<Point> points; // Line (2 points) / Polyline
    // Arc: center, radius, and angles in degrees measured clockwise from 12 o'clock.
    float cx = 0.0f, cy = 0.0f, r = 0.0f, a0 = 0.0f, a1 = 0.0f;
    // Text
    std::string text;
    float fontSize = 13.0f;
    bool bold = false;
    TextAlign align = TextAlign::Left;
};

class DrawList {
public:
    std::vector<DrawCmd> cmds;

    void FillRoundRect(const Rect& r, float radius, Color c);
    void StrokeRoundRect(const Rect& r, float radius, Color c, float width = 1.0f);
    void FillEllipse(const Rect& box, Color c);
    void StrokeEllipse(const Rect& box, Color c, float width = 1.0f);
    void Line(Point a, Point b, Color c, float width = 1.0f);
    void Polyline(const std::vector<Point>& pts, Color c, float width = 1.5f);
    void Arc(float cx, float cy, float r, float a0, float a1, Color c, float width);
    void Text(const Rect& box, const std::string& text, float fontSize, bool bold, Color c, TextAlign align = TextAlign::Left);
    void PushClip(const Rect& r);
    void PopClip();
    // A small stroked glyph fitted into `box` (designed on a 16-unit grid).
    void Icon(IconKind kind, const Rect& box, Color c, float width = 1.5f);
};

} // namespace viewer
