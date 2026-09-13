#pragma once
// The saver's state machine (要件.txt §8).

namespace core {

// STATE_CONTENT replaces what used to be separate STATE_ICONS/STATE_WINDOWS
// phases: real icons/windows are no longer queried and animated as
// individual rectangles, but as a single set of "content" grid cells
// (wherever the real-desktop capture differs from the plain wallpaper --
// see core::ContentMask) sucked in together.
enum class SaverState {
    STATE_CONTENT,
    STATE_BACKGROUND,
    STATE_BLACK,
    STATE_FADE,
    STATE_RESET,
};

// Inputs the state machine needs to decide whether to advance. Each flag
// means "the current phase's work is finished".
struct StateMachineInputs {
    bool allContentConsumed = false;
    bool allParticlesConsumed = false;
    bool blackHoldElapsed = false;
    bool fadeComplete = false;
    bool resetHoldElapsed = false;
};

class SaverStateMachine {
public:
    explicit SaverStateMachine(SaverState initial = SaverState::STATE_CONTENT) : state_(initial) {}

    SaverState Current() const { return state_; }

    // Advances the state machine by one logical step if the inputs indicate
    // the current phase is done. Returns true if the state changed.
    // 要件.txt §4 の順序 (アイコン+ウィンドウ吸い込みはCONTENTに統合): CONTENT ->
    // BACKGROUND -> BLACK -> FADE -> RESET -> CONTENT (無限ループ)。
    bool Advance(const StateMachineInputs& inputs);

private:
    SaverState state_;
};

} // namespace core
