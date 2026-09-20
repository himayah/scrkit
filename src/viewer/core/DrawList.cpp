#include "DrawList.h"

namespace viewer {

namespace {
DrawCmd Make(DrawCmd::Kind kind) {
    DrawCmd c;
    c.kind = kind;
    return c;
}
} // namespace

void DrawList::FillRoundRect(const Rect& r, float radius, Color c) {
    DrawCmd d = Make(DrawCmd::Kind::FillRoundRect);
    d.rect = r;
    d.radius = radius;
    d.color = c;
    cmds.push_back(std::move(d));
}

void DrawList::StrokeRoundRect(const Rect& r, float radius, Color c, float width) {
    DrawCmd d = Make(DrawCmd::Kind::StrokeRoundRect);
    d.rect = r;
    d.radius = radius;
    d.color = c;
    d.strokeWidth = width;
    cmds.push_back(std::move(d));
}

void DrawList::FillEllipse(const Rect& box, Color c) {
    DrawCmd d = Make(DrawCmd::Kind::FillEllipse);
    d.rect = box;
    d.color = c;
    cmds.push_back(std::move(d));
}

void DrawList::StrokeEllipse(const Rect& box, Color c, float width) {
    DrawCmd d = Make(DrawCmd::Kind::StrokeEllipse);
    d.rect = box;
    d.color = c;
    d.strokeWidth = width;
    cmds.push_back(std::move(d));
}

void DrawList::Line(Point a, Point b, Color c, float width) {
    DrawCmd d = Make(DrawCmd::Kind::Line);
    d.points = {a, b};
    d.color = c;
    d.strokeWidth = width;
    cmds.push_back(std::move(d));
}

void DrawList::Polyline(const std::vector<Point>& pts, Color c, float width) {
    DrawCmd d = Make(DrawCmd::Kind::Polyline);
    d.points = pts;
    d.color = c;
    d.strokeWidth = width;
    cmds.push_back(std::move(d));
}

void DrawList::Arc(float cx, float cy, float r, float a0, float a1, Color c, float width) {
    DrawCmd d = Make(DrawCmd::Kind::Arc);
    d.cx = cx;
    d.cy = cy;
    d.r = r;
    d.a0 = a0;
    d.a1 = a1;
    d.color = c;
    d.strokeWidth = width;
    cmds.push_back(std::move(d));
}

void DrawList::Text(const Rect& box, const std::string& text, float fontSize, bool bold, Color c, TextAlign align) {
    DrawCmd d = Make(DrawCmd::Kind::Text);
    d.rect = box;
    d.text = text;
    d.fontSize = fontSize;
    d.bold = bold;
    d.color = c;
    d.align = align;
    cmds.push_back(std::move(d));
}

void DrawList::PushClip(const Rect& r) {
    DrawCmd d = Make(DrawCmd::Kind::PushClip);
    d.rect = r;
    cmds.push_back(std::move(d));
}

void DrawList::PopClip() { cmds.push_back(Make(DrawCmd::Kind::PopClip)); }

void DrawList::Icon(IconKind kind, const Rect& box, Color c, float width) {
    const float sx = box.w / 16.0f, sy = box.h / 16.0f;
    auto at = [&](float gx, float gy) { return Point{box.x + gx * sx, box.y + gy * sy}; };
    switch (kind) {
        case IconKind::ChevronDown: Polyline({at(4, 6), at(8, 10), at(12, 6)}, c, width); break;
        case IconKind::ChevronRight: Polyline({at(6, 4), at(10, 8), at(6, 12)}, c, width); break;
        case IconKind::Check: Polyline({at(3.5f, 8.5f), at(6.5f, 11.5f), at(12.5f, 4.5f)}, c, width); break;
    }
}

} // namespace viewer
