#include "InfiniteRotation.h"

#include "../QuadEmit.h"
#include "../TileMapper.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

void InfiniteRotationEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    omegaSign_ = rng_.NextFloat01() < 0.5f ? 1.0f : -1.0f;
    theta_ = 0.0f;
}

void InfiniteRotationEffect::Step(EffectFrame& frame) {
    const float omega = omegaSign_ * 2.0f * kPi / params_.secondsPerTurn;
    theta_ += frame.intensity * omega * frame.dt;

    Transform2D transform;
    transform.pivot = {layer_->screenW / 2.0f, layer_->screenH / 2.0f};
    transform.rotateRad = theta_;
    transform.scale = 1.0f;
    frame.out->transform = transform;

    frame.out->quads.clear();
    const auto offsets = VisibleTileOffsets(transform, layer_->screenW, layer_->screenH, layer_->screenW,
                                             layer_->screenH);
    for (const auto& offset : offsets) {
        const float ox = offset.first * layer_->screenW;
        const float oy = offset.second * layer_->screenH;
        for (const auto& cell : layer_->cells) {
            EmitAxisAlignedQuad({cell.x + ox, cell.y + oy}, layer_->cellHalfW, layer_->cellHalfH, cell.u0, cell.v0,
                                 cell.u1, cell.v1, 1.0f, 1.0f, frame.out->quads);
        }
    }
}

} // namespace core::fx
