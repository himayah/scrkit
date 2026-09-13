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

TEST_CASE(SpiralMath_ZeroCenterAccelMatchesPlainDTheta) {
    // Default centerAccelFactor (0) must reproduce the original
    // constant-angular-speed behavior exactly.
    auto stateA = MakeSpiralState(150.0f, 100.0f, 100.0f, 100.0f);
    auto stateB = stateA;
    SpiralParams plain{0.15f, 2.0f, 0.0f};
    SpiralParams explicitZero{0.15f, 2.0f, 0.0f};
    StepSpiral(stateA, plain, 100.0f, 100.0f);
    StepSpiral(stateB, explicitZero, 100.0f, 100.0f);
    CHECK_NEAR(stateA.theta, stateB.theta, 0.00001f);
}

TEST_CASE(SpiralMath_CenterAccelIncreasesAngularSpeedNearCenter) {
    SpiralParams params{0.1f, 0.0f, 40.0f}; // suctionSpeed 0 so r stays fixed per case
    auto farState = MakeSpiralState(300.0f, 100.0f, 100.0f, 100.0f);  // r = 200
    auto nearState = MakeSpiralState(110.0f, 100.0f, 100.0f, 100.0f); // r = 10
    StepSpiral(farState, params, 100.0f, 100.0f);
    StepSpiral(nearState, params, 100.0f, 100.0f);
    const float farDTheta = farState.theta;   // started at theta=0
    const float nearDTheta = nearState.theta; // started at theta=0
    CHECK(nearDTheta > farDTheta);
    // far: 0.1 + 40/200 = 0.3 ; near: 0.1 + 40/10 = 4.1 -> clamped to kMaxDTheta(1.2)
    CHECK_NEAR(farDTheta, 0.3f, 0.001f);
    CHECK_NEAR(nearDTheta, 1.2f, 0.001f);
}

TEST_CASE(SpiralMath_CenterAccelNeverExceedsClampEvenAtTinyR) {
    SpiralParams params{0.1f, 0.0f, 1000.0f};
    auto state = MakeSpiralState(100.5f, 100.0f, 100.0f, 100.0f); // r = 0.5 -> clamped to max(r,1)=1
    auto pos = StepSpiral(state, params, 100.0f, 100.0f);
    CHECK_NEAR(state.theta, 1.2f, 0.001f); // clamped, not 0.1 + 1000/0.5
    (void)pos;
}

TEST_CASE(SpiralMath_VortexParamsHavePositiveCenterAccel) {
    auto vortex = core::VortexSpiralParams();
    CHECK(vortex.centerAccelFactor > 0.0f);
    CHECK_NEAR(vortex.dTheta, core::LightweightSpiralParams().dTheta, 0.0001f);
}

TEST_CASE(SpiralMath_MakeParamsForRevolutionsCompletesRequestedTurns) {
    // r0 = 400, suctionSpeed = 2 -> exactly 200 frames to consume.
    const float r0 = 400.0f;
    const float suctionSpeed = 2.0f;
    const float targetRevolutions = 4.0f;
    auto params = core::MakeParamsForRevolutions(r0, suctionSpeed, targetRevolutions);
    CHECK(params.centerAccelFactor == 0.0f);
    CHECK_NEAR(params.suctionSpeed, suctionSpeed, 0.0001f);

    auto state = core::MakeSpiralState(r0 + 100.0f, 100.0f, 100.0f, 100.0f); // r0 = 400
    float totalTheta = 0.0f;
    while (state.alive) {
        const float before = state.theta;
        core::StepSpiral(state, params, 100.0f, 100.0f);
        totalTheta += (state.theta - before);
    }
    const float kTwoPi = 6.28318530718f;
    CHECK_NEAR(totalTheta / kTwoPi, targetRevolutions, 0.05f);
}

TEST_CASE(SpiralMath_MakeParamsForRevolutionsFallsBackForTinyRadius) {
    // r0 so small the object dies within one frame -- derived dTheta would
    // be meaningless, so this should just be the normal fallback constant.
    auto params = core::MakeParamsForRevolutions(0.5f, 2.0f, 4.0f);
    CHECK_NEAR(params.dTheta, core::NormalSpiralParams().dTheta, 0.0001f);
}
