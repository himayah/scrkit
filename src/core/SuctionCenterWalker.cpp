#include "SuctionCenterWalker.h"

#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kMinFramesBetweenRetarget = 30;
constexpr int kMaxFramesBetweenRetarget = 90;
} // namespace

namespace core {

SuctionCenterWalker::SuctionCenterWalker(Vec2 startPosition, WalkerBounds bounds, float speedPxPerFrame)
    : position_(startPosition), bounds_(bounds), speed_(speedPxPerFrame) {}

void SuctionCenterWalker::PickNewHeading(IRandomSource& rng) {
    headingRadians_ = rng.NextFloat01() * 2.0f * kPi;
    const int span = kMaxFramesBetweenRetarget - kMinFramesBetweenRetarget;
    framesUntilRetarget_ = kMinFramesBetweenRetarget + static_cast<int>(rng.NextFloat01() * span);
}

void SuctionCenterWalker::Step(IRandomSource& rng) {
    if (framesUntilRetarget_ <= 0) {
        PickNewHeading(rng);
    }
    --framesUntilRetarget_;

    float nx = position_.x + std::cos(headingRadians_) * speed_;
    float ny = position_.y + std::sin(headingRadians_) * speed_;

    // Reflect off the edges so the center always stays within bounds
    // (要件.txt: 画面内をランダムウォークで移動する).
    if (nx < bounds_.minX) {
        nx = bounds_.minX;
        headingRadians_ = kPi - headingRadians_;
    } else if (nx > bounds_.maxX) {
        nx = bounds_.maxX;
        headingRadians_ = kPi - headingRadians_;
    }

    if (ny < bounds_.minY) {
        ny = bounds_.minY;
        headingRadians_ = -headingRadians_;
    } else if (ny > bounds_.maxY) {
        ny = bounds_.maxY;
        headingRadians_ = -headingRadians_;
    }

    position_.x = nx;
    position_.y = ny;
}

} // namespace core
