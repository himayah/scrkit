#include "test_framework.h"

#include "../src/core/FadeController.h"

using core::FadeController;

TEST_CASE(FadeController_StartsAtZeroAlpha) {
    FadeController fade(2.0f);
    CHECK_NEAR(fade.Alpha(), 0.0f, 0.0001f);
    CHECK(!fade.IsComplete());
}

TEST_CASE(FadeController_ReachesFullAlphaAtDuration) {
    FadeController fade(2.0f);
    fade.Step(1.0f);
    CHECK_NEAR(fade.Alpha(), 0.5f, 0.0001f);
    CHECK(!fade.IsComplete());
    fade.Step(1.0f);
    CHECK_NEAR(fade.Alpha(), 1.0f, 0.0001f);
    CHECK(fade.IsComplete());
}

TEST_CASE(FadeController_ClampsAtOneWhenOvershooting) {
    FadeController fade(1.0f);
    fade.Step(5.0f);
    CHECK_NEAR(fade.Alpha(), 1.0f, 0.0001f);
    CHECK(fade.IsComplete());
}

TEST_CASE(FadeController_ResetGoesBackToZero) {
    FadeController fade(1.0f);
    fade.Step(1.0f);
    CHECK(fade.IsComplete());
    fade.Reset();
    CHECK_NEAR(fade.Alpha(), 0.0f, 0.0001f);
}

TEST_CASE(FadeController_ZeroDurationIsImmediatelyComplete) {
    FadeController fade(0.0f);
    CHECK(fade.IsComplete());
}
