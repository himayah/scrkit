#include "EffectStateMachine.h"

#include "Envelope.h"

namespace core::fx {

EffectStateMachine::EffectStateMachine(LayerKind layer, NoCandidatePolicy policy, const EngineConfig& engine,
                                        core::IRandomSource& rng, Timeline& timeline)
    : layer_(layer), policy_(policy), engine_(&engine), rng_(&rng), timeline_(&timeline) {}

EffectId EffectStateMachine::DefaultTerminalId() const {
    return layer_ == LayerKind::Foreground ? EffectId::VortexSuction : EffectId::BackgroundSuction;
}

void EffectStateMachine::BeginEntry(EffectId id, float durationSeconds, uint32_t seed) {
    currentDurationSeconds_ = durationSeconds;
    timeline_->Append(TimelineEntry{layer_, id, phaseElapsedSeconds_, durationSeconds, seed});
}

void EffectStateMachine::EnterTerminalRunning() {
    auto pick = EffectScheduler::Pick(EffectKind::Terminal, layer_, *engine_, timeline_->History(), *rng_,
                                       hueShiftReadyCache_);
    if (pick) {
        BeginEntry(pick->id, pick->durationSeconds, pick->seed);
    } else {
        // Both engine.enabled==false and "every terminal effect individually
        // disabled" land here; the layer's fixed default terminal always
        // takes over so a layer is never left stuck (§5.5, D-12).
        BeginEntry(DefaultTerminalId(), engine_->terminalMaxSeconds, rng_->NextUInt32());
    }
    envelope_ = 1.0f;
    state_ = FxState::TerminalRunning;
    elapsedInState_ = 0.0f;
}

void EffectStateMachine::Reset(EmptyReason reason) {
    emptyReason_ = reason;
    phaseElapsedSeconds_ = 0.0f;
    showcaseElapsedSeconds_ = 0.0f;
    terminalRequestedFlag_ = false;
    elapsedInState_ = 0.0f;

    if (reason != EmptyReason::NotEmpty) {
        state_ = FxState::Empty;
        return;
    }

    auto pick = EffectScheduler::Pick(EffectKind::Continuous, layer_, *engine_, timeline_->History(), *rng_,
                                       hueShiftReadyCache_);
    if (pick) {
        BeginEntry(pick->id, pick->durationSeconds, pick->seed);
        envelope_ = 0.0f;
        state_ = FxState::Entering;
        return;
    }

    if (policy_ == NoCandidatePolicy::ImmediateTerminal) {
        EnterTerminalRunning();
    } else {
        state_ = FxState::Idle;
    }
}

void EffectStateMachine::RequestTerminal() {
    terminalRequestedFlag_ = true;
    if (state_ == FxState::Idle) {
        EnterTerminalRunning();
    } else if (state_ == FxState::Entering || state_ == FxState::Running) {
        state_ = FxState::Exiting;
        elapsedInState_ = 0.0f;
    }
}

void EffectStateMachine::ForceIdle() {
    state_ = FxState::Idle;
    elapsedInState_ = 0.0f;
    envelope_ = 0.0f;
}

void EffectStateMachine::ForceRest() {
    state_ = FxState::Rest;
    elapsedInState_ = 0.0f;
    envelope_ = 0.0f;
}

void EffectStateMachine::AdvanceAfterExiting(FxOutputs& out) {
    if (!terminalRequestedFlag_) {
        auto pick = EffectScheduler::Pick(EffectKind::Continuous, layer_, *engine_, timeline_->History(), *rng_,
                                           hueShiftReadyCache_);
        if (pick) {
            BeginEntry(pick->id, pick->durationSeconds, pick->seed);
            envelope_ = 0.0f;
            state_ = FxState::Entering;
            elapsedInState_ = 0.0f;
            out.switched = true;
            return;
        }
    }
    EnterTerminalRunning();
    out.switched = true;
}

FxOutputs EffectStateMachine::Step(const FxInputs& in) {
    FxOutputs out{};
    elapsedInState_ += in.dt;
    phaseElapsedSeconds_ += in.dt;
    hueShiftReadyCache_ = in.hueShiftReady;

    const FxState before = state_;

    if (layer_ == LayerKind::Foreground &&
        (state_ == FxState::Entering || state_ == FxState::Running || state_ == FxState::Exiting)) {
        showcaseElapsedSeconds_ += in.dt;
        if (!terminalRequestedFlag_ && showcaseElapsedSeconds_ >= engine_->foregroundShowcaseSeconds) {
            terminalRequestedFlag_ = true;
            if (state_ != FxState::Exiting) {
                state_ = FxState::Exiting;
                elapsedInState_ = 0.0f;
            }
        }
    }

    switch (state_) {
        case FxState::Idle:
        case FxState::Rest:
            out.envelope = 0.0f;
            break;

        case FxState::Entering:
            envelope_ = StepEnvelope(envelope_, in.dt, 1.0f, engine_->transitionSeconds);
            if (elapsedInState_ >= engine_->transitionSeconds) {
                state_ = FxState::Running;
                elapsedInState_ = 0.0f;
            }
            out.envelope = envelope_;
            break;

        case FxState::Running:
            envelope_ = StepEnvelope(envelope_, in.dt, 1.0f, engine_->transitionSeconds);
            if (elapsedInState_ >= currentDurationSeconds_) {
                state_ = FxState::Exiting;
                elapsedInState_ = 0.0f;
            }
            out.envelope = envelope_;
            break;

        case FxState::Exiting: {
            bool exitComplete = false;
            switch (in.currentExitStrategy) {
                case ExitStrategy::Envelope:
                    envelope_ = StepEnvelope(envelope_, in.dt, 0.0f, engine_->transitionSeconds);
                    exitComplete = envelope_ <= 0.0f;
                    break;
                case ExitStrategy::ReturnToRest:
                    // envelope stays at its Exiting-entry value (§6.0.1): the
                    // effect itself slides its phase to rest instead.
                    exitComplete = in.currentAtRest || elapsedInState_ >= 2.0f * engine_->transitionSeconds;
                    break;
                case ExitStrategy::Crossfade:
                    exitComplete = elapsedInState_ >= engine_->transitionSeconds;
                    break;
            }
            out.envelope = envelope_;
            if (exitComplete) AdvanceAfterExiting(out);
            break;
        }

        case FxState::TerminalRunning:
            out.envelope = envelope_;
            if (in.currentFinished) {
                state_ = FxState::Consumed;
                elapsedInState_ = 0.0f;
                out.consumed = true;
            } else if (elapsedInState_ >= engine_->terminalMaxSeconds) {
                state_ = FxState::TerminalDraining;
                elapsedInState_ = 0.0f;
            }
            break;

        case FxState::TerminalDraining:
            out.envelope = envelope_;
            if (elapsedInState_ >= 0.5f) {
                state_ = FxState::Consumed;
                elapsedInState_ = 0.0f;
                out.consumed = true;
            }
            break;

        case FxState::Consumed:
            out.consumed = true;
            break;

        case FxState::Empty: {
            const bool consumeNow = emptyReason_ == EmptyReason::CaptureFailed
                                         ? true
                                         : elapsedInState_ >= engine_->foregroundShowcaseSeconds;
            if (consumeNow) {
                state_ = FxState::Consumed;
                elapsedInState_ = 0.0f;
                out.consumed = true;
            }
            break;
        }
    }

    out.enteredExiting = (before != FxState::Exiting && state_ == FxState::Exiting);
    return out;
}

} // namespace core::fx
