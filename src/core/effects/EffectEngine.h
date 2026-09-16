#pragma once
// Owns both layers' state machines and the currently-active IEffect
// instances, and produces the FrameDrawList platform::Renderer consumes
// (DESIGN_EFFECTS.md §2, §5.1, §8). This is the "core::fx" entry point
// AppController drives once per frame (§16 Step 8 wires that up).
//
// Known scope limit as of §16 Step 6: a layer's DrawList contribution is
// exactly one DrawBatch per frame, built from that layer's single
// LayerGeometry scratch buffer. That's sufficient for every effect
// registered so far (Fragments-geometry terminal effects) and for the
// Transform/Mesh/RadialMesh/Tiles/Bands single-batch cases in general, but
// NOT for the handful of effects that inherently need more than one batch in
// the same frame (GlitchShift's 3 RGB-masked passes, HueShift's 2-texture
// blend, any Crossfade transition's rest+effect blend, §4.6). Extending
// LayerGeometry/AppendDrawBatches for those is deferred to §16 Step 9, when
// those effects are actually implemented, rather than speculatively now.

#include <memory>

#include "../StateMachine.h" // core::SaverState
#include "DrawList.h"
#include "EffectStateMachine.h"
#include "IEffect.h"
#include "Layer.h"

namespace core::fx {

class EffectEngine {
public:
    EffectEngine(const EngineConfig& config, core::IRandomSource& rng);
    EffectEngine(const EffectEngine&) = delete;
    EffectEngine& operator=(const EffectEngine&) = delete;

    // `foreground`/`background` should already have kind/texture/screenW/H/
    // gridN/cellIndices/cells/cellHalfW/cellHalfH/empty/emptyReason filled
    // in (§4.1) by the caller; this completes them by building restMesh
    // (§4.4.1) and takes ownership.
    void SetLayers(LayerSource foreground, LayerSource background);

    struct Inputs {
        float dt = 0.0f;
        Vec2 suctionCenter{0.0f, 0.0f};
        bool hueShiftReady = true; // wired for real in §16 Step 10; harmless no-op until HueShift exists
    };
    struct Outputs {
        bool foregroundConsumed = false;
        bool backgroundConsumed = false;
    };

    Outputs Update(const Inputs& in);

    // Call exactly once whenever core::SaverStateMachine::Advance() returns
    // true (i.e. right after the phase actually changed), per §5.1's table.
    void OnPhaseEntered(core::SaverState phase);

    const FrameDrawList& DrawList() const { return drawList_; }

private:
    struct LayerRuntime {
        EffectStateMachine* sm = nullptr;
        LayerSource layer;
        Timeline timeline;
        std::unique_ptr<IEffect> current;
        LayerGeometry geometry;
        float effectElapsedSeconds = 0.0f;
        // Identifies which TimelineEntry `current` was Begin()-built from, so
        // a fresh entry can be detected uniformly whether it was started by
        // Step()'s FxOutputs::switched or synchronously inside Reset()/
        // RequestTerminal() (which don't go through Step() at all -- see
        // SyncCurrentEffectFromTimeline's comment).
        bool hasAppliedEntry = false;
        uint32_t lastAppliedSeed = 0;
    };

    void UpdateLayer(LayerRuntime& rt, const LayerEffectConfig& layerConfig, const Inputs& in, bool& consumedOut);
    void SyncCurrentEffectFromTimeline(LayerRuntime& rt, const LayerEffectConfig& layerConfig);
    void AppendDrawBatch(const LayerRuntime& rt);

    EngineConfig config_;
    core::IRandomSource* rng_;

    LayerRuntime fg_;
    LayerRuntime bg_;
    ForegroundEffectStateMachine fgSM_;
    BackgroundEffectStateMachine bgSM_;

    FrameDrawList drawList_;
};

} // namespace core::fx
