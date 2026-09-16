#include "GlitchShift.h"

#include <algorithm>

#include "../CoverScale.h"

namespace core::fx {

std::vector<Band> MakeGlitchBands(core::IRandomSource& rng, float screenH, const GlitchShiftParams& params,
                                   float intensity, float screenW) {
    std::vector<Band> bands;
    const int n = params.bandsMin + static_cast<int>(rng.NextFloat01() * (params.bandsMax - params.bandsMin + 1));
    float y = 0.0f;
    for (int k = 0; k < n && y < screenH; ++k) {
        const float fillerH = std::min(rng.NextFloat01() * 0.05f * screenH, screenH - y);
        if (fillerH > 0.0f) {
            bands.push_back({y, y + fillerH, 0.0f});
            y += fillerH;
        }
        const float activeH = std::min((0.02f + rng.NextFloat01() * 0.13f) * screenH, screenH - y);
        if (activeH > 0.0f) {
            const float dx = intensity * params.shiftRatio * screenW * (rng.NextFloat01() * 2.0f - 1.0f);
            bands.push_back({y, y + activeH, dx});
            y += activeH;
        }
    }
    if (y < screenH) bands.push_back({y, screenH, 0.0f}); // trailing filler closes the partition exactly at H
    return bands;
}

void GlitchShiftEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    inBurst_ = false;
    nextBurstTime_ = params_.burstGapMin + rng_.NextFloat01() * (params_.burstGapMax - params_.burstGapMin);
    lastRebandTime_ = -1.0f;
    bands_ = {{0.0f, ctx.screenH, 0.0f}};
}

void GlitchShiftEffect::Step(EffectFrame& frame) {
    if (!inBurst_ && frame.t >= nextBurstTime_) {
        inBurst_ = true;
        burstEndTime_ =
            frame.t + (params_.burstLenMin + rng_.NextFloat01() * (params_.burstLenMax - params_.burstLenMin));
        lastRebandTime_ = -1.0f;
    }
    if (inBurst_ && frame.t >= burstEndTime_) {
        inBurst_ = false;
        nextBurstTime_ = frame.t + (params_.burstGapMin + rng_.NextFloat01() * (params_.burstGapMax - params_.burstGapMin));
        bands_ = {{0.0f, layer_->screenH, 0.0f}};
    }
    if (inBurst_ && (lastRebandTime_ < 0.0f || frame.t - lastRebandTime_ >= params_.rebandEvery)) {
        bands_ = MakeGlitchBands(rng_, layer_->screenH, params_, frame.intensity, layer_->screenW);
        lastRebandTime_ = frame.t;
    }

    frame.out->quads.clear();
    for (const auto& band : bands_) {
        const float v0 = band.y0 / layer_->screenH;
        const float v1 = band.y1 / layer_->screenH;
        frame.out->quads.push_back({{band.dx, band.y0}, {0.0f, v0}, 1.0f, 1.0f});
        frame.out->quads.push_back({{band.dx + layer_->screenW, band.y0}, {1.0f, v0}, 1.0f, 1.0f});
        frame.out->quads.push_back({{band.dx + layer_->screenW, band.y1}, {1.0f, v1}, 1.0f, 1.0f});
        frame.out->quads.push_back({{band.dx, band.y1}, {0.0f, v1}, 1.0f, 1.0f});
    }

    Transform2D transform;
    transform.pivot = {layer_->screenW / 2.0f, layer_->screenH / 2.0f};
    const float maxShift = frame.intensity * (params_.shiftRatio * layer_->screenW + params_.rgbSplitPx);
    transform.scale = CoverScaleForShift(layer_->screenW, layer_->screenH, maxShift, 0.0f);
    frame.out->transform = transform;
    frame.out->bandsRgbSplitPx = inBurst_ ? frame.intensity * params_.rgbSplitPx : 0.0f;
}

} // namespace core::fx
