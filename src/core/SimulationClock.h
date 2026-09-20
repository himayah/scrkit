#pragma once
// Turns real frame time into simulated frame time for external control
// (scrapi.paused / scrapi.timeScale / scrapi.step, docs/SCRAPI_SPEC.md §6).
// Pure logic; the saver's normal /s path never uses it (it always advances by the
// real dt).

#include <algorithm>

namespace core {

class SimulationClock {
public:
    void SetPaused(bool paused) { paused_ = paused; }
    void SetTimeScale(float scale) { timeScale_ = std::clamp(scale, 0.0f, kMaxTimeScale); }
    // While paused, the next Advance() yields exactly one fixed step, once.
    void RequestStep() { stepPending_ = true; }

    bool paused() const { return paused_; }
    float timeScale() const { return timeScale_; }

    // The dt the simulation should advance by this frame.
    float Advance(float realDt) {
        if (paused_) {
            if (!stepPending_) return 0.0f;
            stepPending_ = false;
            return kStepSeconds;
        }
        stepPending_ = false; // a step requested while running is meaningless; don't bank it
        return realDt * timeScale_;
    }

    static constexpr float kStepSeconds = 1.0f / 60.0f;
    static constexpr float kMaxTimeScale = 8.0f;

private:
    bool paused_ = false;
    float timeScale_ = 1.0f;
    bool stepPending_ = false;
};

} // namespace core
