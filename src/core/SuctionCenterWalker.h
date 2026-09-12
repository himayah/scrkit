#pragma once
// Random-walk motion for the suction center (要件.txt §4).

#include "SpiralMath.h"

namespace core {

// Abstract random number source so tests can inject deterministic sequences
// without depending on <random> engines directly in the interface.
class IRandomSource {
public:
    virtual ~IRandomSource() = default;
    // Returns a value in [0, 1).
    virtual float NextFloat01() = 0;
};

struct WalkerBounds {
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 1920.0f;
    float maxY = 1080.0f;
};

// Moves the suction center with a constant-speed random walk, bouncing off
// the screen edges so it always stays within bounds (要件.txt: 「画面内を
// ランダムウォークで移動」「速度は一定」).
class SuctionCenterWalker {
public:
    SuctionCenterWalker(Vec2 startPosition, WalkerBounds bounds, float speedPxPerFrame);

    // Advances one frame using `rng` to occasionally pick a new heading.
    void Step(IRandomSource& rng);

    const Vec2& Position() const { return position_; }

private:
    Vec2 position_;
    WalkerBounds bounds_;
    float speed_;
    float headingRadians_ = 0.0f;
    int framesUntilRetarget_ = 0;

    void PickNewHeading(IRandomSource& rng);
};

} // namespace core
