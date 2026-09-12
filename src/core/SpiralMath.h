#pragma once
// Platform-independent spiral suction math (要件.txt §4, §7).
// No Windows / OpenGL headers here so this module can be unit-tested on any OS.

namespace core {

// Parameters controlling how fast an object spirals into the suction center.
// Two presets are used in practice: a "normal" one for icons/windows (few
// objects), and a lighter one for background particles (many objects) per
// 要件.txt §7 (θ += 0.1, r -= 0.5 など軽量化).
struct SpiralParams {
    float dTheta = 0.15f;      // angular step per frame (radians)
    float suctionSpeed = 2.0f; // radial shrink per frame (pixels)
};

inline SpiralParams NormalSpiralParams() { return SpiralParams{0.15f, 2.0f}; }
inline SpiralParams LightweightSpiralParams() { return SpiralParams{0.1f, 0.5f}; }

// The evolving state of a single object being sucked in.
struct SpiralState {
    float theta = 0.0f; // current angle (radians)
    float r = 0.0f;     // current distance from the suction center
    bool alive = true;  // false once r has reached 0 (object consumed)
};

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;
};

// Initializes a SpiralState so that the object starts exactly at
// (startX, startY) relative to (centerX, centerY).
SpiralState MakeSpiralState(float startX, float startY, float centerX, float centerY);

// Advances one frame: θ += dTheta; r -= suctionSpeed; marks dead when r <= 0.
// Returns the new absolute position (center + polar offset). Once dead, the
// returned position is exactly the center and further calls are no-ops.
Vec2 StepSpiral(SpiralState& state, const SpiralParams& params, float centerX, float centerY);

} // namespace core
