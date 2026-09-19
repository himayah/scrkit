#include "test_framework.h"

#include <deque>
#include <set>

#include "../src/core/ParticleGrid.h"
#include "../src/core/RandomSource.h"
#include "../src/core/SpiralControls.h"
#include "../src/core/effects/EffectCatalog.h"
#include "../src/scrapi/ClientCore.h"

using namespace core;
using core::fx::EffectId;
using core::fx::EffectKind;
using core::fx::FxState;
using core::fx::LayerKind;
using scrapi::ClientCore;
using scrapi::ControlType;
using scrapi::JsonValue;
using scrapi::Manifest;
using scrapi::ServerCore;

namespace {

fx::LayerSource MakeLayer(LayerKind kind, int gridN = 8) {
    fx::LayerSource layer;
    layer.kind = kind;
    layer.texture = kind == LayerKind::Foreground ? fx::TextureRole::Foreground : fx::TextureRole::Background;
    layer.screenW = 1920.0f;
    layer.screenH = 1080.0f;
    layer.gridN = gridN;
    ParticleGridConfig config;
    config.screenWidth = layer.screenW;
    config.screenHeight = layer.screenH;
    config.gridN = gridN;
    config.particleCount = gridN * gridN;
    layer.cells = BuildParticleGrid(config);
    for (size_t i = 0; i < layer.cells.size(); ++i) layer.cellIndices.push_back(static_cast<int>(i));
    layer.cellHalfW = (layer.screenW / gridN) * 0.55f;
    layer.cellHalfH = (layer.screenH / gridN) * 0.55f;
    return layer;
}

// Engine + binder + server + client, wired through in-memory queues -- the whole
// saver-side stack minus the Win32 transport and GL.
struct SpiralRig {
    Mt19937RandomSource rng{2024};
    fx::EffectEngine engine;
    SimulationClock clock;
    SpiralControlBinder binder;
    std::deque<std::string> toServer, toClient;
    ServerCore server;
    ClientCore client;
    double now = 1000.0;

    SpiralRig()
        : engine(fx::MakeDefaultEngineConfig(), rng),
          binder(engine, clock),
          server(BuildSpiralManifest(engine.Config(), "test"), binder.Hooks(),
                 [this](const std::string& l) { toClient.push_back(l); }),
          client([this](const std::string& l) { toServer.push_back(l); }) {
        binder.Attach(server);
        engine.SetLayers(MakeLayer(LayerKind::Foreground), MakeLayer(LayerKind::Background));
        engine.OnPhaseEntered(SaverState::STATE_CONTENT);
        client.StartSession("test");
        Pump();
    }

    void Pump() {
        for (int guard = 0; guard < 1000 && (!toServer.empty() || !toClient.empty()); ++guard) {
            while (!toServer.empty()) {
                std::string l = std::move(toServer.front());
                toServer.pop_front();
                server.HandleLine(l);
            }
            while (!toClient.empty()) {
                std::string l = std::move(toClient.front());
                toClient.pop_front();
                client.OnLine(l);
            }
        }
    }

    // One saver frame, as the Win32 loop will run it: requests first (frame boundary), then simulate.
    void Frame(float realDt = 1.0f / 30.0f) {
        Pump();
        const float dt = clock.Advance(realDt);
        fx::EffectEngine::Inputs in;
        in.dt = dt;
        in.suctionCenter = {960.0f, 540.0f};
        engine.Update(in);
        binder.PublishStatus();
        now += realDt;
        server.Flush(now);
    }
    void Run(float seconds) {
        for (float t = 0.0f; t < seconds; t += 1.0f / 30.0f) Frame();
        Pump();
    }
    void Set(const std::string& id, JsonValue v) {
        client.Set({{id, std::move(v)}});
        Pump();
    }
    std::string Str(const std::string& id) const { return client.model()->Get(id)->AsString(); }
};

} // namespace

TEST_CASE(SpiralManifest_IsValidAndSurvivesJson) {
    const Manifest m = BuildSpiralManifest(fx::MakeDefaultEngineConfig(), "9.9.9");
    std::string error;
    CHECK(scrapi::ValidateManifest(m, &error));
    if (!error.empty()) std::fprintf(stderr, "%s\n", error.c_str());
    CHECK(m.saver.version == "9.9.9");

    Manifest back;
    CHECK(scrapi::ManifestFromJson(scrapi::ManifestToJson(m), back));
    CHECK(scrapi::ManifestToJson(back) == scrapi::ManifestToJson(m));
}

TEST_CASE(SpiralManifest_CoversEveryEffectExactlyOncePerLayerDropdownAndOnceInParams) {
    const Manifest m = BuildSpiralManifest(fx::MakeDefaultEngineConfig(), "t");
    std::set<std::string> inDropdowns;
    for (const char* id : {"bg.effect", "fg.effect"}) {
        const auto* node = scrapi::FindControl(m, id);
        CHECK(node != nullptr);
        if (!node) continue;
        for (const auto& o : node->options) CHECK(inDropdowns.insert(o.value).second); // no effect on both layers
    }
    CHECK_EQ(inDropdowns.size(), static_cast<size_t>(fx::kEffectIdCount));
    for (int i = 0; i < fx::kEffectIdCount; ++i) {
        const std::string name = fx::EffectIdToString(static_cast<EffectId>(i));
        CHECK(inDropdowns.count(name) == 1);
        CHECK(scrapi::FindControl(m, "fx." + name) != nullptr);
        CHECK(scrapi::FindControl(m, "fx." + name + ".intensity") != nullptr);
    }
}

TEST_CASE(SpiralManifest_DefaultsMirrorTheEngineConfiguration) {
    fx::EngineConfig config = fx::MakeDefaultEngineConfig();
    config.background.perEffect[EffectId::Ripple].enabled = false;
    config.foregroundDirective.mode = fx::LayerMode::Rest;
    const Manifest m = BuildSpiralManifest(config, "t");
    scrapi::ControlModel model{Manifest(m)};
    CHECK(*model.Get("bg.mode") == JsonValue::String("auto"));
    CHECK(*model.Get("fg.mode") == JsonValue::String("rest"));
    CHECK(model.Get("bg.minSeconds")->AsDouble() == 8.0);
    CHECK(model.Get("bg.maxSeconds")->AsDouble() == 15.0);
    CHECK(model.Get("fg.minSeconds")->AsDouble() == 5.0);
    // Ripple was disabled in the config, so it's absent from the pool default; Tilt is present.
    bool ripple = false, tilt = false;
    for (const auto& v : model.Get("bg.pool")->items()) {
        ripple = ripple || v.AsString() == "Ripple";
        tilt = tilt || v.AsString() == "Tilt";
    }
    CHECK(!ripple);
    CHECK(tilt);
    CHECK(model.Get("fx.Ripple.intensity")->AsDouble() == 0.6);
    CHECK(model.Get("fx.FlagWave.intensity")->AsDouble() == 0.7);
}

TEST_CASE(SpiralControls_SessionStartsWithEveryControlPopulated) {
    SpiralRig rig;
    CHECK(rig.client.state() == ClientCore::State::Ready);
    CHECK(rig.client.saver().name == "Spiral Suction Saver");
    CHECK(rig.client.model() != nullptr);
    CHECK(rig.client.model()->Get("scrapi.timeScale")->AsDouble() == 1.0);
}

TEST_CASE(SpiralControls_PinningABackgroundEffectPlaysOnlyThatEffectAndReportsIt) {
    SpiralRig rig;
    rig.Set("bg.mode", JsonValue::String("pin"));
    rig.Set("bg.effect", JsonValue::String("Ripple"));
    rig.Run(10.0f);
    const auto status = rig.engine.Status(LayerKind::Background);
    CHECK(status.hasEffect);
    CHECK(status.effect == EffectId::Ripple);
    // and the viewer sees it through the readouts
    CHECK(rig.Str("bg.currentEffect") == "Ripple");
    CHECK(rig.Str("bg.state") == "running");
}

TEST_CASE(SpiralControls_ForegroundRestWithBackgroundPinIsTheBackgroundOnlyPreview) {
    SpiralRig rig;
    rig.Set("fg.mode", JsonValue::String("rest"));
    rig.Set("bg.mode", JsonValue::String("pin"));
    rig.Set("bg.effect", JsonValue::String("Tilt"));
    rig.Run(5.0f);
    CHECK(rig.engine.Status(LayerKind::Foreground).state == FxState::Rest);
    CHECK(rig.engine.Status(LayerKind::Background).effect == EffectId::Tilt);
    CHECK(rig.Str("fg.state") == "rest");
}

TEST_CASE(SpiralControls_VisibilityFollowsTheModeAndTheChosenEffect) {
    SpiralRig rig;
    const auto* model = rig.client.model();
    CHECK(model->IsVisible("bg.mode"));
    CHECK(!model->IsVisible("bg.effect"));          // Auto: no effect dropdown
    CHECK(model->IsVisible("bg.pool"));             // Auto: the pool is editable
    CHECK(!model->IsVisible("fx.Ripple.intensity"));

    rig.Set("bg.mode", JsonValue::String("pin"));
    rig.Set("bg.effect", JsonValue::String("Ripple"));
    CHECK(model->IsVisible("bg.effect"));
    CHECK(!model->IsVisible("bg.pool"));
    CHECK(!model->IsVisible("bg.loopTerminal"));    // Ripple is continuous
    CHECK(model->IsVisible("fx.Ripple.intensity"));
    CHECK(!model->IsVisible("fx.Tilt.intensity"));

    rig.Set("bg.effect", JsonValue::String("BackgroundSuction"));
    CHECK(model->IsVisible("bg.loopTerminal"));     // terminal effect: repeat flag applies
    CHECK(model->IsVisible("fx.BackgroundSuction.intensity"));
}

TEST_CASE(SpiralControls_EditingTheHiddenEffectDropdownInAutoModeDoesNotRestartTheLayer) {
    SpiralRig rig;
    rig.Run(2.0f);
    const auto before = rig.engine.Status(LayerKind::Background);
    rig.Set("bg.effect", JsonValue::String("Tilt")); // still in Auto
    rig.Run(0.3f);
    const auto after = rig.engine.Status(LayerKind::Background);
    CHECK(after.effectElapsedSeconds > before.effectElapsedSeconds); // the same effect kept running
    CHECK(rig.engine.Config().backgroundDirective.mode == fx::LayerMode::Auto);
}

TEST_CASE(SpiralControls_PoolRestrictsWhatAutoModeMayPick) {
    SpiralRig rig;
    JsonValue only = JsonValue::Array();
    only.Push(JsonValue::String("Tilt"));
    rig.Set("bg.pool", only);
    // Let the currently running effect finish and be re-picked a few times.
    rig.Run(120.0f);
    std::set<EffectId> seen;
    for (int frame = 0; frame < 30 * 120; ++frame) {
        rig.Frame();
        const auto s = rig.engine.Status(LayerKind::Background);
        if (s.hasEffect) seen.insert(s.effect);
    }
    CHECK(seen.size() == 1);
    CHECK(seen.count(EffectId::Tilt) == 1);
}

TEST_CASE(SpiralControls_EffectParametersEditTheLiveConfig) {
    SpiralRig rig;
    rig.Set("fx.Ripple.intensity", JsonValue::Double(0.25));
    rig.Set("fx.FlagWave.minSeconds", JsonValue::Double(3.0));
    rig.Set("fx.FlagWave.maxSeconds", JsonValue::Double(4.0));
    CHECK_NEAR(rig.engine.Config().background.perEffect.at(EffectId::Ripple).intensity, 0.25f, 1e-6f);
    CHECK_NEAR(rig.engine.Config().foreground.perEffect.at(EffectId::FlagWave).minSeconds, 3.0f, 1e-6f);
    CHECK_NEAR(rig.engine.Config().foreground.perEffect.at(EffectId::FlagWave).maxSeconds, 4.0f, 1e-6f);
    // out of range is clamped by the model before it ever reaches the engine
    rig.Set("fx.Ripple.intensity", JsonValue::Double(5.0));
    CHECK_NEAR(rig.engine.Config().background.perEffect.at(EffectId::Ripple).intensity, 1.0f, 1e-6f);
}

TEST_CASE(SpiralControls_MinDurationCannotExceedMaxAndTheViewerIsToldTheCorrection) {
    SpiralRig rig;
    rig.Set("bg.minSeconds", JsonValue::Double(50.0)); // default max is 15
    CHECK_NEAR(rig.engine.Config().background.defaultMinSeconds, 50.0f, 1e-6f);
    CHECK_NEAR(rig.engine.Config().background.defaultMaxSeconds, 50.0f, 1e-6f);
    rig.Run(0.5f); // flush delivers the corrected max to the viewer
    CHECK(rig.client.model()->Get("bg.maxSeconds")->AsDouble() == 50.0);

    rig.Set("bg.maxSeconds", JsonValue::Double(20.0)); // now below min
    CHECK_NEAR(rig.engine.Config().background.defaultMinSeconds, 20.0f, 1e-6f);
    rig.Run(0.5f);
    CHECK(rig.client.model()->Get("bg.minSeconds")->AsDouble() == 20.0);
}

TEST_CASE(SpiralControls_PauseAndStepDriveTheSimulationClock) {
    SpiralRig rig;
    rig.Run(1.0f);
    rig.Set("scrapi.paused", JsonValue::Bool(true));
    const float t0 = rig.engine.Status(LayerKind::Background).effectElapsedSeconds;
    rig.Run(2.0f);
    CHECK_NEAR(rig.engine.Status(LayerKind::Background).effectElapsedSeconds, t0, 1e-6f); // frozen

    CHECK(rig.client.model()->IsEnabled("scrapi.step"));
    bool ok = false;
    rig.client.Invoke("scrapi.step", JsonValue::Null(), [&](const ClientCore::Reply& r) { ok = r.ok; });
    rig.Frame();
    CHECK(ok);
    CHECK_NEAR(rig.engine.Status(LayerKind::Background).effectElapsedSeconds, t0 + SimulationClock::kStepSeconds, 1e-5f);
    rig.Frame();
    CHECK_NEAR(rig.engine.Status(LayerKind::Background).effectElapsedSeconds, t0 + SimulationClock::kStepSeconds, 1e-5f); // one step only

    rig.Set("scrapi.paused", JsonValue::Bool(false));
    CHECK(!rig.client.model()->IsEnabled("scrapi.step"));
}

TEST_CASE(SpiralControls_TimeScaleScalesSimulatedTime) {
    SpiralRig rig;
    rig.Set("scrapi.timeScale", JsonValue::Double(2.0));
    CHECK_NEAR(rig.clock.timeScale(), 2.0f, 1e-6f);
    CHECK_NEAR(rig.clock.Advance(0.1f), 0.2f, 1e-6f);
    rig.Set("scrapi.timeScale", JsonValue::Double(100.0)); // clamped by the manifest's max
    CHECK_NEAR(rig.clock.timeScale(), SimulationClock::kMaxTimeScale, 1e-6f);
}

TEST_CASE(SpiralControls_UnknownInvokeIsReportedNotIgnored) {
    SpiralRig rig;
    std::string code;
    rig.client.Invoke("bg.mode", JsonValue::Null(), [&](const ClientCore::Reply& r) { code = r.errorCode; });
    rig.Pump();
    CHECK(code == scrapi::err::kBadRequest); // not a button
}

TEST_CASE(SimulationClock_Semantics) {
    SimulationClock c;
    CHECK_NEAR(c.Advance(0.5f), 0.5f, 1e-6f);
    c.SetTimeScale(0.5f);
    CHECK_NEAR(c.Advance(0.5f), 0.25f, 1e-6f);
    c.SetTimeScale(-3.0f);
    CHECK_NEAR(c.timeScale(), 0.0f, 1e-6f);
    c.SetTimeScale(1.0f);
    c.RequestStep();                       // a step requested while running is dropped, not banked
    CHECK_NEAR(c.Advance(0.1f), 0.1f, 1e-6f);
    c.SetPaused(true);
    CHECK_NEAR(c.Advance(0.1f), 0.0f, 1e-6f);
    c.RequestStep();
    CHECK_NEAR(c.Advance(0.1f), SimulationClock::kStepSeconds, 1e-6f);
    CHECK_NEAR(c.Advance(0.1f), 0.0f, 1e-6f);
}

namespace {
struct HostLog {
    std::vector<std::string> sources;
    std::vector<bool> overlays;
    std::vector<uint32_t> restarts;
    std::string info = "412 cells, 3 windows";
    int refreshes = 0;
};

core::SpiralHostHooks HooksFor(HostLog& log) {
    core::SpiralHostHooks h;
    h.setContentSource = [&log](const std::string& s) { log.sources.push_back(s); };
    h.setMaskOverlay = [&log](bool on) { log.overlays.push_back(on); };
    h.restart = [&log](uint32_t seed) { log.restarts.push_back(seed); };
    h.contentInfo = [&log] { return log.info; };
    h.refreshContent = [&log] { ++log.refreshes; };
    return h;
}

// Same wiring as SpiralRig, but with host hooks.
struct HostRig {
    HostLog log;
    Mt19937RandomSource rng{7};
    fx::EffectEngine engine;
    SimulationClock clock;
    SpiralControlBinder binder;
    std::deque<std::string> toServer, toClient;
    ServerCore server;
    ClientCore client;

    HostRig()
        : engine(fx::MakeDefaultEngineConfig(), rng),
          binder(engine, clock, HooksFor(log)),
          server(BuildSpiralManifest(engine.Config(), "t"), binder.Hooks(), [this](const std::string& l) { toClient.push_back(l); }),
          client([this](const std::string& l) { toServer.push_back(l); }) {
        binder.Attach(server);
        engine.SetLayers(MakeLayer(LayerKind::Foreground), MakeLayer(LayerKind::Background));
        engine.OnPhaseEntered(SaverState::STATE_CONTENT);
        client.StartSession("test");
        Pump();
    }
    void Pump() {
        for (int guard = 0; guard < 1000 && (!toServer.empty() || !toClient.empty()); ++guard) {
            while (!toServer.empty()) {
                std::string l = std::move(toServer.front());
                toServer.pop_front();
                server.HandleLine(l);
            }
            while (!toClient.empty()) {
                std::string l = std::move(toClient.front());
                toClient.pop_front();
                client.OnLine(l);
            }
        }
    }
    void Set(const std::string& id, JsonValue v) {
        client.Set({{id, std::move(v)}});
        Pump();
    }
};
} // namespace

TEST_CASE(SpiralHost_InitialStateAppliesTheDefaultsOnce) {
    HostRig rig;
    rig.binder.ApplyInitialState();
    CHECK_EQ(rig.log.sources.size(), static_cast<size_t>(1));
    CHECK(rig.log.sources[0] == "sample");
    CHECK_EQ(rig.log.overlays.size(), static_cast<size_t>(1));
    CHECK(rig.log.overlays[0] == false);
}

TEST_CASE(SpiralHost_ContentSourceAndOverlayControlsReachTheHost) {
    HostRig rig;
    rig.Set("content.source", JsonValue::String("none"));
    rig.Set("mask.overlay", JsonValue::Bool(true));
    CHECK_EQ(rig.log.sources.size(), static_cast<size_t>(1));
    CHECK(rig.log.sources[0] == "none");
    CHECK_EQ(rig.log.overlays.size(), static_cast<size_t>(1));
    CHECK(rig.log.overlays[0] == true);
}

TEST_CASE(SpiralHost_SeedChangeAndRestartButtonRestartWithTheSeed) {
    HostRig rig;
    rig.Set("scrapi.seed", JsonValue::Int(777));
    CHECK_EQ(rig.log.restarts.size(), static_cast<size_t>(1));
    CHECK_EQ(rig.log.restarts[0], static_cast<uint32_t>(777));

    bool ok = false;
    rig.client.Invoke("scrapi.restart", JsonValue::Null(), [&](const ClientCore::Reply& r) { ok = r.ok; });
    rig.Pump();
    CHECK(ok);
    CHECK_EQ(rig.log.restarts.size(), static_cast<size_t>(2));
    CHECK_EQ(rig.log.restarts[1], static_cast<uint32_t>(777)); // the current seed
}

TEST_CASE(SpiralHost_CaptureAgainIsOnlyShownForTheDesktopSourceAndInvokesTheHost) {
    HostRig rig;
    CHECK(!rig.client.model()->IsVisible("content.refresh"));   // sample: nothing to recapture
    rig.Set("content.source", JsonValue::String("desktop"));
    CHECK(rig.log.sources.back() == "desktop");
    CHECK(rig.client.model()->IsVisible("content.refresh"));
    bool ok = false;
    rig.client.Invoke("content.refresh", JsonValue::Null(), [&](const ClientCore::Reply& r) { ok = r.ok; });
    rig.Pump();
    CHECK(ok);
    CHECK_EQ(rig.log.refreshes, 1);
}

TEST_CASE(SpiralHost_ContentInfoIsPublishedAsAReadout) {
    HostRig rig;
    rig.binder.PublishStatus();
    rig.server.Flush(1000.0);
    rig.Pump();
    CHECK(rig.client.model()->Get("content.info")->AsString() == "412 cells, 3 windows");
}

TEST_CASE(SpiralManifest_ContentAndSimulationControlsExist) {
    const Manifest m = BuildSpiralManifest(fx::MakeDefaultEngineConfig(), "t");
    std::string error;
    CHECK(scrapi::ValidateManifest(m, &error));
    for (const char* id : {"content.source", "content.refresh", "mask.overlay", "content.info", "scrapi.seed", "scrapi.restart"}) {
        CHECK(scrapi::FindControl(m, id) != nullptr);
    }
    CHECK(scrapi::FindControl(m, "group.content")->presentation == "collapsed");
    CHECK(m.HasCapability("scrapi.seed") && m.HasCapability("scrapi.restart"));
}
