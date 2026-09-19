#pragma once
// The saver's state machine (要件.txt §8).

namespace core {

// STATE_CONTENT replaces what used to be separate STATE_ICONS/STATE_WINDOWS
// phases: real icons/windows are no longer queried and animated as
// individual rectangles, but as a single set of "content" grid cells
// (wherever the real-desktop capture differs from the plain wallpaper --
// see core::ContentMask) sucked in together.
//
// Both the foreground (content) and background layers now cycle their own
// effect pools forever, independently, for the whole of STATE_CONTENT --
// neither layer "finishes" on its own anymore. What used to be
// STATE_BACKGROUND (background alone, forced to finish via a dedicated
// suction effect once content was gone) is now STATE_FADEOUT: a fixed-length
// beat where both layers keep animating, undisturbed, while the screen fades
// to black on top of them. Entry into STATE_FADEOUT is driven purely by an
// elapsed-time trigger (AppController's randomized "blackout" timer), not by
// either layer completing anything.
enum class SaverState {
    STATE_CONTENT,
    STATE_FADEOUT,
    STATE_BLACK,
    STATE_FADE,
    STATE_RESET,
};

// Inputs the state machine needs to decide whether to advance. Each flag
// means "the current phase's work is finished".
struct StateMachineInputs {
    bool blackoutElapsed = false; // STATE_CONTENT: the randomized blackout timer fired
    bool fadeOutComplete = false; // STATE_FADEOUT: the fade-to-black overlay reached full black
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
    // CONTENT -> FADEOUT -> BLACK -> FADE -> RESET -> CONTENT (無限ループ)。
    bool Advance(const StateMachineInputs& inputs);

private:
    SaverState state_;
};

} // namespace core
