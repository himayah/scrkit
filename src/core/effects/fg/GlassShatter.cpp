#include "GlassShatter.h"

#include <algorithm>
#include <cmath>

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

std::vector<int> AssignShards(const std::vector<Vec2>& cellPos, const std::vector<Vec2>& seeds) {
    std::vector<int> result(cellPos.size(), 0);
    for (size_t i = 0; i < cellPos.size(); ++i) {
        int best = 0;
        float bestDistSq = 1e30f;
        for (size_t k = 0; k < seeds.size(); ++k) {
            const float dx = cellPos[i].x - seeds[k].x;
            const float dy = cellPos[i].y - seeds[k].y;
            const float d = dx * dx + dy * dy;
            if (d < bestDistSq) {
                bestDistSq = d;
                best = static_cast<int>(k);
            }
        }
        result[i] = best;
    }
    return result;
}

void GlassShatterEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    system_.InitFromCells(ctx.layer->cells);

    const float intensity = ctx.params ? ctx.params->intensity : 0.7f;
    const Vec2 impact{rng_.NextFloat01() * ctx.screenW, rng_.NextFloat01() * ctx.screenH};

    const int shardCount =
        params_.shardsMin + static_cast<int>(rng_.NextFloat01() * (params_.shardsMax - params_.shardsMin + 1));
    std::vector<Vec2> seeds(static_cast<size_t>(shardCount));
    for (auto& s : seeds) s = {rng_.NextFloat01() * ctx.screenW, rng_.NextFloat01() * ctx.screenH};

    std::vector<Vec2> cellPos(system_.fragments.size());
    for (size_t i = 0; i < cellPos.size(); ++i) cellPos[i] = system_.fragments[i].restPos;
    const std::vector<int> assignment = AssignShards(cellPos, seeds);

    system_.groups.assign(static_cast<size_t>(shardCount), FragmentGroup{});
    groupRestCentroids_.assign(static_cast<size_t>(shardCount), Vec2{0.0f, 0.0f});
    std::vector<int> counts(static_cast<size_t>(shardCount), 0);
    for (size_t i = 0; i < assignment.size(); ++i) {
        system_.fragments[i].group = assignment[i];
        groupRestCentroids_[static_cast<size_t>(assignment[i])].x += cellPos[i].x;
        groupRestCentroids_[static_cast<size_t>(assignment[i])].y += cellPos[i].y;
        counts[static_cast<size_t>(assignment[i])]++;
    }
    for (int k = 0; k < shardCount; ++k) {
        if (counts[static_cast<size_t>(k)] > 0) {
            groupRestCentroids_[static_cast<size_t>(k)].x /= counts[static_cast<size_t>(k)];
            groupRestCentroids_[static_cast<size_t>(k)].y /= counts[static_cast<size_t>(k)];
        } else {
            groupRestCentroids_[static_cast<size_t>(k)] = seeds[static_cast<size_t>(k)]; // no cell ended up nearest this seed
        }

        FragmentGroup& g = system_.groups[static_cast<size_t>(k)];
        g.centroid = groupRestCentroids_[static_cast<size_t>(k)];
        g.rot = 0.0f;
        const float dx = g.centroid.x - impact.x;
        const float dy = g.centroid.y - impact.y;
        g.delay = std::sqrt(dx * dx + dy * dy) / params_.propagation;
        const float kick =
            (params_.kickMin + rng_.NextFloat01() * (params_.kickMax - params_.kickMin)) * (0.5f + 0.5f * intensity);
        g.vel = {(rng_.NextFloat01() * 2.0f - 1.0f) * kick, -0.3f * kick};
        g.angVel = (rng_.NextFloat01() * 2.0f - 1.0f) * params_.spinMax;
    }
}

void GlassShatterEffect::Step(EffectFrame& frame) {
    for (auto& g : system_.groups) {
        if (frame.t >= g.delay) {
            g.vel.y += params_.gravity * frame.dt;
            g.centroid.x += g.vel.x * frame.dt;
            g.centroid.y += g.vel.y * frame.dt;
            g.rot += g.angVel * frame.dt;
        }
    }

    for (auto& f : system_.fragments) {
        if (f.group < 0 || static_cast<size_t>(f.group) >= system_.groups.size()) continue;
        const FragmentGroup& g = system_.groups[static_cast<size_t>(f.group)];
        const Vec2& restCentroid = groupRestCentroids_[static_cast<size_t>(f.group)];

        if (frame.t < g.delay) {
            // §6.1.9's crack-flash pulse in the last 0.08s before delay,
            // static rest pose otherwise (§4.7 common pre-delay contract).
            if (frame.t >= g.delay - 0.08f) {
                const float u = (frame.t - (g.delay - 0.08f)) / 0.08f;
                f.scaleX = f.scaleY = 1.0f + 0.05f * std::sin(kPi * u);
            } else {
                f.scaleX = f.scaleY = 1.0f;
            }
            f.pos = f.restPos;
            f.rot = 0.0f;
            f.alive = true;
        } else {
            const float dx = f.restPos.x - restCentroid.x;
            const float dy = f.restPos.y - restCentroid.y;
            const float c = std::cos(g.rot), s = std::sin(g.rot);
            f.pos = {g.centroid.x + dx * c - dy * s, g.centroid.y + dx * s + dy * c};
            f.rot = g.rot;
            f.scaleX = f.scaleY = 1.0f;
            const float distFromCentroid = std::sqrt(dx * dx + dy * dy);
            const float margin = 2.0f * layer_->cellHalfH * std::max(1.0f, distFromCentroid / layer_->cellHalfH);
            f.alive = f.pos.y < layer_->screenH + margin;
        }
    }

    frame.out->quads.clear();
    system_.EmitQuads(layer_->cells, layer_->cellHalfW, layer_->cellHalfH, frame.out->quads);
}

} // namespace core::fx
