#include "test_framework.h"

#include "../src/core/effects/Envelope.h"

using core::fx::StepEnvelope;

TEST_CASE(Envelope_MonotonicIncreaseTowardsTarget) {
    float e = 0.0f;
    float prev = e;
    for (int i = 0; i < 60; ++i) {
        e = StepEnvelope(e, 1.0f / 60.0f, 1.0f, 0.5f);
        CHECK(e >= prev);
        CHECK(e <= 1.0f);
        prev = e;
    }
    CHECK_NEAR(e, 1.0f, 1e-3f);
}

TEST_CASE(Envelope_MonotonicDecreaseTowardsTarget) {
    float e = 1.0f;
    float prev = e;
    for (int i = 0; i < 60; ++i) {
        e = StepEnvelope(e, 1.0f / 60.0f, 0.0f, 0.5f);
        CHECK(e <= prev);
        CHECK(e >= 0.0f);
        prev = e;
    }
    CHECK_NEAR(e, 0.0f, 1e-3f);
}

TEST_CASE(Envelope_FixedPointAtTarget) {
    float e = 0.5f;
    for (int i = 0; i < 10; ++i) {
        e = StepEnvelope(e, 1.0f / 60.0f, 0.5f, 0.5f);
    }
    CHECK_NEAR(e, 0.5f, 1e-6f);
}

TEST_CASE(Envelope_ZeroRateReachesTargetImmediately) {
    const float e = StepEnvelope(0.0f, 1.0f / 60.0f, 1.0f, 0.0f);
    CHECK_NEAR(e, 1.0f, 1e-6f);
}

TEST_CASE(Envelope_LargeDtDoesNotOvershoot) {
    const float e = StepEnvelope(0.2f, 5.0f, 1.0f, 0.5f);
    CHECK(e <= 1.0f);
    CHECK_NEAR(e, 1.0f, 1e-6f);
}

TEST_CASE(Envelope_TargetSwitchContinuesFromCurrentValueWithoutJump) {
    // Simulate an Entering phase interrupted partway by a switch to target=0
    // (RequestTerminal cutting Entering short, §5.2/§6.0.1): the very next
    // Step must move e continuously from wherever it was, not from 1 or 0.
    float e = 0.0f;
    for (int i = 0; i < 10; ++i) e = StepEnvelope(e, 1.0f / 60.0f, 1.0f, 0.5f);
    const float beforeSwitch = e;
    CHECK(beforeSwitch > 0.0f);
    CHECK(beforeSwitch < 1.0f);
    const float afterSwitch = StepEnvelope(beforeSwitch, 1.0f / 60.0f, 0.0f, 0.5f);
    CHECK(afterSwitch < beforeSwitch);
    // No jump: one frame of movement at the same rate, not a reset to 1 first.
    CHECK_NEAR(beforeSwitch - afterSwitch, (1.0f / 60.0f) / 0.5f, 1e-4f);
}
