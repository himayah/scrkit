#include "DemoRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>

namespace demo {

namespace {

int HexNibble(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return 0;
}
int HexByte(const std::string& s, size_t i) {
    if (i + 1 >= s.size()) return 0;
    return HexNibble(static_cast<wchar_t>(s[i])) * 16 + HexNibble(static_cast<wchar_t>(s[i + 1]));
}

// Parses "#RRGGBB" or "#RRGGBBAA" -- GDI has no alpha-blended fill, so the alpha channel (if any)
// is ignored here; the `color` control type still round-trips it faithfully over SCRAPI regardless
// of what this particular renderer does with it.
COLORREF ParseHexColor(const std::string& hex, COLORREF fallback) {
    if (hex.size() < 7 || hex[0] != '#') return fallback;
    return RGB(HexByte(hex, 1), HexByte(hex, 3), HexByte(hex, 5));
}

bool HasTag(const core::DemoState& s, const char* tag) {
    return std::find(s.tags.begin(), s.tags.end(), tag) != s.tags.end();
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return L"";
    const int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(len > 0 ? len - 1 : 0, L'\0');
    if (len > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), len);
    return out;
}

// The animated phase every mode/shape drives off of -- deliberately independent of
// DemoControlBinder::Tick's own "energy"/"load" phase, so a viewer can see the two vary together
// without literally being the same number.
double Phase(const core::DemoState& s) { return s.elapsedSeconds * s.speed + (s.seed % 1000) * 0.01; }

void DrawShape(HDC hdc, int cx, int cy, int size, const std::string& shape) {
    const int r = std::max(4, size / 2);
    if (shape == "square") {
        Rectangle(hdc, cx - r, cy - r, cx + r, cy + r);
    } else if (shape == "triangle") {
        POINT pts[3] = {{cx, cy - r}, {cx - r, cy + r}, {cx + r, cy + r}};
        Polygon(hdc, pts, 3);
    } else {
        Ellipse(hdc, cx - r, cy - r, cx + r, cy + r);
    }
}

// Built with plain wstring concatenation (not swprintf's "%s"-into-"%ls" mixed-width formatting,
// which MSVC and MinGW's C runtimes don't agree on) -- widen each narrow field, then join.
std::wstring StatusPanelText(const core::DemoState& s) {
    wchar_t numbers[160];
    std::swprintf(numbers, 160, L"size=%d speed=%.2fx seed=%d\npaused=%ls timeScale=%.2fx", s.size, s.speed, s.seed,
                  s.paused ? L"yes" : L"no", s.timeScale);
    wchar_t counts[80];
    std::swprintf(counts, 80, L"bursts=%d elapsed=%.1fs", s.burstCount, s.elapsedSeconds);

    std::wstring out = L"ScrApiDemo\nmode=" + Widen(s.mode) + L" shape=" + Widen(s.shape) + L" quality=" +
                       Widen(s.quality) + L"\n" + numbers + L"\nlabel=\"" + Widen(s.label) + L"\"\nlogo=" +
                       (s.logoPath.empty() ? L"(none)" : Widen(s.logoPath)) + L"\n" + counts;
    return out;
}

} // namespace

void PaintDemoFrame(HDC hdc, const RECT& client, const core::DemoState& s, bool burstActive) {
    const int w = client.right - client.left;
    const int h = client.bottom - client.top;

    HBRUSH bg = CreateSolidBrush(ParseHexColor(s.tint, RGB(30, 42, 60)));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    if (HasTag(s, "grid")) {
        const int step = s.quality == "high" ? 30 : s.quality == "low" ? 100 : 60;
        HPEN gridPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(hdc, gridPen);
        for (int x = step; x < w; x += step) {
            MoveToEx(hdc, x, 0, nullptr);
            LineTo(hdc, x, h);
        }
        for (int y = step; y < h; y += step) {
            MoveToEx(hdc, 0, y, nullptr);
            LineTo(hdc, w, y);
        }
        SelectObject(hdc, oldPen);
        DeleteObject(gridPen);
    }

    if (s.enabled) {
        const double phase = Phase(s);
        int cx = w / 2, cy = h / 2;
        int size = s.size;
        if (s.mode == "pulse") {
            size = static_cast<int>(s.size * (0.6 + 0.4 * std::sin(phase)));
        } else if (s.mode == "sweep") {
            const double margin = s.size;
            cx = static_cast<int>(margin + (w - 2 * margin) * (0.5 + 0.5 * std::sin(phase * 0.7)));
        }

        HBRUSH shapeBrush = CreateSolidBrush(RGB(255, 210, 90));
        HGDIOBJ oldBrush = SelectObject(hdc, shapeBrush);
        HPEN shapePen = CreatePen(PS_SOLID, 2, RGB(40, 30, 10));
        HGDIOBJ oldPen = SelectObject(hdc, shapePen);
        DrawShape(hdc, cx, cy, size, s.shape);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(shapeBrush);
        DeleteObject(shapePen);

        if (burstActive) {
            HPEN ring = CreatePen(PS_SOLID, 4, RGB(255, 60, 60));
            HGDIOBJ oldRingPen = SelectObject(hdc, ring);
            HGDIOBJ oldRingBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            const int rr = std::max(size, 8) + 20;
            Ellipse(hdc, cx - rr, cy - rr, cx + rr, cy + rr);
            SelectObject(hdc, oldRingBrush);
            SelectObject(hdc, oldRingPen);
            DeleteObject(ring);
        }

        if (HasTag(s, "label") && !s.label.empty()) {
            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(255, 255, 255));
            RECT labelRect{client.left, cy + size / 2 + 8, client.right, cy + size / 2 + 40};
            std::wstring wlabel = Widen(s.label);
            DrawTextW(hdc, wlabel.c_str(), -1, &labelRect, DT_CENTER | DT_SINGLELINE);
        }
    }

    if (HasTag(s, "border")) {
        HPEN borderPen = CreatePen(PS_SOLID, 6, RGB(255, 255, 255));
        HGDIOBJ oldPen = SelectObject(hdc, borderPen);
        HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, client.left, client.top, client.right, client.bottom);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(borderPen);
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255, 255, 255));
    RECT panel{client.left + 12, client.top + 12, client.left + 320, client.top + 220};
    std::wstring panelText = StatusPanelText(s);
    DrawTextW(hdc, panelText.c_str(), -1, &panel, DT_LEFT | DT_NOCLIP);
}

} // namespace demo
