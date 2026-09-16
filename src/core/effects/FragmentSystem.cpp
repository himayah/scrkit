#include "FragmentSystem.h"

#include <cmath>

namespace core::fx {

void FragmentSystem::InitFromCells(const std::vector<core::Particle>& cells) {
    fragments.assign(cells.size(), Fragment{});
    for (size_t i = 0; i < cells.size(); ++i) {
        Fragment& f = fragments[i];
        f.cellIndex = static_cast<int>(i);
        f.restPos = {cells[i].x, cells[i].y};
        f.pos = f.restPos;
    }
}

void FragmentSystem::StepGroups(GroupIntegrator integrator, const EffectFrame& frame, const void* params) {
    for (auto& g : groups) integrator(g, frame, params);
}

void FragmentSystem::StepAll(FragmentIntegrator integrator, const EffectFrame& frame, const void* params) {
    for (auto& f : fragments) {
        if (!f.alive) continue;
        const FragmentGroup* g = (f.group >= 0 && static_cast<size_t>(f.group) < groups.size())
                                      ? &groups[static_cast<size_t>(f.group)]
                                      : nullptr;
        integrator(f, g, frame, params);
    }
}

bool FragmentSystem::AllDead() const {
    for (const auto& f : fragments) {
        if (f.alive) return false;
    }
    return true;
}

void FragmentSystem::EmitQuads(const std::vector<core::Particle>& cells, float cellHalfW, float cellHalfH,
                                std::vector<QuadVertex>& outQuads) const {
    for (const auto& f : fragments) {
        if (!f.alive) continue;
        if (f.cellIndex < 0 || static_cast<size_t>(f.cellIndex) >= cells.size()) continue;
        const core::Particle& cell = cells[static_cast<size_t>(f.cellIndex)];

        const float cosR = std::cos(f.rot), sinR = std::sin(f.rot);
        const Vec2 localCorners[4] = {
            {-cellHalfW, -cellHalfH}, {cellHalfW, -cellHalfH}, {cellHalfW, cellHalfH}, {-cellHalfW, cellHalfH}};
        const Vec2 uvCorners[4] = {{cell.u0, cell.v0}, {cell.u1, cell.v0}, {cell.u1, cell.v1}, {cell.u0, cell.v1}};

        for (int i = 0; i < 4; ++i) {
            const float sx = localCorners[i].x * f.scaleX;
            const float sy = localCorners[i].y * f.scaleY;
            QuadVertex qv;
            qv.pos = {f.pos.x + sx * cosR - sy * sinR, f.pos.y + sx * sinR + sy * cosR};
            qv.uv = uvCorners[i];
            qv.alpha = f.alpha;
            qv.shade = f.shade;
            outQuads.push_back(qv);
        }
    }
}

} // namespace core::fx
