#pragma once
// spiral-suction-saver's SCRAPI surface (docs/DESIGN_VIEWER.md §B.6): the manifest it
// declares, and the binder that turns validated control changes into calls on the
// effect engine. Depends only on core::fx and src/scrapi, so all of it is
// unit-testable without Windows; the Win32 side only supplies the transport and
// the frame loop.

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

class SpiralControlBinder {
public:
    SpiralControlBinder(fx::EffectEngine& engine, SimulationClock& clock);

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
    scrapi::ServerCore* server_ = nullptr;
};

} // namespace core
