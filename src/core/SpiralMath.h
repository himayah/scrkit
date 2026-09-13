#pragma once
// Platform-independent spiral suction math (要件.txt §4, §7).
// No Windows / OpenGL headers here so this module can be unit-tested on any OS.

namespace core {

// Parameters controlling how fast an object spirals into the suction center.
// Two presets are used in practice: a "normal" one (also the fallback used
// by MakeParamsForRevolutions below), and a lighter one for background
// particles (many objects) per 要件.txt §7 (θ += 0.1, r -= 0.5 など軽量化).
struct SpiralParams {
    float dTheta = 0.15f;      // angular step per frame (radians)
    float suctionSpeed = 2.0f; // radial shrink per frame (pixels)

    // Extra angular speed added as the object nears the center, so the
    // spiral visibly tightens/warps inward instead of keeping a constant
    // angular rate all the way in. 0 (the default) reproduces the original
    // constant-ω behavior. When positive, StepSpiral adds
    // `centerAccelFactor / max(r, 1)` to dTheta each frame, then clamps the
    // total to kMaxDTheta so it stays a smooth-looking spiral rather than
    // spinning multiple full turns in one frame as r approaches 0.
    float centerAccelFactor = 0.0f;
};

inline SpiralParams NormalSpiralParams() { return SpiralParams{0.15f, 2.0f, 0.0f}; }
inline SpiralParams LightweightSpiralParams() { return SpiralParams{0.1f, 0.5f, 0.0f}; }

// Same as LightweightSpiralParams, but with center-approach acceleration
// enabled -- used for the background particle phase so the image visibly
// warps into a tighter spiral as it's sucked in (要件.txt §4 の追加要望:
// 中心に近づくほど角速度を上げてらせん状に歪める).
inline SpiralParams VortexSpiralParams(float centerAccelFactor = 40.0f) {
    return SpiralParams{0.1f, 0.5f, centerAccelFactor};
}

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

// Returns SpiralParams (centerAccelFactor = 0, constant angular speed) whose
// dTheta is derived from `r0` (the object's starting distance from the
// suction center) and `suctionSpeed` so the spiral completes almost exactly
// `targetRevolutions` full turns by the time r reaches 0 -- i.e.
// dTheta * (r0 / suctionSpeed) == targetRevolutions * 2π. This makes the
// number of revolutions roughly the same regardless of how far an object
// happens to start from the (randomly wandering) center, rather than
// varying wildly with r0 under a single fixed dTheta (追加要望: らせん回転を
// もっと緩やかにし、中心に吸い込まれるまでに3〜5周回するくらいにする -- callers
// typically randomize targetRevolutions per object within [3,5) for organic
// variety). Falls back to NormalSpiralParams().dTheta when r0 is small
// enough that the object would be consumed in about a frame anyway, where a
// derived dTheta would be meaninglessly large.
SpiralParams MakeParamsForRevolutions(float r0, float suctionSpeed, float targetRevolutions);

// Advances one frame: θ += dTheta (+ extra spin from centerAccelFactor as r
// shrinks); r -= suctionSpeed; marks dead when r <= 0. Returns the new
// absolute position (center + polar offset). Once dead, the returned
// position is exactly the center and further calls are no-ops.
Vec2 StepSpiral(SpiralState& state, const SpiralParams& params, float centerX, float centerY);

} // namespace core
