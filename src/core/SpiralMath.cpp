#include "SpiralMath.h"

#include <cmath>

namespace core {

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

    state.theta += params.dTheta;
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
