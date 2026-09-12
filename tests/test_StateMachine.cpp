#include "test_framework.h"

#include "../src/core/StateMachine.h"

using core::SaverState;
using core::SaverStateMachine;
using core::StateMachineInputs;

TEST_CASE(StateMachine_StartsAtIcons) {
    SaverStateMachine sm;
    CHECK(sm.Current() == SaverState::STATE_ICONS);
}

TEST_CASE(StateMachine_DoesNotAdvanceUntilConditionMet) {
    SaverStateMachine sm;
    StateMachineInputs inputs;
    CHECK(!sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_ICONS);
}

TEST_CASE(StateMachine_FollowsFullRequiredOrder) {
    SaverStateMachine sm;
    StateMachineInputs inputs;

    inputs.allIconsConsumed = true;
    CHECK(sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_WINDOWS);
    inputs.allIconsConsumed = false;

    inputs.allWindowsConsumed = true;
    CHECK(sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_BACKGROUND);
    inputs.allWindowsConsumed = false;

    inputs.allParticlesConsumed = true;
    CHECK(sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_BLACK);
    inputs.allParticlesConsumed = false;

    inputs.blackHoldElapsed = true;
    CHECK(sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_FADE);
    inputs.blackHoldElapsed = false;

    inputs.fadeComplete = true;
    CHECK(sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_RESET);
    inputs.fadeComplete = false;

    inputs.resetHoldElapsed = true;
    CHECK(sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_ICONS); // loops back (要件: 無限ループ)
}

TEST_CASE(StateMachine_UnrelatedFlagsDoNotCauseSkips) {
    SaverStateMachine sm;
    StateMachineInputs inputs;
    // Setting a later-phase flag should not skip the current phase.
    inputs.fadeComplete = true;
    inputs.resetHoldElapsed = true;
    CHECK(!sm.Advance(inputs));
    CHECK(sm.Current() == SaverState::STATE_ICONS);
}
