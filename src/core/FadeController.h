#pragma once
// Black -> background image fade (要件.txt §4 step 5: alpha = 0 -> 1).

namespace core {

class FadeController {
public:
    explicit FadeController(float durationSeconds) : durationSeconds_(durationSeconds) {}

    void Reset() { elapsedSeconds_ = 0.0f; }

    // Advances the fade by dt seconds and returns the current alpha in [0,1].
    float Step(float dtSeconds) {
        elapsedSeconds_ += dtSeconds;
        if (elapsedSeconds_ < 0.0f) elapsedSeconds_ = 0.0f;
        return Alpha();
    }

    float Alpha() const {
        if (durationSeconds_ <= 0.0f) return 1.0f;
        float a = elapsedSeconds_ / durationSeconds_;
        if (a < 0.0f) a = 0.0f;
        if (a > 1.0f) a = 1.0f;
        return a;
    }

    bool IsComplete() const { return Alpha() >= 1.0f; }

private:
    float durationSeconds_;
    float elapsedSeconds_ = 0.0f;
};

} // namespace core
