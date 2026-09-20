#include "test_framework.h"

#include <cstdio>
#include <deque>
#include <set>

#include "../src/core/DemoControls.h"
#include "../src/scrapi/ClientCore.h"

using namespace core;
using scrapi::ClientCore;
using scrapi::ControlType;
using scrapi::JsonValue;
using scrapi::Manifest;
using scrapi::ServerCore;

namespace {

// Engine-free counterpart of test_SpiralControls.cpp's SpiralRig: binder + server + client wired
// through in-memory queues, no EffectEngine (the demo has no rendering to drive from here).
struct DemoRig {
    DemoControlBinder binder;
    std::deque<std::string> toServer, toClient;
    ServerCore server;
    ClientCore client;
    double now = 1000.0;
    std::string savedIni;
    std::string savedFolder;
    int saveToConfigCalls = 0;

    DemoRig()
        : binder(MakeHooks()),
          server(BuildDemoManifest("test"), binder.Hooks(), [this](const std::string& l) { toClient.push_back(l); }),
          client([this](const std::string& l) { toServer.push_back(l); }) {
        binder.Attach(server);
        binder.ApplyInitialState();
        client.StartSession("test");
        Pump();
    }

    DemoHostHooks MakeHooks() {
        DemoHostHooks h;
        h.saveToConfig = [this](const std::string& ini, const std::string& folder) {
            ++saveToConfigCalls;
            savedIni = ini;
            savedFolder = folder;
        };
        return h;
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

    void Frame(float realDt = 1.0f / 30.0f) {
        Pump();
        binder.Tick(realDt);
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
    // Invoking a button can change other controls' values (SetValue), which only reach the
    // client on the next Flush -- rate-limited to subscribeMaxHz, so `now` must actually advance
    // between calls or a second Invoke()'s Flush is dropped as "too soon".
    void Invoke(const std::string& id) {
        client.Invoke(id, JsonValue::Object());
        Pump();
        now += 1.0;
        server.Flush(now);
        Pump();
    }
    const JsonValue* Get(const std::string& id) const { return client.model()->Get(id); }
};

} // namespace

TEST_CASE(DemoManifest_IsValidAndSurvivesJson) {
    const Manifest m = BuildDemoManifest("9.9.9");
    std::string error;
    CHECK(scrapi::ValidateManifest(m, &error));
    if (!error.empty()) std::fprintf(stderr, "%s\n", error.c_str());
    CHECK(m.saver.name == "ScrApiDemo");
    CHECK(m.saver.version == "9.9.9");

    Manifest back;
    CHECK(scrapi::ManifestFromJson(scrapi::ManifestToJson(m), back));
    CHECK(scrapi::ManifestToJson(back) == scrapi::ManifestToJson(m));
}

// The whole point of this manifest: every v1 control type appears at least once, including the
// three ScrKit.scr itself never uses (string/color/path).
TEST_CASE(DemoManifest_CoversEveryV1ControlType) {
    const Manifest m = BuildDemoManifest("t");
    std::set<ControlType> seen;
    std::vector<const scrapi::ControlNode*> stack;
    for (const auto& c : m.controls) stack.push_back(&c);
    while (!stack.empty()) {
        const scrapi::ControlNode* n = stack.back();
        stack.pop_back();
        seen.insert(n->type);
        for (const auto& c : n->children) stack.push_back(&c);
    }
    for (ControlType t : {ControlType::Group, ControlType::Enum, ControlType::Flags, ControlType::Bool,
                          ControlType::Int, ControlType::Float, ControlType::String, ControlType::Color,
                          ControlType::Path, ControlType::Button, ControlType::Readout}) {
        CHECK(seen.count(t) == 1);
    }
}

// Same coverage claim for the standard scrapi.* controls ScrKit.scr doesn't implement.
TEST_CASE(DemoManifest_CoversFpsAndSaveToConfig) {
    const Manifest m = BuildDemoManifest("t");
    CHECK(scrapi::FindControl(m, "scrapi.fps") != nullptr);
    CHECK(scrapi::FindControl(m, "scrapi.saveToConfig") != nullptr);
    const auto* seed = scrapi::FindControl(m, "scrapi.seed");
    CHECK(seed != nullptr);
    if (seed) CHECK(seed->applyRestart);
}

TEST_CASE(DemoControls_SessionStartsWithDefaults) {
    DemoRig rig;
    CHECK(rig.client.state() == ClientCore::State::Ready);
    CHECK(rig.client.saver().name == "ScrApiDemo");
    CHECK(*rig.Get("mode") == JsonValue::String("static"));
    CHECK(*rig.Get("shape") == JsonValue::String("circle"));
    CHECK(rig.Get("size")->AsInt() == 120);
    CHECK(rig.binder.state().size == 120);
}

TEST_CASE(DemoControls_SetUpdatesEveryTypeOfState) {
    DemoRig rig;
    rig.Set("mode", JsonValue::String("pulse"));
    rig.Set("shape", JsonValue::String("square"));
    rig.Set("size", JsonValue::Int(50));
    rig.Set("speed", JsonValue::Double(2.5));
    rig.Set("label", JsonValue::String("hello"));
    rig.Set("tint", JsonValue::String("#FF0000FF"));
    rig.Set("logoPath", JsonValue::String("C:\\logo.png"));
    rig.Set("outputFolder", JsonValue::String("C:\\out"));
    JsonValue tags = JsonValue::Array();
    tags.Push(JsonValue::String("grid"));
    tags.Push(JsonValue::String("border"));
    rig.Set("tags", std::move(tags));

    const DemoState& s = rig.binder.state();
    CHECK_EQ(s.mode, std::string("pulse"));
    CHECK_EQ(s.shape, std::string("square"));
    CHECK_EQ(s.size, 50);
    CHECK(s.speed > 2.49 && s.speed < 2.51);
    CHECK_EQ(s.label, std::string("hello"));
    CHECK_EQ(s.tint, std::string("#FF0000FF"));
    CHECK_EQ(s.logoPath, std::string("C:\\logo.png"));
    CHECK_EQ(s.outputFolder, std::string("C:\\out"));
    CHECK_EQ(s.tags.size(), static_cast<size_t>(2));
}

// visibleWhen `any` (tags): hidden in "static" mode, visible in "pulse"/"sweep".
TEST_CASE(DemoControls_AnyConditionGatesTagsVisibility) {
    DemoRig rig;
    CHECK(!rig.client.model()->IsVisible("tags")); // default mode is "static"
    rig.Set("mode", JsonValue::String("pulse"));
    CHECK(rig.client.model()->IsVisible("tags"));
    rig.Set("mode", JsonValue::String("sweep"));
    CHECK(rig.client.model()->IsVisible("tags"));
    rig.Set("mode", JsonValue::String("static"));
    CHECK(!rig.client.model()->IsVisible("tags"));
}

// visibleWhen `in` (label): visible for circle/square, hidden for triangle.
TEST_CASE(DemoControls_InConditionGatesLabelVisibility) {
    DemoRig rig;
    CHECK(rig.client.model()->IsVisible("label")); // default shape is "circle"
    rig.Set("shape", JsonValue::String("triangle"));
    CHECK(!rig.client.model()->IsVisible("label"));
    rig.Set("shape", JsonValue::String("square"));
    CHECK(rig.client.model()->IsVisible("label"));
}

// enabledWhen `all` (go) and `not` (resetLabel).
TEST_CASE(DemoControls_BurstButtonAndResetLabelInvoke) {
    DemoRig rig;
    CHECK(!rig.client.model()->IsEnabled("go")); // default mode "static" fails the `in` half of `all`
    rig.Set("mode", JsonValue::String("pulse")); // "go" requires mode in {pulse,sweep}
    CHECK(rig.client.model()->IsEnabled("go"));
    rig.Invoke("go");
    CHECK_EQ(rig.binder.state().burstCount, 1);
    CHECK(rig.Get("counter")->AsInt() == 1);

    CHECK(rig.client.model()->IsEnabled("resetLabel")); // default label "ScrApiDemo" != ""
    rig.Invoke("resetLabel");
    CHECK_EQ(rig.binder.state().label, std::string());
    CHECK(*rig.Get("label") == JsonValue::String(""));
    CHECK(!rig.client.model()->IsEnabled("resetLabel")); // label is now "" -- not(eq("")) is false
}

// apply:"restart" (scrapi.seed): the model sees the new value immediately, but state_.seed --
// and therefore anything derived from it -- only updates once scrapi.restart is invoked.
TEST_CASE(DemoControls_SeedIsApplyRestartNotLive) {
    DemoRig rig;
    CHECK_EQ(rig.binder.state().seed, 12345);
    rig.Set("scrapi.seed", JsonValue::Int(999));
    CHECK(rig.Get("scrapi.seed")->AsInt() == 999);       // model updated (a `get` would see this)
    CHECK_EQ(rig.binder.state().seed, 12345);            // but the state used for rendering hasn't
    rig.Invoke("scrapi.restart");
    CHECK_EQ(rig.binder.state().seed, 999);              // now it has
    CHECK_EQ(rig.binder.state().burstCount, 0);          // restart also clears the burst counter
}

TEST_CASE(DemoControls_SaveToConfigCallsHostHookWithSerializedState) {
    DemoRig rig;
    rig.Set("label", JsonValue::String("saved-label"));
    rig.Set("outputFolder", JsonValue::String("C:\\demo-out"));
    rig.Invoke("scrapi.saveToConfig");
    CHECK_EQ(rig.saveToConfigCalls, 1);
    CHECK_EQ(rig.savedFolder, std::string("C:\\demo-out"));
    CHECK(rig.savedIni.find("[ScrApiDemo]") != std::string::npos);
    CHECK(rig.savedIni.find("Label=saved-label") != std::string::npos);
}

TEST_CASE(DemoControls_TickPublishesFpsEnergyAndStatusReadouts) {
    DemoRig rig;
    rig.Run(1.0f);
    CHECK(rig.Get("scrapi.fps")->AsDouble() > 0.0);
    CHECK(rig.Get("energy")->AsDouble() >= 0.0);
    CHECK(rig.Get("energy")->AsDouble() <= 100.0);
    CHECK(!rig.Get("status")->AsString().empty());
}

TEST_CASE(DemoControls_PausedStopsElapsedTimeStepAdvancesByOneFrame) {
    DemoRig rig;
    rig.Set("scrapi.paused", JsonValue::Bool(true));
    rig.Run(1.0f);
    CHECK(rig.binder.state().elapsedSeconds == 0.0);
    rig.Invoke("scrapi.step");
    rig.Frame();
    CHECK(rig.binder.state().elapsedSeconds > 0.0);
}
