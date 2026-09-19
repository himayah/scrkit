#include "StateMachine.h"

namespace core {

bool SaverStateMachine::Advance(const StateMachineInputs& inputs) {
    switch (state_) {
        case SaverState::STATE_CONTENT:
            if (inputs.blackoutElapsed) {
                state_ = SaverState::STATE_FADEOUT;
                return true;
            }
            return false;
        case SaverState::STATE_FADEOUT:
            if (inputs.fadeOutComplete) {
                state_ = SaverState::STATE_BLACK;
                return true;
            }
            return false;
        case SaverState::STATE_BLACK:
            if (inputs.blackHoldElapsed) {
                state_ = SaverState::STATE_FADE;
                return true;
            }
            return false;
        case SaverState::STATE_FADE:
            if (inputs.fadeComplete) {
                state_ = SaverState::STATE_RESET;
                return true;
            }
            return false;
        case SaverState::STATE_RESET:
            if (inputs.resetHoldElapsed) {
                state_ = SaverState::STATE_CONTENT;
                return true;
            }
            return false;
    }
    return false;
}

} // namespace core
