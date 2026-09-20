#pragma once
// ScrApiDemo.scr's SCRAPI surface: a deliberately trivial screensaver whose only real job is to
// exercise every SCRAPI v1 control type, standard `scrapi.*` control, and condition kind, including
// the ones ScrKit.scr itself never happens to use (`string`/`color`/`path`, `scrapi.fps`/
// `scrapi.saveToConfig`, `apply:"restart"`, and the `in`/`all`/`any`/`not` condition kinds -- ScrKit
// only ever needed `eq`). See docs/SCRAPI_V2_DRAFT.md for the survey that found these gaps.
// Depends only on core::SimulationClock and src/scrapi, so (like SpiralControls) it's fully
// unit-testable without Windows; the Win32 side only supplies the transport, the frame loop and the
// (intentionally minimal, GDI-only) rendering.

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "SimulationClock.h"
#include "../scrapi/ControlModel.h"
#include "../scrapi/Manifest.h"
#include "../scrapi/ServerCore.h"

namespace core {

// Everything the renderer needs to draw one frame, and everything `scrapi.saveToConfig` persists.
// Deliberately a flat plain-data struct (no behavior) so the Win32 renderer can just read it.
struct DemoState {
    std::string mode = "static";        // static | pulse | sweep
    std::string shape = "circle";       // circle | square | triangle
    std::string quality = "medium";     // low | medium | high
    std::vector<std::string> tags = {"label"}; // subset of grid | label | border -- matches the
                                                // manifest's own default so /s (no SCRAPI at all,
                                                // see docs/SCRAPI_SPEC.md §7) looks the same as a
                                                // freshly-connected /p with nothing changed yet.
    bool enabled = true;
    int size = 120;                     // px
    double speed = 1.0;                 // animation speed multiplier (independent of scrapi.timeScale)
    std::string label = "ScrApiDemo";
    std::string tint = "#1E2A3CFF";     // background fill, "#RRGGBBAA"
    std::string logoPath;               // path (file) -- filename only is ever shown, never decoded
    std::string outputFolder;           // path (folder) -- where scrapi.saveToConfig writes, if set

    bool paused = false;
    float timeScale = 1.0f;
    int seed = 12345;                   // apply:"restart" -- only takes effect on the next Restart()

    double elapsedSeconds = 0.0;        // advances by SimulationClock::Advance() each frame
    int burstCount = 0;                 // scrapi.saveToConfig-visible, incremented by the "go" button
};

// Renders the current state into a stable one-line summary (the `status` readout).
std::string SummarizeDemoState(const DemoState& s);

// Renders the state as an INI-style text block ("scrapi.saveToConfig"). Pure string building, no
// file I/O here -- the Win32 host writes the returned text wherever it wants (see
// DemoHostHooks::saveToConfig).
std::string SerializeDemoStateToIni(const DemoState& s);

scrapi::Manifest BuildDemoManifest(const std::string& version);

// What the Win32 host implements for the one control whose effect is genuinely platform-specific:
// persisting `scrapi.saveToConfig`'s text somewhere real. Optional; a missing hook makes the button
// a no-op (still returns ok to the viewer -- see DemoControlBinder::OnInvoke).
struct DemoHostHooks {
    std::function<void(const std::string& iniText, const std::string& outputFolder)> saveToConfig;
};

class DemoControlBinder {
public:
    explicit DemoControlBinder(DemoHostHooks host = {});

    // Applies the manifest's declared defaults to `state_` once, after Attach. Mirrors
    // SpiralControlBinder::ApplyInitialState -- the model already holds the defaults (ServerCore
    // seeds it from the manifest), so this just copies them into the plain state struct the
    // renderer reads.
    void ApplyInitialState();

    void Attach(scrapi::ServerCore& server);
    scrapi::ServerCore::Hooks Hooks();

    void OnSet(const std::vector<scrapi::ServerCore::Change>& changes);
    bool OnInvoke(const std::string& id, const scrapi::JsonValue& args, std::string* error);

    // Advances the simulation clock by `realDt` (honoring paused/timeScale/step) and updates the
    // status/counter/energy/load readouts. Call once per frame, before ServerCore::Flush.
    void Tick(float realDt);

    const DemoState& state() const { return state_; }
    SimulationClock& clock() { return clock_; }
    // True for one simulated second after the "go" button (§DemoManifest's "Burst") fires.
    bool burstActive() const { return burstActive_; }

private:
    void Restart();

    DemoState state_;
    SimulationClock clock_;
    DemoHostHooks host_;
    scrapi::ServerCore* server_ = nullptr;
    bool burstActive_ = false;
    double burstEndSeconds_ = 0.0;
};

} // namespace core
