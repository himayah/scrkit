#include "test_framework.h"

#include "../src/core/SuctionCenterWalker.h"

using core::IRandomSource;
using core::SuctionCenterWalker;
using core::Vec2;
using core::WalkerBounds;

namespace {
// Deterministic sequence for reproducible tests.
class FixedSequenceRandom : public IRandomSource {
public:
    explicit FixedSequenceRandom(std::vector<float> values) : values_(std::move(values)) {}
    float NextFloat01() override {
        float v = values_[index_ % values_.size()];
        ++index_;
        return v;
    }

private:
    std::vector<float> values_;
    size_t index_ = 0;
};
} // namespace

TEST_CASE(SuctionCenterWalker_StaysWithinBounds) {
    WalkerBounds bounds{0.0f, 0.0f, 100.0f, 100.0f};
    SuctionCenterWalker walker(Vec2{50.0f, 50.0f}, bounds, 3.0f);
    FixedSequenceRandom rng({0.1f, 0.9f, 0.3f, 0.7f, 0.05f, 0.6f});

    for (int i = 0; i < 500; ++i) {
        walker.Step(rng);
        const auto& p = walker.Position();
        CHECK(p.x >= bounds.minX - 0.001f);
        CHECK(p.x <= bounds.maxX + 0.001f);
        CHECK(p.y >= bounds.minY - 0.001f);
        CHECK(p.y <= bounds.maxY + 0.001f);
    }
}

TEST_CASE(SuctionCenterWalker_MovesEachFrame) {
    WalkerBounds bounds{0.0f, 0.0f, 1000.0f, 1000.0f};
    SuctionCenterWalker walker(Vec2{500.0f, 500.0f}, bounds, 2.0f);
    FixedSequenceRandom rng({0.25f, 0.9f});
    const Vec2 start = walker.Position();
    walker.Step(rng);
    const Vec2 afterOne = walker.Position();
    CHECK(start.x != afterOne.x || start.y != afterOne.y);
}
