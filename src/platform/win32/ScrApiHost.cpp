#include "ScrApiHost.h"

#include "../../core/Logger.h"
#include "../../core/Version.h"
#include "AppController.h"

namespace platform {

namespace {
constexpr int kMaxLinesPerFrame = 64; // bounds the work a chatty viewer can cost one frame
}

namespace {
// What the spiral controls beyond the effect engine do inside the app.
core::SpiralHostHooks HooksFor(AppController& app) {
    core::SpiralHostHooks h;
    h.setContentSource = [&app](const std::string& source) { app.ApplyContentSource(source); };
    h.setMaskOverlay = [&app](bool on) { app.SetMaskOverlay(on); };
    h.restart = [&app](uint32_t seed) { app.Restart(seed); };
    h.contentInfo = [&app] { return app.ContentInfo(); };
    h.refreshContent = [&app] { app.RefreshContent(); };
    return h;
}
} // namespace

ScrApiHost::ScrApiHost(AppController& app, const core::ConfigModel& config, HWND viewport,
                       const std::wstring& pipeName)
    : binder_(*app.Engine(), clock_, HooksFor(app)) {
    server_ = std::make_unique<scrapi::ServerCore>(
        core::BuildSpiralManifest(config.effects, core::kAppVersion), binder_.Hooks(),
        [this](const std::string& line) { pipe_.SendLine(line); });
    binder_.Attach(*server_);
    app.SetCaptureHost(viewport);
    binder_.ApplyInitialState(); // e.g. build the sample desktop for the foreground
    server_->SetViewportHandle(static_cast<int64_t>(reinterpret_cast<intptr_t>(viewport)));
    pipe_.Start(pipeName);
}

ScrApiHost::~ScrApiHost() { pipe_.Stop(); }

float ScrApiHost::BeginFrame(float realDt) {
    seconds_ += realDt;
    if (!active_) return realDt;

    if (pipe_.IsFailed() || server_->closed()) {
        // Connection lost or the viewer said goodbye: stop SCRAPI, keep previewing normally.
        active_ = false;
        pipe_.Stop();
        return realDt;
    }

    std::string line;
    for (int i = 0; i < kMaxLinesPerFrame && pipe_.PopLine(line); ++i) server_->HandleLine(line);
    return clock_.Advance(realDt);
}

void ScrApiHost::EndFrame() {
    if (!active_) return;
    binder_.PublishStatus();
    server_->Flush(seconds_);
}

} // namespace platform
