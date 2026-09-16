#pragma once
// Stateful envelope helper (DESIGN_EFFECTS.md §6.0.1 / D-13). Deliberately not
// a pure function of absolute time t: EffectStateMachine holds the scalar and
// steps it towards a target each frame, so an Entering phase interrupted by
// RequestTerminal() before reaching Running slides continuously towards 0
// instead of jumping (the old "envelope(t)" formula would have started the
// Exiting curve from a wrong assumed value of 1).

namespace core::fx {

// Advances e towards target at a constant rate of 1/rateSeconds per second,
// clamped so it never overshoots target. rateSeconds <= 0 reaches target
// immediately (still in this single Step call, not before).
inline float StepEnvelope(float e, float dt, float target, float rateSeconds) {
    const float step = dt / (rateSeconds > 1e-3f ? rateSeconds : 1e-3f);
    if (target > e) {
        e += step;
        return e < target ? e : target;
    }
    if (target < e) {
        e -= step;
        return e > target ? e : target;
    }
    return target;
}

} // namespace core::fx
