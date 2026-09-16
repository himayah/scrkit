#include "TileMapper.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace core::fx {

namespace {
float WrapScalar(float v, float period) {
    float r = std::fmod(v, period);
    if (r < 0.0f) r += period;
    return r;
}
} // namespace

Vec2 WrapPosition(Vec2 p, float periodW, float periodH) {
    return {WrapScalar(p.x, periodW), WrapScalar(p.y, periodH)};
}

std::vector<Vec2> EdgeDuplicates(Vec2 pWrapped, float halfW, float halfH, float screenW, float screenH) {
    std::vector<Vec2> offsets;
    std::optional<float> xOff, yOff;
    if (pWrapped.x < halfW) xOff = screenW;
    else if (pWrapped.x > screenW - halfW) xOff = -screenW;
    if (pWrapped.y < halfH) yOff = screenH;
    else if (pWrapped.y > screenH - halfH) yOff = -screenH;

    if (xOff) offsets.push_back({*xOff, 0.0f});
    if (yOff) offsets.push_back({0.0f, *yOff});
    if (xOff && yOff) offsets.push_back({*xOff, *yOff});
    return offsets;
}

namespace {
Vec2 ApplyTransform(const Transform2D& t, Vec2 p) {
    const float dx = (p.x - t.pivot.x) * t.scale;
    const float dy = (p.y - t.pivot.y) * t.scale;
    const float c = std::cos(t.rotateRad), s = std::sin(t.rotateRad);
    const float rx = dx * c - dy * s;
    const float ry = dx * s + dy * c;
    return {t.pivot.x + rx + t.translate.x, t.pivot.y + ry + t.translate.y};
}
} // namespace

std::vector<std::pair<int, int>> VisibleTileOffsets(const Transform2D& transform, float tileW, float tileH,
                                                     float screenW, float screenH) {
    const float longSide = std::max(screenW, screenH);
    const float shortSide = std::min(screenW, screenH);
    const bool sufficient3x3 = shortSide >= longSide / std::sqrt(8.0f);
    const int range = sufficient3x3 ? 1 : 2;

    std::vector<std::pair<int, int>> result;
    for (int i = -range; i <= range; ++i) {
        for (int j = -range; j <= range; ++j) {
            const float ox = i * tileW;
            const float oy = j * tileH;
            const Vec2 corners[4] = {
                {ox, oy}, {ox + tileW, oy}, {ox + tileW, oy + tileH}, {ox, oy + tileH}};
            float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
            for (const Vec2& corner : corners) {
                const Vec2 p = ApplyTransform(transform, corner);
                minX = std::min(minX, p.x);
                maxX = std::max(maxX, p.x);
                minY = std::min(minY, p.y);
                maxY = std::max(maxY, p.y);
            }
            // Strict inequalities: two rects that only touch at a shared edge
            // (zero-area overlap) don't count as visible -- otherwise every
            // grid-aligned neighbor of the identity transform would qualify.
            const bool intersects = maxX > 0.0f && minX < screenW && maxY > 0.0f && minY < screenH;
            if (intersects) result.emplace_back(i, j);
        }
    }
    return result;
}

} // namespace core::fx
