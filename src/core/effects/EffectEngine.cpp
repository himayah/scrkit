#include "EffectEngine.h"

#include <cmath>

#include "EffectRegistry.h"
#include "ForegroundMeshBuilder.h"

namespace core::fx {

EffectEngine::EffectEngine(const EngineConfig& config, core::IRandomSource& rng)
    : config_(config),
      rng_(&rng),
      fgSM_(config_, rng, fg_.timeline),
      bgSM_(config_, rng, bg_.timeline) {
    fg_.sm = &fgSM_;
    bg_.sm = &bgSM_;
}

void EffectEngine::SetLayers(LayerSource foreground, LayerSource background) {
    // Foreground rest mesh: gridN x gridN diff mask, subdivided into
    // cellSubdiv x cellSubdiv child quads per parent cell when gridN < 48
    // (§4.4.1 / D-20). No seams -- NorenSwing (§6.1.2) builds its own mesh
    // with strip-boundary seams separately, via the same helper.
    foreground.restMesh = BuildForegroundRestMesh(foreground);
    // Background rest mesh: fixed ~20px grid, independent of particle count (D-6).
    {
        const int cols = static_cast<int>(std::ceil(background.screenW / 20.0f));
        const int rows = static_cast<int>(std::ceil(background.screenH / 20.0f));
        background.restMesh = MeshBuilder::BuildGrid(background.screenW, background.screenH, cols, rows, {}, {});
    }

    fg_.layer = std::move(foreground);
    bg_.layer = std::move(background);
}

void EffectEngine::UpdateLayer(LayerRuntime& rt, const LayerEffectConfig& layerConfig, const Inputs& in,
                                bool& consumedOut) {
    FxInputs fxIn;
    fxIn.dt = in.dt;
    fxIn.hueShiftReady = in.hueShiftReady;
    if (rt.current) {
        fxIn.currentFinished = rt.current->IsFinished();
        fxIn.currentAtRest = rt.current->IsAtRest();
        fxIn.currentExitStrategy = rt.current->Exit();
    }

    const FxOutputs fxOut = rt.sm->Step(fxIn);

    if (fxOut.enteredExiting && rt.current) {
        rt.current->RequestExit(config_.transitionSeconds);
    }

    SyncCurrentEffectFromTimeline(rt, layerConfig);

    const FxState state = rt.sm->Current();
    const bool active = state == FxState::Entering || state == FxState::Running || state == FxState::Exiting ||
                         state == FxState::TerminalRunning || state == FxState::TerminalDraining;
    if (rt.current && active) {
        rt.effectElapsedSeconds += in.dt;
        rt.geometry.Clear();
        EffectFrame frame;
        frame.t = rt.effectElapsedSeconds;
        frame.dt = in.dt;
        frame.intensity = ParamsFor(layerConfig, rt.current->Id()).intensity * fxOut.envelope;
        frame.suctionCenter = in.suctionCenter;
        frame.out = &rt.geometry;
        rt.current->Step(frame);
    }

    consumedOut = (state == FxState::Consumed);
    if (consumedOut) {
        rt.current.reset();
        rt.hasAppliedEntry = false;
    }
}

void EffectEngine::SyncCurrentEffectFromTimeline(LayerRuntime& rt, const LayerEffectConfig& layerConfig) {
    // Reset()/RequestTerminal() can start a fresh TimelineEntry synchronously,
    // outside of Step() (§5.1/§5.2's "synchronous" transitions), so this
    // can't just watch FxOutputs::switched -- it compares against the last
    // entry this layer actually Begin()-built an IEffect from instead,
    // which is updated from every possible entry point uniformly.
    const TimelineEntry* entry = rt.timeline.Current();
    if (!entry) {
        rt.current.reset();
        rt.hasAppliedEntry = false;
        return;
    }
    if (rt.hasAppliedEntry && rt.lastAppliedSeed == entry->seed) return; // already applied

    rt.current = EffectRegistry::Create(entry->effectId);
    rt.effectElapsedSeconds = 0.0f;
    rt.hasAppliedEntry = true;
    rt.lastAppliedSeed = entry->seed;
    if (rt.current) {
        EffectContext ctx;
        ctx.layer = &rt.layer;
        ctx.params = &ParamsFor(layerConfig, entry->effectId);
        ctx.engine = &config_;
        ctx.screenW = rt.layer.screenW;
        ctx.screenH = rt.layer.screenH;
        ctx.seed = entry->seed;
        rt.current->Begin(ctx);
    }
    // Create() returning nullptr just means this EffectId isn't registered
    // yet (§16 Step 9 in progress) -- rt.current staying null means
    // AppendDrawBatch draws nothing for this layer this frame rather than
    // crashing (A.7 "クラッシュさせない" policy).
}

void EffectEngine::AppendDrawBatch(const LayerRuntime& rt) {
    const FxState state = rt.sm->Current();

    DrawBatch batch;
    batch.texture = rt.layer.texture;

    if (state == FxState::Idle || state == FxState::Rest) {
        batch.mesh = &rt.layer.restMesh;
        drawList_.batches.push_back(batch);
        return;
    }
    if (!rt.current || state == FxState::Consumed || state == FxState::Empty) {
        return;
    }

    switch (rt.current->Geometry()) {
        case GeometryKind::Transform:
            // Whole-layer rigid transform: reuse the layer's own rest
            // geometry, just with the effect's transform/alpha applied.
            batch.mesh = &rt.layer.restMesh;
            batch.transform = rt.geometry.transform;
            batch.alpha = rt.geometry.alpha;
            break;
        case GeometryKind::Mesh:
        case GeometryKind::RadialMesh:
            batch.mesh = &rt.geometry.mesh;
            batch.alpha = rt.geometry.alpha;
            break;
        case GeometryKind::Tiles:
        case GeometryKind::Bands:
        case GeometryKind::Fragments:
            batch.quads = &rt.geometry.quads;
            batch.transform = rt.geometry.transform;
            batch.alpha = rt.geometry.alpha;
            break;
    }
    drawList_.batches.push_back(batch);
}

EffectEngine::Outputs EffectEngine::Update(const Inputs& in) {
    Outputs out;
    drawList_.Clear();

    UpdateLayer(bg_, config_.background, in, out.backgroundConsumed);
    AppendDrawBatch(bg_);
    UpdateLayer(fg_, config_.foreground, in, out.foregroundConsumed);
    AppendDrawBatch(fg_); // foreground drawn after background (§4.6)

    return out;
}

void EffectEngine::OnPhaseEntered(core::SaverState phase) {
    using core::SaverState;
    switch (phase) {
        case SaverState::STATE_CONTENT:
            fg_.effectElapsedSeconds = 0.0f;
            bg_.effectElapsedSeconds = 0.0f;
            fgSM_.Reset(fg_.layer.emptyReason);
            bgSM_.Reset(EmptyReason::NotEmpty);
            break;
        case SaverState::STATE_BACKGROUND:
            bgSM_.RequestTerminal();
            if (bgSM_.Current() == FxState::Exiting && bg_.current) {
                bg_.current->RequestExit(config_.transitionSeconds);
            }
            break;
        case SaverState::STATE_BLACK:
        case SaverState::STATE_FADE:
            fgSM_.ForceIdle();
            bgSM_.ForceIdle();
            fg_.current.reset();
            fg_.hasAppliedEntry = false;
            bg_.current.reset();
            bg_.hasAppliedEntry = false;
            break;
        case SaverState::STATE_RESET:
            fgSM_.ForceRest();
            bgSM_.ForceRest();
            fg_.current.reset();
            fg_.hasAppliedEntry = false;
            bg_.current.reset();
            bg_.hasAppliedEntry = false;
            break;
    }
}

} // namespace core::fx
