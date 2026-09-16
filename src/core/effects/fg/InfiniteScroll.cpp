#include "InfiniteScroll.h"

#include <algorithm>
#include <cmath>

#include "../EffectMath.h"
#include "../QuadEmit.h"
#include "../TileMapper.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
// §6.1.3: {0, 90, 180, 270, +-45} degrees.
constexpr float kDirectionsDeg[] = {0.0f, 90.0f, 180.0f, 270.0f, 45.0f, -45.0f};
} // namespace

Vec2 ScrollOffsetStep(Vec2 offset, Vec2 velocity, float dt) {
    return {offset.x + velocity.x * dt, offset.y + velocity.y * dt};
}

Vec2 ScrollRestTarget(Vec2 offset, float screenW, float screenH) {
    return {std::round(offset.x / screenW) * screenW, std::round(offset.y / screenH) * screenH};
}

void InfiniteScrollEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    const int pick = static_cast<int>(rng_.NextFloat01() * 6.0f) % 6;
    const float rad = kDirectionsDeg[pick] * kPi / 180.0f;
    direction_ = {std::cos(rad), std::sin(rad)};
    offset_ = {0.0f, 0.0f};
    exiting_ = false;
}

void InfiniteScrollEffect::RequestExit(float exitSeconds) {
    exiting_ = true;
    exitStartOffset_ = offset_;
    restTarget_ = ScrollRestTarget(offset_, layer_->screenW, layer_->screenH);
    exitElapsed_ = 0.0f;
    exitDuration_ = exitSeconds; // refined to the actual travel time on the first Step() below, using the live speed
}

void InfiniteScrollEffect::Step(EffectFrame& frame) {
    const float speedScalar = frame.intensity * params_.speedRatio * std::max(layer_->screenW, layer_->screenH);
    const Vec2 velocity = {speedScalar * direction_.x, speedScalar * direction_.y};

    if (exiting_) {
        if (exitElapsed_ == 0.0f) {
            // §6.1.3: 所要 = min(transitionSeconds, |残り|/|v|), evaluated once
            // at the live speed when the slide actually starts (RequestExit()
            // itself may run before this frame's intensity/envelope is known).
            const float remaining =
                std::hypot(restTarget_.x - exitStartOffset_.x, restTarget_.y - exitStartOffset_.y);
            const float speed = std::hypot(velocity.x, velocity.y);
            if (speed > 1e-3f) exitDuration_ = std::min(exitDuration_, remaining / speed);
        }
        exitElapsed_ += frame.dt;
        const float u = exitDuration_ > 1e-4f ? SmoothStep01(std::min(1.0f, exitElapsed_ / exitDuration_)) : 1.0f;
        offset_ = {Lerp(exitStartOffset_.x, restTarget_.x, u), Lerp(exitStartOffset_.y, restTarget_.y, u)};
    } else {
        offset_ = ScrollOffsetStep(offset_, velocity, frame.dt);
    }

    frame.out->quads.clear();
    for (const auto& cell : layer_->cells) {
        const Vec2 wrapped =
            WrapPosition({cell.x + offset_.x, cell.y + offset_.y}, layer_->screenW, layer_->screenH);
        EmitAxisAlignedQuad(wrapped, layer_->cellHalfW, layer_->cellHalfH, cell.u0, cell.v0, cell.u1, cell.v1, 1.0f,
                             1.0f, frame.out->quads);
        for (const Vec2& dup :
             EdgeDuplicates(wrapped, layer_->cellHalfW, layer_->cellHalfH, layer_->screenW, layer_->screenH)) {
            EmitAxisAlignedQuad({wrapped.x + dup.x, wrapped.y + dup.y}, layer_->cellHalfW, layer_->cellHalfH,
                                 cell.u0, cell.v0, cell.u1, cell.v1, 1.0f, 1.0f, frame.out->quads);
        }
    }
}

} // namespace core::fx
