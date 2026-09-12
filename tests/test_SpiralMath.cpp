#include "test_framework.h"

#include "../src/core/SpiralMath.h"

using core::MakeSpiralState;
using core::NormalSpiralParams;
using core::SpiralParams;
using core::StepSpiral;

TEST_CASE(SpiralMath_StartsAtGivenDistanceAndAngle) {
    auto state = MakeSpiralState(110.0f, 100.0f, 100.0f, 100.0f); // 10px to the right of center
    CHECK_NEAR(state.r, 10.0f, 0.001f);
    CHECK_NEAR(state.theta, 0.0f, 0.001f);
    CHECK(state.alive);
}

TEST_CASE(SpiralMath_RShrinksBySuctionSpeedEachStep) {
    auto state = MakeSpiralState(150.0f, 100.0f, 100.0f, 100.0f); // r = 50
    SpiralParams params{0.15f, 2.0f};
    StepSpiral(state, params, 100.0f, 100.0f);
    CHECK_NEAR(state.r, 48.0f, 0.001f);
    StepSpiral(state, params, 100.0f, 100.0f);
    CHECK_NEAR(state.r, 46.0f, 0.001f);
}

TEST_CASE(SpiralMath_ThetaAdvancesBySpiralParamsStep) {
    auto state = MakeSpiralState(150.0f, 100.0f, 100.0f, 100.0f);
    SpiralParams params{0.15f, 2.0f};
    StepSpiral(state, params, 100.0f, 100.0f);
    CHECK_NEAR(state.theta, 0.15f, 0.0001f);
}

TEST_CASE(SpiralMath_DiesWhenRReachesZero) {
    auto state = MakeSpiralState(104.0f, 100.0f, 100.0f, 100.0f); // r = 4
    SpiralParams params{0.15f, 2.0f};
    StepSpiral(state, params, 100.0f, 100.0f); // r -> 2
    CHECK(state.alive);
    auto pos = StepSpiral(state, params, 100.0f, 100.0f); // r -> 0
    CHECK(!state.alive);
    CHECK_EQ(state.r, 0.0f);
    CHECK_NEAR(pos.x, 100.0f, 0.001f);
    CHECK_NEAR(pos.y, 100.0f, 0.001f);
}

TEST_CASE(SpiralMath_StepAfterDeathIsNoOpAtCenter) {
    auto state = MakeSpiralState(101.0f, 100.0f, 100.0f, 100.0f); // r = 1
    SpiralParams params{0.15f, 2.0f};
    StepSpiral(state, params, 100.0f, 100.0f); // dies
    CHECK(!state.alive);
    auto pos = StepSpiral(state, params, 100.0f, 100.0f); // should stay dead/no-op
    CHECK(!state.alive);
    CHECK_NEAR(pos.x, 100.0f, 0.001f);
    CHECK_NEAR(pos.y, 100.0f, 0.001f);
}

TEST_CASE(SpiralMath_LightweightParamsAreLighterThanNormal) {
    auto normal = NormalSpiralParams();
    auto light = core::LightweightSpiralParams();
    CHECK(light.dTheta < normal.dTheta);
    CHECK(light.suctionSpeed < normal.suctionSpeed);
}
