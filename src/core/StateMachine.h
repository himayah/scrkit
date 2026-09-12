#pragma once
// The saver's state machine (要件.txt §8).

namespace core {

enum class SaverState {
    STATE_ICONS,
    STATE_WINDOWS,
    STATE_BACKGROUND,
    STATE_BLACK,
    STATE_FADE,
    STATE_RESET,
};

// Inputs the state machine needs to decide whether to advance. Each flag
// means "the current phase's work is finished".
struct StateMachineInputs {
    bool allIconsConsumed = false;
    bool allWindowsConsumed = false;
    bool allParticlesConsumed = false;
    bool blackHoldElapsed = false;
    bool fadeComplete = false;
    bool resetHoldElapsed = false;
};

class SaverStateMachine {
public:
    explicit SaverStateMachine(SaverState initial = SaverState::STATE_ICONS) : state_(initial) {}

    SaverState Current() const { return state_; }

    // Advances the state machine by one logical step if the inputs indicate
    // the current phase is done. Returns true if the state changed.
    // 要件.txt §4 の順序: ICONS -> WINDOWS -> BACKGROUND -> BLACK -> FADE ->
    // RESET -> ICONS (無限ループ)。
    bool Advance(const StateMachineInputs& inputs);

private:
    SaverState state_;
};

} // namespace core
