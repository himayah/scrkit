#include "ConfettiFall.h"

#include <algorithm>
#include <cmath>
#include <climits>

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

Vec2 ConfettiPosition(Vec2 rest, float tau, const ConfettiIndividual& individual) {
    const float x = rest.x + individual.swayAmp * std::sin(individual.swayOmega * tau + individual.swayPhase);
    // Approaches terminal velocity individual.fallSpeed exponentially rather
    // than jumping to it immediately (§6.1.10).
    const float y = rest.y + individual.fallSpeed * tau -
                     individual.fallSpeed * (1.0f - std::exp(-3.0f * tau)) / 3.0f;
    return {x, y};
}

void ConfettiFallEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    system_.InitFromCells(ctx.layer->cells);
    const float intensity = ctx.params ? ctx.params->intensity : 0.7f;

    // D-16: delay is normalized by the row range this layer's cells actually
    // occupy, not by gridN, so it scales with real content extent instead of
    // grid resolution.
    const int gridN = layer_->gridN;
    std::vector<int> rows(system_.fragments.size());
    int minRow = INT_MAX, maxRow = INT_MIN;
    for (size_t i = 0; i < system_.fragments.size(); ++i) {
        const int original = i < layer_->cellIndices.size() ? layer_->cellIndices[i] : 0;
        const int row = gridN > 0 ? original / gridN : 0;
        rows[i] = row;
        minRow = std::min(minRow, row);
        maxRow = std::max(maxRow, row);
    }
    const int rowSpan = std::max(1, maxRow - minRow);

    individuals_.resize(system_.fragments.size());
    for (size_t i = 0; i < system_.fragments.size(); ++i) {
        Fragment& f = system_.fragments[i];
        f.delay = (static_cast<float>(rows[i] - minRow) / static_cast<float>(rowSpan)) * params_.totalStaggerSeconds +
                   rng_.NextFloat01() * params_.jitter;

        ConfettiIndividual ind;
        ind.fallSpeed =
            (params_.fallMin + rng_.NextFloat01() * (params_.fallMax - params_.fallMin)) * (0.6f + 0.4f * intensity);
        ind.swayAmp = params_.swayMin + rng_.NextFloat01() * (params_.swayMax - params_.swayMin);
        ind.swayOmega = 2.0f * kPi * (params_.swayHzMin + rng_.NextFloat01() * (params_.swayHzMax - params_.swayHzMin));
        ind.swayPhase = rng_.NextFloat01() * 2.0f * kPi;
        ind.tumbleOmega = 2.0f * kPi * params_.tumbleHz * (0.7f + rng_.NextFloat01() * 0.6f);
        individuals_[i] = ind;
    }
}

void ConfettiFallEffect::Step(EffectFrame& frame) {
    for (size_t i = 0; i < system_.fragments.size(); ++i) {
        Fragment& f = system_.fragments[i];
        if (frame.t < f.delay) {
            f.pos = f.restPos;
            f.rot = 0.0f;
            f.scaleX = f.scaleY = 1.0f;
            f.alive = true;
            continue;
        }
        const float tau = frame.t - f.delay;
        const ConfettiIndividual& ind = individuals_[i];
        f.pos = ConfettiPosition(f.restPos, tau, ind);
        f.scaleX = std::cos(ind.tumbleOmega * tau); // flips through negative values -- the "tumbling" look
        f.scaleY = 1.0f;
        f.rot = 0.3f * std::sin(ind.swayOmega * tau + ind.swayPhase);
        f.alive = f.pos.y < layer_->screenH + 2.0f * layer_->cellHalfH;
    }

    frame.out->quads.clear();
    system_.EmitQuads(layer_->cells, layer_->cellHalfW, layer_->cellHalfH, frame.out->quads);
}

} // namespace core::fx
