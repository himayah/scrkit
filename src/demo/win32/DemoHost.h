#pragma once
// SCRAPI plumbing for ScrApiDemo.scr's /p preview -- the demo's counterpart to
// src/platform/win32/ScrApiHost.h, wired to core::DemoControlBinder instead of the spiral effect
// engine. Reuses the same saver-agnostic scrapi::ServerCore and platform::ScrApiPipe ScrKit.scr
// uses (proving those two really are saver-agnostic, not just documented as such).

#include <memory>
#include <string>

#include <windows.h>

#include "../../core/DemoControls.h"
#include "../../scrapi/ServerCore.h"
#include "../../platform/win32/ScrApiPipe.h"

namespace demo {

class DemoHost {
public:
    // `viewport` is the window the viewer may resize (reported in the hello reply).
    DemoHost(HWND viewport, const std::wstring& pipeName, const std::string& version);
    ~DemoHost();
    DemoHost(const DemoHost&) = delete;
    DemoHost& operator=(const DemoHost&) = delete;

    // Handles everything the viewer has sent so far, advances the simulation by `realDt`, and
    // flushes pending readout events. Call once per frame.
    void Tick(float realDt, double nowSeconds);

    bool active() const { return active_; }
    const core::DemoState& state() const { return binder_.state(); }
    bool burstActive() const { return binder_.burstActive(); }

private:
    core::DemoControlBinder binder_;
    platform::ScrApiPipe pipe_;
    std::unique_ptr<scrapi::ServerCore> server_;
    bool active_ = true;
};

} // namespace demo
