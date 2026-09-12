#include "SpiralMath.h"

#include <algorithm>
#include <cmath>

namespace core {

namespace {
// Upper bound on the per-frame angular step once centerAccelFactor is added
// in, regardless of how small r gets. Without this, r -> 0 would make the
// object spin an unbounded number of full turns in a single frame, which
// reads as noise rather than a tightening spiral.
constexpr float kMaxDTheta = 1.2f;
} // namespace

SpiralState MakeSpiralState(float startX, float startY, float centerX, float centerY) {
    SpiralState s;
    const float dx = startX - centerX;
    const float dy = startY - centerY;
    s.r = std::sqrt(dx * dx + dy * dy);
    s.theta = std::atan2(dy, dx);
    s.alive = true;
    return s;
}

Vec2 StepSpiral(SpiralState& state, const SpiralParams& params, float centerX, float centerY) {
    if (!state.alive) {
        return Vec2{centerX, centerY};
    }

    float dTheta = params.dTheta;
    if (params.centerAccelFactor > 0.0f) {
        // The nearer the object is to the center (smaller r), the larger
        // this term gets, so angular speed ramps up as it's sucked in
        // (要件.txt 追加要望: 中心に近づくほど角速度を上げてらせん状に歪める).
        dTheta += params.centerAccelFactor / std::max(state.r, 1.0f);
        dTheta = std::min(dTheta, kMaxDTheta);
    }

    state.theta += dTheta;
    state.r -= params.suctionSpeed;

    if (state.r <= 0.0f) {
        state.r = 0.0f;
        state.alive = false;
        return Vec2{centerX, centerY};
    }

    return Vec2{centerX + state.r * std::cos(state.theta),
                centerY + state.r * std::sin(state.theta)};
}

} // namespace core
