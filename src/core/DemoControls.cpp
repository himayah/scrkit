#include "DemoControls.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace core {

using namespace scrapi;

namespace {

EnumOption Opt(const std::string& value, const std::string& label) {
    EnumOption o;
    o.value = value;
    o.label = label;
    return o;
}

Condition Eq(const std::string& id, JsonValue value) {
    Condition c;
    c.kind = Condition::Kind::Eq;
    c.id = id;
    c.value = std::move(value);
    return c;
}
Condition InOf(const std::string& id, std::vector<JsonValue> values) {
    Condition c;
    c.kind = Condition::Kind::In;
    c.id = id;
    c.values = std::move(values);
    return c;
}
Condition AllOf(std::vector<Condition> parts) {
    Condition c;
    c.kind = Condition::Kind::All;
    c.children = std::move(parts);
    return c;
}
Condition AnyOf(std::vector<Condition> parts) {
    Condition c;
    c.kind = Condition::Kind::Any;
    c.children = std::move(parts);
    return c;
}
Condition NotOf(Condition inner) {
    Condition c;
    c.kind = Condition::Kind::Not;
    c.children = {std::move(inner)};
    return c;
}

ControlNode Group(const std::string& id, const std::string& label, std::vector<ControlNode> children) {
    ControlNode n;
    n.id = id;
    n.type = ControlType::Group;
    n.label = label;
    n.children = std::move(children);
    return n;
}

ControlNode Readout(const std::string& id, const std::string& label, const std::string& format) {
    ControlNode n;
    n.id = id;
    n.type = ControlType::Readout;
    n.label = label;
    n.format = format;
    n.readOnly = true;
    return n;
}

} // namespace

std::string SummarizeDemoState(const DemoState& s) {
    char buf[192];
    std::snprintf(buf, sizeof(buf), "%s/%s size=%dpx speed=%.2fx t=%.1fs bursts=%d", s.mode.c_str(), s.shape.c_str(),
                  s.size, s.speed, s.elapsedSeconds, s.burstCount);
    return buf;
}

std::string SerializeDemoStateToIni(const DemoState& s) {
    std::string tags;
    for (size_t i = 0; i < s.tags.size(); ++i) {
        if (i) tags += ",";
        tags += s.tags[i];
    }
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "[ScrApiDemo]\n"
                  "Mode=%s\nShape=%s\nQuality=%s\nTags=%s\nEnabled=%d\nSize=%d\nSpeed=%.4f\n"
                  "Label=%s\nTint=%s\nSeed=%d\nBurstCount=%d\n",
                  s.mode.c_str(), s.shape.c_str(), s.quality.c_str(), tags.c_str(), s.enabled ? 1 : 0, s.size, s.speed,
                  s.label.c_str(), s.tint.c_str(), s.seed, s.burstCount);
    return buf;
}

Manifest BuildDemoManifest(const std::string& version) {
    Manifest m;
    // A demo saver, deliberately unrelated to ScrKit.scr's own identity (tests/scrapi_fixture.h's
    // synthetic manifest already covers every type in isolated unit tests; this is the real,
    // runnable counterpart -- see docs/SCRAPI_V2_DRAFT.md for why it exists).
    m.saver = {"jp.himayah.scrkit.demo", "ScrApiDemo", version};
    m.capabilities = {"scrapi.paused",  "scrapi.timeScale", "scrapi.step",       "scrapi.seed",
                       "scrapi.restart", "scrapi.fps",       "scrapi.saveToConfig", "viewport.resize"};

    ControlNode mode;
    mode.id = "mode";
    mode.type = ControlType::Enum;
    mode.label = "Mode";
    mode.presentation = "dropdown";
    mode.options = {Opt("static", "Static"), Opt("pulse", "Pulse"), Opt("sweep", "Sweep")};
    mode.hasDefault = true;
    mode.defaultValue = JsonValue::String("static");

    ControlNode shape;
    shape.id = "shape";
    shape.type = ControlType::Enum;
    shape.label = "Shape";
    shape.presentation = "radio";
    shape.options = {Opt("circle", "Circle"), Opt("square", "Square"), Opt("triangle", "Triangle")};
    shape.hasDefault = true;
    shape.defaultValue = JsonValue::String("circle");

    ControlNode quality;
    quality.id = "quality";
    quality.type = ControlType::Enum;
    quality.label = "Grid quality";
    quality.presentation = "list";
    quality.options = {Opt("low", "Low"), Opt("medium", "Medium"), Opt("high", "High")};
    quality.hasDefault = true;
    quality.defaultValue = JsonValue::String("medium");

    ControlNode tags;
    tags.id = "tags";
    tags.type = ControlType::Flags;
    tags.label = "Overlay";
    tags.options = {Opt("grid", "Grid"), Opt("label", "Label"), Opt("border", "Border")};
    tags.hasDefault = true;
    tags.defaultValue = JsonValue::Array();
    tags.defaultValue.Push(JsonValue::String("label"));
    tags.visibleWhen = AnyOf({Eq("mode", JsonValue::String("pulse")), Eq("mode", JsonValue::String("sweep"))});

    ControlNode enabled;
    enabled.id = "enabled";
    enabled.type = ControlType::Bool;
    enabled.label = "Enabled";
    enabled.hasDefault = true;
    enabled.defaultValue = JsonValue::Bool(true);

    ControlNode size;
    size.id = "size";
    size.type = ControlType::Int;
    size.label = "Size";
    size.presentation = "slider";
    size.hasMin = size.hasMax = size.hasStep = true;
    size.min = 8;
    size.max = 400;
    size.step = 1;
    size.unit = "px";
    size.hasDefault = true;
    size.defaultValue = JsonValue::Int(120);
    size.visibleWhen = Eq("enabled", JsonValue::Bool(true));

    ControlNode speed;
    speed.id = "speed";
    speed.type = ControlType::Float;
    speed.label = "Speed";
    speed.presentation = "knob";
    speed.scale = "log";
    speed.hasMin = speed.hasMax = speed.hasStep = true;
    speed.min = 0.1;
    speed.max = 10.0;
    speed.step = 0.1;
    speed.unit = "x";
    speed.hasDefault = true;
    speed.defaultValue = JsonValue::Double(1.0);

    ControlNode label;
    label.id = "label";
    label.type = ControlType::String;
    label.label = "Label text";
    label.maxLength = 40;
    label.hasDefault = true;
    label.defaultValue = JsonValue::String("ScrApiDemo");
    label.visibleWhen = InOf("shape", {JsonValue::String("circle"), JsonValue::String("square")});

    ControlNode tint;
    tint.id = "tint";
    tint.type = ControlType::Color;
    tint.label = "Background tint";
    tint.alpha = true;
    tint.palette = {"#1E2A3CFF", "#3C1E2AFF", "#2A3C1EFF", "#00000000"};
    tint.hasDefault = true;
    tint.defaultValue = JsonValue::String("#1E2A3CFF");

    ControlNode logoPath;
    logoPath.id = "logoPath";
    logoPath.type = ControlType::Path;
    logoPath.label = "Logo file";
    logoPath.description = "Only the filename is shown -- this demo never decodes it";
    logoPath.pathKind = "file";
    logoPath.filters = {{"Images", "*.png;*.jpg;*.bmp"}, {"All files", "*.*"}};

    ControlNode outputFolder;
    outputFolder.id = "outputFolder";
    outputFolder.type = ControlType::Path;
    outputFolder.label = "Save-to folder";
    outputFolder.description = "Where \"Save to config\" below writes demo.ini (default: next to saver.log)";
    outputFolder.pathKind = "folder";

    ControlNode go;
    go.id = "go";
    go.type = ControlType::Button;
    go.label = "Burst";
    go.confirm = "Trigger a burst effect?";
    go.enabledWhen = AllOf({Eq("enabled", JsonValue::Bool(true)), InOf("mode", {JsonValue::String("pulse"), JsonValue::String("sweep")})});

    ControlNode resetLabel;
    resetLabel.id = "resetLabel";
    resetLabel.type = ControlType::Button;
    resetLabel.label = "Clear label";
    resetLabel.enabledWhen = NotOf(Eq("label", JsonValue::String("")));

    ControlNode status = Readout("status", "Status", "text");
    ControlNode counter = Readout("counter", "Bursts fired", "number");
    ControlNode energy = Readout("energy", "Energy", "gauge");
    energy.hasMin = energy.hasMax = true;
    energy.min = 0;
    energy.max = 100;
    ControlNode load = Readout("load", "Load", "bar");
    load.hasMin = load.hasMax = true;
    load.min = 0;
    load.max = 100;

    m.controls.push_back(Group("group.appearance", "Appearance",
                                {mode, shape, quality, tags, enabled, size, speed, label, tint, logoPath, outputFolder}));
    m.controls.push_back(Group("group.actions", "Actions", {go, resetLabel, status, counter, energy, load}));

    ControlNode paused;
    paused.id = "scrapi.paused";
    paused.type = ControlType::Bool;
    paused.label = "Paused";
    paused.hasDefault = true;
    paused.defaultValue = JsonValue::Bool(false);

    ControlNode timeScale;
    timeScale.id = "scrapi.timeScale";
    timeScale.type = ControlType::Float;
    timeScale.label = "Speed";
    timeScale.presentation = "slider";
    timeScale.hasMin = timeScale.hasMax = timeScale.hasStep = true;
    timeScale.min = 0.0;
    timeScale.max = SimulationClock::kMaxTimeScale;
    timeScale.step = 0.05;
    timeScale.unit = "x";
    timeScale.hasDefault = true;
    timeScale.defaultValue = JsonValue::Double(1.0);

    ControlNode step;
    step.id = "scrapi.step";
    step.type = ControlType::Button;
    step.label = "Step one frame";
    step.enabledWhen = Eq("scrapi.paused", JsonValue::Bool(true));

    ControlNode seed;
    seed.id = "scrapi.seed";
    seed.type = ControlType::Int;
    seed.label = "Seed";
    seed.description = "apply:\"restart\" -- stored immediately (a get sees it right away), but only "
                        "changes the animation once \"Restart\" is pressed";
    seed.applyRestart = true;
    seed.hasMin = seed.hasMax = true;
    seed.min = 0;
    seed.max = 2147483647.0;
    seed.hasDefault = true;
    seed.defaultValue = JsonValue::Int(12345);

    ControlNode restart;
    restart.id = "scrapi.restart";
    restart.type = ControlType::Button;
    restart.label = "Restart";

    ControlNode fps = Readout("scrapi.fps", "FPS", "number");

    ControlNode saveToConfig;
    saveToConfig.id = "scrapi.saveToConfig";
    saveToConfig.type = ControlType::Button;
    saveToConfig.label = "Save to config";
    saveToConfig.confirm = "Write the current values to demo.ini?";

    ControlNode simGroup =
        Group("group.simulation", "Simulation", {paused, timeScale, step, seed, restart, fps, saveToConfig});
    simGroup.presentation = "collapsed";
    m.controls.push_back(std::move(simGroup));

    return m;
}

// ---------------------------------------------------------------------------

DemoControlBinder::DemoControlBinder(DemoHostHooks host) : host_(std::move(host)) {}

void DemoControlBinder::ApplyInitialState() {
    if (!server_) return;
    const ControlModel& model = server_->model();
    auto str = [&](const char* id, std::string& out) {
        if (const JsonValue* v = model.Get(id)) out = v->AsString();
    };
    str("mode", state_.mode);
    str("shape", state_.shape);
    str("quality", state_.quality);
    str("label", state_.label);
    str("tint", state_.tint);
    str("logoPath", state_.logoPath);
    str("outputFolder", state_.outputFolder);
    if (const JsonValue* v = model.Get("tags")) {
        state_.tags.clear();
        for (const auto& item : v->items()) state_.tags.push_back(item.AsString());
    }
    if (const JsonValue* v = model.Get("enabled")) state_.enabled = v->AsBool(true);
    if (const JsonValue* v = model.Get("size")) state_.size = static_cast<int>(v->AsInt(120));
    if (const JsonValue* v = model.Get("speed")) state_.speed = v->AsDouble(1.0);
    if (const JsonValue* v = model.Get("scrapi.seed")) state_.seed = static_cast<int>(v->AsInt(12345));
    if (const JsonValue* v = model.Get("scrapi.paused")) clock_.SetPaused(v->AsBool(false));
    if (const JsonValue* v = model.Get("scrapi.timeScale")) clock_.SetTimeScale(static_cast<float>(v->AsDouble(1.0)));
}

void DemoControlBinder::Attach(ServerCore& server) { server_ = &server; }

ServerCore::Hooks DemoControlBinder::Hooks() {
    ServerCore::Hooks hooks;
    hooks.onSet = [this](const std::vector<ServerCore::Change>& c) { OnSet(c); };
    hooks.onInvoke = [this](const std::string& id, const JsonValue& args, std::string* error) {
        return OnInvoke(id, args, error);
    };
    return hooks;
}

void DemoControlBinder::OnSet(const std::vector<ServerCore::Change>& changes) {
    for (const auto& ch : changes) {
        const std::string& id = ch.id;
        if (id == "mode") state_.mode = ch.value.AsString();
        else if (id == "shape") state_.shape = ch.value.AsString();
        else if (id == "quality") state_.quality = ch.value.AsString();
        else if (id == "tags") {
            state_.tags.clear();
            for (const auto& item : ch.value.items()) state_.tags.push_back(item.AsString());
        } else if (id == "enabled") state_.enabled = ch.value.AsBool();
        else if (id == "size") state_.size = static_cast<int>(ch.value.AsInt());
        else if (id == "speed") state_.speed = ch.value.AsDouble();
        else if (id == "label") state_.label = ch.value.AsString();
        else if (id == "tint") state_.tint = ch.value.AsString();
        else if (id == "logoPath") state_.logoPath = ch.value.AsString();
        else if (id == "outputFolder") state_.outputFolder = ch.value.AsString();
        else if (id == "scrapi.paused") clock_.SetPaused(ch.value.AsBool());
        else if (id == "scrapi.timeScale") clock_.SetTimeScale(static_cast<float>(ch.value.AsDouble(1.0)));
        // "scrapi.seed" is deliberately NOT applied here (see its applyRestart=true doc comment in
        // BuildDemoManifest): the model already holds the new value (a `get` sees it), but
        // state_.seed -- and so the animation phase it feeds -- only updates in Restart().
    }
}

bool DemoControlBinder::OnInvoke(const std::string& id, const JsonValue&, std::string* error) {
    if (id == "scrapi.step") {
        clock_.RequestStep();
        return true;
    }
    if (id == "scrapi.restart") {
        Restart();
        return true;
    }
    if (id == "go") {
        ++state_.burstCount;
        burstActive_ = true;
        burstEndSeconds_ = state_.elapsedSeconds + 1.0;
        if (server_) server_->SetValue("counter", JsonValue::Int(state_.burstCount));
        return true;
    }
    if (id == "resetLabel") {
        state_.label.clear();
        if (server_) server_->SetValue("label", JsonValue::String(""));
        return true;
    }
    if (id == "scrapi.saveToConfig") {
        if (host_.saveToConfig) host_.saveToConfig(SerializeDemoStateToIni(state_), state_.outputFolder);
        return true;
    }
    if (error) *error = "unsupported action '" + id + "'";
    return false;
}

void DemoControlBinder::Restart() {
    state_.elapsedSeconds = 0.0;
    state_.burstCount = 0;
    burstActive_ = false;
    if (server_) {
        if (const JsonValue* v = server_->model().Get("scrapi.seed")) state_.seed = static_cast<int>(v->AsInt(12345));
        server_->SetValue("counter", JsonValue::Int(0));
    }
}

void DemoControlBinder::Tick(float realDt) {
    const float dt = clock_.Advance(realDt);
    state_.elapsedSeconds += dt;
    if (burstActive_ && state_.elapsedSeconds >= burstEndSeconds_) burstActive_ = false;

    if (!server_) return;
    server_->SetValue("status", JsonValue::String(SummarizeDemoState(state_)));
    // A smooth, deterministic (seed-dependent) wave -- just enough to prove `readout` values
    // stream continuously to a `subscribe`d viewer, independent of any user action.
    const double phase = state_.elapsedSeconds * state_.speed + (state_.seed % 1000) * 0.01;
    server_->SetValue("energy", JsonValue::FromFloat(static_cast<float>(50.0 + 50.0 * std::sin(phase))));
    server_->SetValue("load", JsonValue::FromFloat(static_cast<float>(50.0 + 50.0 * std::sin(phase * 0.5 + 1.0))));
    server_->SetValue("scrapi.fps", JsonValue::FromFloat(realDt > 0.0f ? 1.0f / realDt : 0.0f));
}

} // namespace core
