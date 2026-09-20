#pragma once
// ScrKit's spiral-suction saver SCRAPI surface (docs/DESIGN_VIEWER.md §B.6): the manifest it
// declares, and the binder that turns validated control changes into calls on the
// effect engine. Depends only on core::fx and src/scrapi, so all of it is
// unit-testable without Windows; the Win32 side only supplies the transport and
// the frame loop.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "SimulationClock.h"
#include "effects/EffectEngine.h"
#include "effects/EffectParams.h"
#include "../scrapi/ControlModel.h"
#include "../scrapi/Manifest.h"
#include "../scrapi/ServerCore.h"

namespace core {

// Builds the manifest from the effect catalogs and `defaults` (so it can never drift
// out of sync with the effects that actually exist, nor with the engine's starting
// configuration). The result passes scrapi::ValidateManifest.
scrapi::Manifest BuildSpiralManifest(const fx::EngineConfig& defaults, const std::string& version);

// What the host application (the SCRAPI preview) implements for the controls that go beyond
// the effect engine: which desktop content the foreground shows, the mask debug overlay, and
// restarting with a seed. All optional; a missing one makes that control a no-op.
struct SpiralHostHooks {
    std::function<void(const std::string& source)> setContentSource; // "sample" | "none"
    std::function<void(bool on)> setMaskOverlay;
    std::function<void(uint32_t seed)> restart;
    std::function<std::string()> contentInfo; // shown in the `content.info` readout
    std::function<void()> refreshContent;     // "Capture again": rebuild from the current source
    std::function<void()> dumpDebug;          // "Save debug images": write the capture and the reference to disk
};

class SpiralControlBinder {
public:
    SpiralControlBinder(fx::EffectEngine& engine, SimulationClock& clock, SpiralHostHooks host = {});

    // Applies the model's initial values of the host-backed controls (content source, overlay)
    // once, after Attach: the manifest's defaults are the state the preview should start in.
    void ApplyInitialState();

    // Must be called once, before any request is handled: the binder reads the current
    // values of related controls (mode + effect + loop flag make one directive) from
    // the server's model, and publishes readouts through it.
    void Attach(scrapi::ServerCore& server);

    // The hooks to construct the ServerCore with. They capture `this`.
    scrapi::ServerCore::Hooks Hooks();

    void OnSet(const std::vector<scrapi::ServerCore::Change>& changes);
    bool OnInvoke(const std::string& id, const scrapi::JsonValue& args, std::string* error);

    // Pushes the readouts (current effect / state per layer) into the server. Call once
    // per frame, then scrapi::ServerCore::Flush.
    void PublishStatus();

private:
    void ApplyLayerDirective(fx::LayerKind layer);
    fx::LayerDirective DirectiveFromModel(fx::LayerKind layer) const;

    fx::EffectEngine& engine_;
    SimulationClock& clock_;
    SpiralHostHooks host_;
    scrapi::ServerCore* server_ = nullptr;
};

} // namespace core
