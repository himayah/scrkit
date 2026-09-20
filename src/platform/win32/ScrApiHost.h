#pragma once
// Saver-side SCRAPI plumbing for the /p preview (docs/DESIGN_VIEWER.md §B.5): owns the
// pipe, the protocol engine and the spiral control binder, and is driven once per frame
// by the render loop. Requests are only ever handled at the frame boundary on the render
// thread, so a `set` is applied atomically with respect to drawing.

#include <memory>
#include <string>

#include <windows.h>

#include "../../core/ConfigModel.h"
#include "../../core/SimulationClock.h"
#include "../../core/SpiralControls.h"
#include "../../scrapi/ServerCore.h"
#include "ScrApiPipe.h"

namespace platform {

class AppController;

class ScrApiHost {
public:
    // `app` must already be initialized and outlive this object. `viewport` is the
    // window the viewer may resize (reported in the hello reply).
    ScrApiHost(AppController& app, const core::ConfigModel& config, HWND viewport, const std::wstring& pipeName);
    ~ScrApiHost();
    ScrApiHost(const ScrApiHost&) = delete;
    ScrApiHost& operator=(const ScrApiHost&) = delete;

    // Handles everything the viewer has sent so far, then returns the dt the simulation
    // should advance by this frame (scaled/paused/stepped per the viewer's controls).
    float BeginFrame(float realDt);
    // Publishes status readouts and sends any pending change events.
    void EndFrame();

    bool active() const { return active_; }

private:
    core::SimulationClock clock_;
    core::SpiralControlBinder binder_;
    ScrApiPipe pipe_;
    std::unique_ptr<scrapi::ServerCore> server_;
    bool active_ = true;
    double seconds_ = 0.0;
};

} // namespace platform
