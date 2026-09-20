#include "EffectEngine.h"

#include <algorithm>
#include <cmath>

#include "EffectCatalog.h"
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

void EffectEngine::UpdateLayer(LayerRuntime& rt, const LayerEffectConfig& layerConfig, const Inputs& in) {
    FxInputs fxIn;
    fxIn.dt = in.dt;
    fxIn.hueShiftReady = in.hueShiftReady;
    if (rt.current) {
        fxIn.currentFinished = rt.current->IsFinished();
        fxIn.currentAtRest = rt.current->IsAtRest();
        fxIn.currentExitStrategy = rt.current->Exit();
    }

    const FxState beforeState = rt.sm->Current();
    const FxOutputs fxOut = rt.sm->Step(fxIn);
    const FxState afterStep = rt.sm->Current();

    if (afterStep == FxState::Exiting) {
        rt.exitingElapsedSeconds = beforeState == FxState::Exiting ? rt.exitingElapsedSeconds + in.dt : 0.0f;
    }

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

    if (state == FxState::Consumed) {
        rt.current.reset();
        rt.hasAppliedEntry = false;
        // Foreground never stays Consumed: the moment its current terminal
        // effect (or an empty-layer wait) finishes, it reappears and picks a
        // fresh continuous effect immediately, so content is perpetually
        // cycling just like the background layer already does (Reset()'s own
        // Continuous-first policy handles that half; this closes the loop for
        // the Terminal/Empty half, which otherwise has no path back to
        // Entering on its own). A genuinely empty layer (no real desktop
        // content) is left alone -- there's nothing to reappear.
        //
        // External-control override (LayerDirective): a pinned terminal that
        // just finished either re-runs (loopTerminal) or leaves the layer at
        // rest, on both layers -- the background never self-resets otherwise.
        const LayerDirective directive = ActiveDirectiveFor(config_, rt.layer.kind);
        const bool pinnedTerminal = directive.mode == LayerMode::Pin &&
                                     EffectKindOn(rt.layer.kind, directive.pinned) == EffectKind::Terminal;
        if (pinnedTerminal && !directive.loopTerminal) {
            rt.sm->ForceRest();
        } else if (pinnedTerminal) {
            rt.sm->Reset(EmptyReason::NotEmpty);
        } else if (rt.layer.kind == LayerKind::Foreground && !rt.layer.empty) {
            rt.sm->Reset(rt.layer.emptyReason);
        }
    }
}

void EffectEngine::SetDirective(LayerKind layer, const LayerDirective& directive) {
    LayerRuntime& rt = layer == LayerKind::Foreground ? fg_ : bg_;
    (layer == LayerKind::Foreground ? config_.foregroundDirective : config_.backgroundDirective) = directive;

    const FxState before = rt.sm->Current();
    rt.sm->Interrupt();
    // Interrupt() moved a running effect into Exiting synchronously; the effect
    // itself has to be told (Step() only reports transitions it made itself).
    if (rt.sm->Current() == FxState::Exiting && before != FxState::Exiting && rt.current) {
        rt.exitingElapsedSeconds = 0.0f;
        rt.current->RequestExit(config_.transitionSeconds);
    }
    SyncCurrentEffectFromTimeline(rt, layer == LayerKind::Foreground ? config_.foreground : config_.background);
}

void EffectEngine::SetForegroundLayer(LayerSource foreground) {
    foreground.restMesh = BuildForegroundRestMesh(foreground);
    fg_.current.reset(); // an effect keeps pointers into the layer it began on: drop it first
    fg_.hasAppliedEntry = false;
    fg_.effectElapsedSeconds = 0.0f;
    fg_.layer = std::move(foreground);
    fgSM_.Reset(fg_.layer.emptyReason);
}

EffectEngine::LayerStatus EffectEngine::Status(LayerKind layer) const {
    const LayerRuntime& rt = layer == LayerKind::Foreground ? fg_ : bg_;
    LayerStatus status;
    status.state = rt.sm->Current();
    status.envelope = rt.sm->Envelope();
    status.effectElapsedSeconds = rt.effectElapsedSeconds;
    if (rt.current) {
        status.hasEffect = true;
        status.effect = rt.current->Id();
    }
    return status;
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
        ctx.durationSeconds = entry->durationSeconds;
        rt.current->Begin(ctx);
    }
    // Create() returning nullptr just means this EffectId isn't registered
    // yet (§16 Step 9 in progress) -- rt.current staying null means
    // AppendDrawBatch draws nothing for this layer this frame rather than
    // crashing (A.7 "クラッシュさせない" policy).
}

void EffectEngine::AppendDrawBatch(const LayerRuntime& rt) {
    const FxState state = rt.sm->Current();

    if (state == FxState::Idle || state == FxState::Rest) {
        DrawBatch batch;
        batch.texture = rt.layer.texture;
        batch.mesh = &rt.layer.restMesh;
        drawList_.batches.push_back(batch);
        return;
    }
    if (!rt.current || state == FxState::Consumed || state == FxState::Empty) {
        return;
    }

    // Crossfade exit (§5.2): the one exit strategy needing two batches from
    // a single layer in the same frame -- the effect's own batch fading out
    // against the layer's plain rest-mesh batch fading in, since a
    // Crossfade-exit effect (Kaleidoscope, InfiniteRotation, HueShift) can't
    // reach its own rest pose by animating its own geometry (§6.1.13).
    float primaryAlphaScale = 1.0f;
    float crossfadeRestAlpha = 0.0f;
    if (state == FxState::Exiting && rt.current->Exit() == ExitStrategy::Crossfade) {
        const float u = config_.transitionSeconds > 1e-4f
                            ? std::min(1.0f, rt.exitingElapsedSeconds / config_.transitionSeconds)
                            : 1.0f;
        primaryAlphaScale = 1.0f - u;
        crossfadeRestAlpha = u;
    }

    // GeometryKind::Bands (GlitchShift only, §6.2.9) is the one geometry
    // kind that's inherently 3 batches, not 1: the same band quads drawn 3
    // times with a colorMask restricting each pass to one channel and a
    // small +-rgbSplitPx x-offset layered on top of the shared transform.
    if (rt.current->Geometry() == GeometryKind::Bands) {
        const float split = rt.geometry.bandsRgbSplitPx;
        const struct { ColorMask mask; float dx; } passes[3] = {
            {{true, false, false}, -split}, {{false, true, false}, 0.0f}, {{false, false, true}, split}};
        for (const auto& pass : passes) {
            DrawBatch b;
            b.texture = rt.layer.texture;
            b.quads = &rt.geometry.quads;
            b.transform = rt.geometry.transform;
            b.transform.translate.x += pass.dx;
            b.colorMask = pass.mask;
            b.alpha = rt.geometry.alpha * primaryAlphaScale;
            drawList_.batches.push_back(b);
        }
        return; // Bands has no static-match rest form to Crossfade against (Envelope-only, §6.2.9)
    }

    // HueShift (§6.2.8) is the other inherently-multi-batch case: 2 passes
    // over the *same* rest mesh, differing only in which HueRing texture
    // index each one binds and how much alpha the second (blend-in) pass
    // gets -- unlike every other Transform effect, whose single batch
    // always uses the layer's normal Background/Foreground texture role.
    if (rt.current->Id() == EffectId::HueShift) {
        DrawBatch primary;
        primary.texture = TextureRole::HueRing;
        primary.textureIndex = rt.geometry.textureIndex;
        primary.mesh = &rt.layer.restMesh;
        primary.transform = rt.geometry.transform;
        primary.alpha = rt.geometry.alpha * primaryAlphaScale;
        drawList_.batches.push_back(primary);

        if (rt.geometry.textureIndexB >= 0 && rt.geometry.alphaB > 0.0f) {
            DrawBatch blend = primary;
            blend.textureIndex = rt.geometry.textureIndexB;
            blend.alpha = rt.geometry.alphaB * primaryAlphaScale;
            drawList_.batches.push_back(blend);
        }

        if (crossfadeRestAlpha > 0.0f) {
            DrawBatch restBatch;
            restBatch.texture = rt.layer.texture;
            restBatch.mesh = &rt.layer.restMesh;
            restBatch.alpha = crossfadeRestAlpha;
            drawList_.batches.push_back(restBatch);
        }
        return;
    }

    DrawBatch batch;
    batch.texture = rt.layer.texture;
    switch (rt.current->Geometry()) {
        case GeometryKind::Transform:
            // Whole-layer rigid transform: reuse the layer's own rest
            // geometry, just with the effect's transform/alpha applied.
            batch.mesh = &rt.layer.restMesh;
            batch.transform = rt.geometry.transform;
            batch.alpha = rt.geometry.alpha * primaryAlphaScale;
            break;
        case GeometryKind::Mesh:
        case GeometryKind::RadialMesh:
            // Most Mesh effects bake displacement directly into vertex
            // positions and leave this at the default identity, but a few
            // (Ripple, NoiseRipple) also need a coverScale wrapping the
            // whole mesh to avoid an edge gap (§6.0.4) -- same transform
            // field, applied the same way as the other geometry kinds.
            batch.mesh = &rt.geometry.mesh;
            batch.transform = rt.geometry.transform;
            batch.alpha = rt.geometry.alpha * primaryAlphaScale;
            break;
        case GeometryKind::Tiles:
        case GeometryKind::Bands: // unreachable (handled above); listed for switch completeness
        case GeometryKind::Fragments:
            batch.quads = &rt.geometry.quads;
            batch.transform = rt.geometry.transform;
            batch.alpha = rt.geometry.alpha * primaryAlphaScale;
            break;
    }
    drawList_.batches.push_back(batch);

    if (crossfadeRestAlpha > 0.0f) {
        DrawBatch restBatch;
        restBatch.texture = rt.layer.texture;
        restBatch.mesh = &rt.layer.restMesh;
        restBatch.alpha = crossfadeRestAlpha;
        drawList_.batches.push_back(restBatch);
    }
}

void EffectEngine::Update(const Inputs& in) {
    drawList_.Clear();

    UpdateLayer(bg_, config_.background, in);
    AppendDrawBatch(bg_);
    UpdateLayer(fg_, config_.foreground, in);
    AppendDrawBatch(fg_); // foreground drawn after background (§4.6)
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
        case SaverState::STATE_FADEOUT:
            // Both layers keep animating, undisturbed, while AppController
            // fades the whole screen to black on top of them -- nothing to do
            // here (§ design: no forced termination anymore).
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
