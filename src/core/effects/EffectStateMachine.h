#pragma once
// Per-layer continuous/terminal effect cycling (DESIGN_EFFECTS.md §5.2-5.4).
// Deliberately decoupled from IEffect/EffectRegistry: this class only tracks
// *which* effect (by id) should be active and *when* to switch, driven by
// EffectScheduler::Pick. The caller (EffectEngine, §16 Step 6) owns the
// actual IEffect instances and is responsible for:
//   - constructing/Begin()-ing a new IEffect when FxOutputs::switched is true
//     (Timeline::Current() names which EffectId/seed to use)
//   - calling the current IEffect's RequestExit(transitionSeconds) once, the
//     moment Current() reads Exiting (whether that happened synchronously
//     inside a RequestTerminal() call the engine just made, or was noticed
//     via FxOutputs::enteredExiting after a plain Step())
//   - supplying IsFinished()/IsAtRest()/Exit() of the current IEffect back in
//     the next FxInputs

#include "EffectParams.h"
#include "EffectScheduler.h"
#include "EffectTypes.h"
#include "Timeline.h"

namespace core::fx {

enum class FxState { Idle, Rest, Entering, Running, Exiting, TerminalRunning, TerminalDraining, Consumed, Empty };

enum class NoCandidatePolicy { ImmediateTerminal, WaitForTerminalRequest };

struct FxInputs {
    float dt = 0.0f;
    bool currentFinished = false; // Terminal only: current IEffect::IsFinished()
    bool currentAtRest = false;   // ReturnToRest exit only: current IEffect::IsAtRest()
    // Current continuous IEffect::Exit(); ignored outside Entering/Running/
    // Exiting (this field isn't in the doc's original FxInputs sketch -- it's
    // an implementation-time addition to resolve how a state machine that
    // never touches IEffect can still know which exit strategy applies).
    ExitStrategy currentExitStrategy = ExitStrategy::Envelope;
    bool hueShiftReady = true; // Background layer only; ignored for Foreground
};

struct FxOutputs {
    bool switched = false;       // a new TimelineEntry began this frame (Entering or TerminalRunning)
    bool enteredExiting = false; // transitioned into Exiting during this Step() call specifically
                                  // (a RequestTerminal()-triggered Exiting is visible immediately via
                                  // Current() right after that call instead, see class comment above)
    bool consumed = false;       // reached Consumed this frame
    float envelope = 0.0f;
};

class EffectStateMachine {
public:
    EffectStateMachine(LayerKind layer, NoCandidatePolicy policy, const EngineConfig& engine,
                        core::IRandomSource& rng, Timeline& timeline);

    // reason == NotEmpty is the normal case. Anything else immediately goes
    // to Empty (§5.3/§5.4; Background never passes anything but NotEmpty).
    void Reset(EmptyReason reason);

    // Synchronous: from Idle this may immediately enter TerminalRunning; from
    // Entering/Running this immediately enters Exiting. See class comment.
    void RequestTerminal();

    // §5.2's "任意 | OnPhaseEntered(BLACK/FADE) | Idle" and
    // "任意 | OnPhaseEntered(RESET) | Rest" rows: unconditional, from any
    // state (the caller -- EffectEngine -- calls exactly one of these from
    // its own OnPhaseEntered whenever the phase machine enters BLACK/FADE or
    // RESET, per §5.1's OnPhaseEntered table).
    void ForceIdle();
    void ForceRest();

    FxOutputs Step(const FxInputs& in);

    FxState Current() const { return state_; }
    float Envelope() const { return envelope_; }

private:
    void BeginEntry(EffectId id, float durationSeconds, uint32_t seed);
    void EnterTerminalRunning();
    void AdvanceAfterExiting(FxOutputs& out);
    EffectId DefaultTerminalId() const;

    LayerKind layer_;
    NoCandidatePolicy policy_;
    const EngineConfig* engine_;
    core::IRandomSource* rng_;
    Timeline* timeline_;

    FxState state_ = FxState::Idle;
    float elapsedInState_ = 0.0f;
    float phaseElapsedSeconds_ = 0.0f; // since last Reset(); used only for TimelineEntry::startSeconds
    float showcaseElapsedSeconds_ = 0.0f; // Foreground only
    float envelope_ = 0.0f;
    float currentDurationSeconds_ = 0.0f;
    bool terminalRequestedFlag_ = false;
    EmptyReason emptyReason_ = EmptyReason::NotEmpty;
    // Cached from the most recent Step()'s FxInputs::hueShiftReady, since
    // Reset() has no FxInputs of its own but still needs to know this when
    // picking a Continuous candidate (Background only; HueShift is never a
    // Terminal candidate so this cache is irrelevant to EnterTerminalRunning
    // in practice, but is threaded through there too for consistency).
    bool hueShiftReadyCache_ = true;
};

class ForegroundEffectStateMachine : public EffectStateMachine {
public:
    ForegroundEffectStateMachine(const EngineConfig& engine, core::IRandomSource& rng, Timeline& timeline)
        : EffectStateMachine(LayerKind::Foreground, NoCandidatePolicy::ImmediateTerminal, engine, rng, timeline) {}
};

class BackgroundEffectStateMachine : public EffectStateMachine {
public:
    BackgroundEffectStateMachine(const EngineConfig& engine, core::IRandomSource& rng, Timeline& timeline)
        : EffectStateMachine(LayerKind::Background, NoCandidatePolicy::WaitForTerminalRequest, engine, rng,
                              timeline) {}
};

} // namespace core::fx
