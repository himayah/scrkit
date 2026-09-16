#include "NoiseDissolve.h"

#include "../EffectMath.h"
#include "../NoiseField.h"

namespace core::fx {

float DissolveAlpha(float noise01, float tau, float edge) {
    return 1.0f - SmoothStep01((tau - noise01) / edge + 1.0f);
}

void NoiseDissolveEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    system_.InitFromCells(ctx.layer->cells);

    const int gridN = layer_->gridN;
    noiseValues_.resize(system_.fragments.size());
    for (size_t i = 0; i < system_.fragments.size(); ++i) {
        const int original = i < layer_->cellIndices.size() ? layer_->cellIndices[i] : 0;
        const int row = gridN > 0 ? original / gridN : 0;
        const int col = gridN > 0 ? original % gridN : 0;
        noiseValues_[i] =
            0.5f + 0.5f * Fbm3(col * params_.cellFreq, row * params_.cellFreq, 0.0f, ctx.seed);
    }
}

void NoiseDissolveEffect::Step(EffectFrame& frame) {
    const float tau = frame.t / params_.dissolveSeconds;
    for (size_t i = 0; i < system_.fragments.size(); ++i) {
        Fragment& f = system_.fragments[i];
        f.pos = f.restPos; // §6.1.12: alpha-only, no movement
        f.alpha = DissolveAlpha(noiseValues_[i], tau, params_.edge);
        f.scaleX = f.scaleY = 1.0f - params_.shrink * (1.0f - f.alpha);
        f.alive = f.alpha > 0.0f;
    }

    frame.out->quads.clear();
    system_.EmitQuads(layer_->cells, layer_->cellHalfW, layer_->cellHalfH, frame.out->quads);
}

} // namespace core::fx
