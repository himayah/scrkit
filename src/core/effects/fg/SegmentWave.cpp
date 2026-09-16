#include "SegmentWave.h"

#include <algorithm>
#include <cmath>

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;

// Bundles what FragmentIntegrator's single void* can carry (§6.1.14 needs
// screenW alongside the effect's own params).
struct IntegrateParams {
    SegmentWaveParams params;
    float screenW;
};

void Integrate(Fragment& f, const FragmentGroup*, const EffectFrame& frame, const void* paramsVoid) {
    const auto& ip = *static_cast<const IntegrateParams*>(paramsVoid);
    const int band = f.group; // repurposed as a plain band-index label, not a FragmentGroup lookup key
    const float sign = ip.params.alternate ? ((band % 2 == 0) ? 1.0f : -1.0f) : 1.0f;
    const float dx = frame.intensity * ip.params.ampRatio * ip.screenW * sign *
                      std::sin(2.0f * kPi * ip.params.hz * frame.t - ip.params.phaseStep * band);
    f.pos = {f.restPos.x + dx, f.restPos.y};
}
} // namespace

void SegmentWaveEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    system_.InitFromCells(ctx.layer->cells);
    for (size_t i = 0; i < system_.fragments.size(); ++i) {
        const int originalIndex = i < layer_->cellIndices.size() ? layer_->cellIndices[i] : 0;
        const int row = layer_->gridN > 0 ? originalIndex / layer_->gridN : 0;
        system_.fragments[i].group = row / std::max(1, params_.bandRows);
    }
}

void SegmentWaveEffect::Step(EffectFrame& frame) {
    IntegrateParams ip{params_, layer_->screenW};
    system_.StepAll(&Integrate, frame, &ip);
    frame.out->quads.clear();
    system_.EmitQuads(layer_->cells, layer_->cellHalfW, layer_->cellHalfH, frame.out->quads);
}

} // namespace core::fx
