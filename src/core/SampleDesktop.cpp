#include "SampleDesktop.h"

#include <algorithm>

namespace core {

namespace {

struct Canvas {
    std::vector<uint8_t>& px;
    int w, h;

    void Fill(int x0, int y0, int x1, int y1, uint32_t rgb) {
        x0 = std::clamp(x0, 0, w);
        x1 = std::clamp(x1, 0, w);
        y0 = std::clamp(y0, 0, h);
        y1 = std::clamp(y1, 0, h);
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) {
                uint8_t* p = &px[(static_cast<size_t>(y) * w + x) * 4];
                p[0] = static_cast<uint8_t>(rgb >> 16);
                p[1] = static_cast<uint8_t>(rgb >> 8);
                p[2] = static_cast<uint8_t>(rgb);
                p[3] = 255;
            }
        }
    }
};

int At(float fraction, int total) { return static_cast<int>(fraction * static_cast<float>(total) + 0.5f); }

} // namespace

SampleDesktop MakeSampleDesktop(const uint8_t* wallpaperRgba, int width, int height) {
    SampleDesktop out;
    if (!wallpaperRgba || width <= 0 || height <= 0) return out;
    out.rgba.assign(wallpaperRgba, wallpaperRgba + static_cast<size_t>(width) * height * 4);
    for (size_t i = 3; i < out.rgba.size(); i += 4) out.rgba[i] = 255;
    Canvas c{out.rgba, width, height};

    // Desktop icons: a column at the left, each a colored tile with a label bar under it.
    const uint32_t tiles[] = {0x4c8bd6, 0xe0a34a, 0x6cc070, 0xc26fb8, 0x8a8f98};
    const int tile = std::max(8, At(0.05f, height));
    for (int i = 0; i < 5; ++i) {
        const int x = At(0.025f, width);
        const int y = At(0.05f, height) + i * At(0.13f, height);
        c.Fill(x, y, x + tile, y + tile, tiles[i]);
        c.Fill(x - tile / 6, y + tile + tile / 5, x + tile + tile / 6, y + tile + tile / 5 + std::max(2, tile / 6), 0xf2f2f2);
    }

    // Window A: a light settings window -- near-white body, the worst case for a pixel diff.
    const PixelRect a{At(0.12f, width), At(0.10f, height), At(0.42f, width), At(0.62f, height)};
    const int titleH = std::max(6, At(0.035f, height));
    c.Fill(a.left, a.top, a.right, a.bottom, 0xf9f9f9);
    c.Fill(a.left, a.top, a.right, a.top + titleH, 0xeaeaea);
    const int line = std::max(2, At(0.012f, height));
    for (int i = 0; i < 6; ++i) {
        const int y = a.top + titleH + At(0.04f, height) + i * At(0.06f, height);
        const int len = (a.right - a.left) * (i % 3 == 0 ? 60 : (i % 3 == 1 ? 80 : 45)) / 100;
        c.Fill(a.left + At(0.02f, width), y, a.left + At(0.02f, width) + len, y + line, i % 2 ? 0xdcdce0 : 0xc8c8ce);
    }
    c.Fill(a.left + At(0.02f, width), a.bottom - At(0.09f, height), a.left + At(0.09f, width), a.bottom - At(0.04f, height), 0x0067c0);

    // Window B: a dark terminal.
    const PixelRect b{At(0.46f, width), At(0.22f, height), At(0.80f, width), At(0.78f, height)};
    c.Fill(b.left, b.top, b.right, b.bottom, 0x0c0c0c);
    c.Fill(b.left, b.top, b.right, b.top + titleH, 0x2d2d2d);
    for (int i = 0; i < 9; ++i) {
        const int y = b.top + titleH + At(0.03f, height) + i * At(0.05f, height);
        const int len = (b.right - b.left) * (30 + (i * 37) % 55) / 100;
        c.Fill(b.left + At(0.015f, width), y, b.left + At(0.015f, width) + len, y + line, i % 4 == 0 ? 0x66d17a : 0xcccccc);
    }

    // Taskbar across the bottom.
    const PixelRect bar{0, height - std::max(8, At(0.045f, height)), width, height};
    c.Fill(bar.left, bar.top, bar.right, bar.bottom, 0x1f1f1f);
    const int slot = (bar.bottom - bar.top) * 6 / 10;
    for (int i = 0; i < 6; ++i) {
        const int x = width / 2 - 3 * (slot + slot / 2) + i * (slot + slot / 2);
        c.Fill(x, bar.top + (bar.bottom - bar.top - slot) / 2, x + slot, bar.top + (bar.bottom - bar.top - slot) / 2 + slot, i == 2 ? 0x60cdff : 0x4a4a4a);
    }

    out.windows = {a, b, bar};
    return out;
}

} // namespace core
