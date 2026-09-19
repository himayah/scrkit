#include "SpiralControls.h"

#include <algorithm>

#include "effects/EffectCatalog.h"
#include "effects/EffectTypes.h"

namespace core {

using namespace scrapi;
using fx::EffectId;
using fx::EffectKind;
using fx::LayerKind;
using fx::LayerMode;

namespace {

const char* Prefix(LayerKind layer) { return layer == LayerKind::Foreground ? "fg" : "bg"; }
const char* LayerLabel(LayerKind layer) { return layer == LayerKind::Foreground ? "Foreground" : "Background"; }

// Every effect available on `layer`: continuous ones first, then terminal ones.
std::vector<EffectId> LayerEffects(LayerKind layer) {
    std::vector<EffectId> out = fx::CatalogFor(layer, EffectKind::Continuous);
    const auto& terminal = fx::CatalogFor(layer, EffectKind::Terminal);
    out.insert(out.end(), terminal.begin(), terminal.end());
    return out;
}

// Which layer an effect belongs to (each of the 26 EffectIds is on exactly one).
bool LayerOfEffect(EffectId id, LayerKind& out) {
    for (LayerKind layer : {LayerKind::Foreground, LayerKind::Background}) {
        if (fx::EffectKindOn(layer, id)) {
            out = layer;
            return true;
        }
    }
    return false;
}

EnumOption Opt(const std::string& value, const std::string& label, const std::string& description = "") {
    EnumOption o;
    o.value = value;
    o.label = label;
    o.description = description;
    return o;
}

Condition Eq(const std::string& id, JsonValue value) {
    Condition c;
    c.kind = Condition::Kind::Eq;
    c.id = id;
    c.value = std::move(value);
    return c;
}
Condition AllOf(std::vector<Condition> parts) {
    Condition c;
    c.kind = Condition::Kind::All;
    c.children = std::move(parts);
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

ControlNode Float(const std::string& id, const std::string& label, double min, double max, double step,
                  double def, const char* unit = "") {
    ControlNode n;
    n.id = id;
    n.type = ControlType::Float;
    n.label = label;
    n.hasMin = n.hasMax = n.hasStep = true;
    n.min = min;
    n.max = max;
    n.step = step;
    n.unit = unit;
    n.hasDefault = true;
    n.defaultValue = JsonValue::Double(def);
    return n;
}

ControlNode Readout(const std::string& id, const std::string& label) {
    ControlNode n;
    n.id = id;
    n.type = ControlType::Readout;
    n.label = label;
    n.format = "text";
    n.readOnly = true;
    return n;
}

double Round7(float v) { return JsonValue::FromFloat(v).AsDouble(); }

ControlNode LayerGroup(LayerKind layer, const fx::LayerEffectConfig& defaults, const fx::LayerDirective& directive) {
    const std::string p = Prefix(layer);
    const std::vector<EffectId> effects = LayerEffects(layer);

    ControlNode mode;
    mode.id = p + ".mode";
    mode.type = ControlType::Enum;
    mode.label = "Mode";
    mode.presentation = "radio";
    mode.options = {Opt("auto", "Auto", "Random cycling, as the screensaver normally does"),
                    Opt("rest", "Rest", "No effect: the layer stays at its static pose"),
                    Opt("pin", "Pin", "Play exactly one chosen effect")};
    mode.hasDefault = true;
    mode.defaultValue = JsonValue::String(directive.mode == LayerMode::Rest  ? "rest"
                                          : directive.mode == LayerMode::Pin ? "pin"
                                                                             : "auto");

    ControlNode effect;
    effect.id = p + ".effect";
    effect.type = ControlType::Enum;
    effect.label = "Effect";
    effect.presentation = "dropdown";
    for (EffectId id : effects) {
        const bool terminal = fx::EffectKindOn(layer, id) == EffectKind::Terminal;
        effect.options.push_back(Opt(fx::EffectIdToString(id), fx::EffectIdToString(id),
                                     terminal ? "Terminal effect (plays to its end)" : "Continuous effect"));
    }
    effect.hasDefault = true;
    effect.defaultValue = JsonValue::String(fx::EffectIdToString(
        directive.mode == LayerMode::Pin && fx::EffectKindOn(layer, directive.pinned) ? directive.pinned : effects.front()));
    effect.visibleWhen = Eq(mode.id, JsonValue::String("pin"));

    // Only meaningful when the pinned effect is a terminal one.
    ControlNode loop;
    loop.id = p + ".loopTerminal";
    loop.type = ControlType::Bool;
    loop.label = "Repeat when finished";
    loop.hasDefault = true;
    loop.defaultValue = JsonValue::Bool(directive.loopTerminal);
    {
        Condition anyTerminal;
        anyTerminal.kind = Condition::Kind::In;
        anyTerminal.id = effect.id;
        for (EffectId id : fx::CatalogFor(layer, EffectKind::Terminal)) {
            anyTerminal.values.push_back(JsonValue::String(fx::EffectIdToString(id)));
        }
        loop.visibleWhen = AllOf({Eq(mode.id, JsonValue::String("pin")), anyTerminal});
    }

    // The set of effects Auto mode may pick from.
    ControlNode pool;
    pool.id = p + ".pool";
    pool.type = ControlType::Flags;
    pool.label = "Auto pool";
    pool.description = "Effects Auto mode may choose from";
    pool.options = effect.options;
    pool.hasDefault = true;
    pool.defaultValue = JsonValue::Array();
    for (EffectId id : effects) {
        if (fx::ParamsFor(defaults, id).enabled) pool.defaultValue.Push(JsonValue::String(fx::EffectIdToString(id)));
    }
    pool.visibleWhen = Eq(mode.id, JsonValue::String("auto"));

    ControlNode minS = Float(p + ".minSeconds", "Min duration", 0.5, 120.0, 0.5, Round7(defaults.defaultMinSeconds), "s");
    ControlNode maxS = Float(p + ".maxSeconds", "Max duration", 0.5, 120.0, 0.5, Round7(defaults.defaultMaxSeconds), "s");
    minS.description = "Default duration range of one effect on this layer";

    ControlNode current = Readout(p + ".currentEffect", "Current effect");
    ControlNode state = Readout(p + ".state", "State");

    return Group("group." + p, LayerLabel(layer), {mode, effect, loop, pool, minS, maxS, current, state});
}

ControlNode EffectParamGroup(LayerKind layer, EffectId id, const fx::EffectParams& defaults) {
    const std::string name = fx::EffectIdToString(id);
    const std::string base = "fx." + name;
    const bool terminal = fx::EffectKindOn(layer, id) == EffectKind::Terminal;

    ControlNode intensity = Float(base + ".intensity", "Intensity", 0.0, 1.0, 0.05, Round7(defaults.intensity));
    intensity.presentation = "knob";
    ControlNode minS = Float(base + ".minSeconds", "Min duration", 0.0, 120.0, 0.5, Round7(defaults.minSeconds), "s");
    ControlNode maxS = Float(base + ".maxSeconds", "Max duration", 0.0, 120.0, 0.5, Round7(defaults.maxSeconds), "s");
    minS.description = maxS.description = "0 = use the layer's default duration";

    ControlNode g = Group(base, name + (terminal ? " (terminal)" : ""), {intensity, minS, maxS});
    g.visibleWhen = AllOf({Eq(std::string(Prefix(layer)) + ".mode", JsonValue::String("pin")),
                           Eq(std::string(Prefix(layer)) + ".effect", JsonValue::String(name))});
    return g;
}

} // namespace

Manifest BuildSpiralManifest(const fx::EngineConfig& defaults, const std::string& version) {
    Manifest m;
    m.saver = {"jp.himayah.spiral-suction-saver", "Spiral Suction Saver", version};
    m.capabilities = {"scrapi.paused", "scrapi.timeScale", "scrapi.step", "viewport.resize"};

    m.controls.push_back(LayerGroup(LayerKind::Background, defaults.background, defaults.backgroundDirective));
    m.controls.push_back(LayerGroup(LayerKind::Foreground, defaults.foreground, defaults.foregroundDirective));

    std::vector<ControlNode> params;
    for (LayerKind layer : {LayerKind::Background, LayerKind::Foreground}) {
        const auto& layerConfig = layer == LayerKind::Foreground ? defaults.foreground : defaults.background;
        for (EffectId id : LayerEffects(layer)) params.push_back(EffectParamGroup(layer, id, fx::ParamsFor(layerConfig, id)));
    }
    m.controls.push_back(Group("group.effectParams", "Effect parameters", std::move(params)));

    ControlNode paused;
    paused.id = "scrapi.paused";
    paused.type = ControlType::Bool;
    paused.label = "Paused";
    paused.hasDefault = true;
    paused.defaultValue = JsonValue::Bool(false);

    ControlNode timeScale = Float("scrapi.timeScale", "Speed", 0.0, SimulationClock::kMaxTimeScale, 0.05, 1.0, "x");
    timeScale.presentation = "slider";

    ControlNode step;
    step.id = "scrapi.step";
    step.type = ControlType::Button;
    step.label = "Step one frame";
    step.enabledWhen = Eq("scrapi.paused", JsonValue::Bool(true));

    m.controls.push_back(Group("group.simulation", "Simulation", {paused, timeScale, step}));
    return m;
}

// ---------------------------------------------------------------------------

SpiralControlBinder::SpiralControlBinder(fx::EffectEngine& engine, SimulationClock& clock)
    : engine_(engine), clock_(clock) {}

void SpiralControlBinder::Attach(ServerCore& server) { server_ = &server; }

ServerCore::Hooks SpiralControlBinder::Hooks() {
    ServerCore::Hooks hooks;
    hooks.onSet = [this](const std::vector<ServerCore::Change>& c) { OnSet(c); };
    hooks.onInvoke = [this](const std::string& id, const JsonValue& args, std::string* error) {
        return OnInvoke(id, args, error);
    };
    return hooks;
}

fx::LayerDirective SpiralControlBinder::DirectiveFromModel(LayerKind layer) const {
    fx::LayerDirective d;
    if (!server_) return d;
    const ControlModel& model = server_->model();
    const std::string p = Prefix(layer);
    const JsonValue* mode = model.Get(p + ".mode");
    const JsonValue* effect = model.Get(p + ".effect");
    const JsonValue* loop = model.Get(p + ".loopTerminal");
    if (mode && mode->AsString() == "rest") d.mode = LayerMode::Rest;
    else if (mode && mode->AsString() == "pin") d.mode = LayerMode::Pin;
    if (effect) {
        EffectId id;
        if (fx::EffectIdFromString(effect->AsString(), id)) d.pinned = id;
    }
    if (loop) d.loopTerminal = loop->AsBool(true);
    return d;
}

void SpiralControlBinder::ApplyLayerDirective(LayerKind layer) {
    const fx::LayerDirective wanted = DirectiveFromModel(layer);
    const fx::LayerDirective& current = layer == LayerKind::Foreground ? engine_.Config().foregroundDirective
                                                                          : engine_.Config().backgroundDirective;
    // Only the fields that matter for the mode count as a change, so e.g. tweaking the
    // (hidden) effect dropdown while in Auto doesn't restart the layer.
    bool same = wanted.mode == current.mode;
    if (same && wanted.mode == LayerMode::Pin) {
        same = wanted.pinned == current.pinned;
        const bool terminal = fx::EffectKindOn(layer, wanted.pinned) == EffectKind::Terminal;
        if (same && terminal) same = wanted.loopTerminal == current.loopTerminal;
    }
    if (same) {
        // Keep the stored loop flag current without interrupting anything.
        (layer == LayerKind::Foreground ? engine_.MutableConfig().foregroundDirective
                                        : engine_.MutableConfig().backgroundDirective) = wanted;
        return;
    }
    engine_.SetDirective(layer, wanted);
}

void SpiralControlBinder::OnSet(const std::vector<ServerCore::Change>& changes) {
    bool layerDirty[2] = {false, false}; // [0]=background, [1]=foreground
    auto idx = [](LayerKind l) { return l == LayerKind::Foreground ? 1 : 0; };

    for (const auto& ch : changes) {
        const std::string& id = ch.id;

        if (id == "scrapi.paused") {
            clock_.SetPaused(ch.value.AsBool());
            continue;
        }
        if (id == "scrapi.timeScale") {
            clock_.SetTimeScale(static_cast<float>(ch.value.AsDouble(1.0)));
            continue;
        }

        if (id.rfind("fx.", 0) == 0) {
            // fx.<EffectId>.<property>
            const size_t dot = id.rfind('.');
            EffectId effect;
            LayerKind layer;
            if (dot <= 3 || !fx::EffectIdFromString(id.substr(3, dot - 3), effect) || !LayerOfEffect(effect, layer)) continue;
            auto& layerConfig = layer == LayerKind::Foreground ? engine_.MutableConfig().foreground
                                                                : engine_.MutableConfig().background;
            fx::EffectParams& params = layerConfig.perEffect[effect];
            const std::string prop = id.substr(dot + 1);
            const float v = static_cast<float>(ch.value.AsDouble());
            if (prop == "intensity") params.intensity = v;
            else if (prop == "minSeconds") params.minSeconds = v;
            else if (prop == "maxSeconds") params.maxSeconds = v;
            continue;
        }

        LayerKind layer;
        if (id.rfind("bg.", 0) == 0) layer = LayerKind::Background;
        else if (id.rfind("fg.", 0) == 0) layer = LayerKind::Foreground;
        else continue;
        auto& layerConfig = layer == LayerKind::Foreground ? engine_.MutableConfig().foreground
                                                            : engine_.MutableConfig().background;
        const std::string prop = id.substr(3);
        if (prop == "mode" || prop == "effect" || prop == "loopTerminal") {
            layerDirty[idx(layer)] = true;
        } else if (prop == "minSeconds" || prop == "maxSeconds") {
            const bool isMin = prop == "minSeconds";
            (isMin ? layerConfig.defaultMinSeconds : layerConfig.defaultMaxSeconds) = static_cast<float>(ch.value.AsDouble());
            // Keep min <= max by dragging the *other* bound along, and tell the viewer.
            if (layerConfig.defaultMinSeconds > layerConfig.defaultMaxSeconds) {
                const std::string otherId = std::string(Prefix(layer)) + (isMin ? ".maxSeconds" : ".minSeconds");
                float& other = isMin ? layerConfig.defaultMaxSeconds : layerConfig.defaultMinSeconds;
                other = isMin ? layerConfig.defaultMinSeconds : layerConfig.defaultMaxSeconds;
                if (server_) server_->SetValue(otherId, JsonValue::FromFloat(other));
            }
        } else if (prop == "pool") {
            std::vector<EffectId> chosen;
            for (const auto& item : ch.value.items()) {
                EffectId e;
                if (fx::EffectIdFromString(item.AsString(), e)) chosen.push_back(e);
            }
            for (EffectId e : LayerEffects(layer)) {
                layerConfig.perEffect[e].enabled = std::find(chosen.begin(), chosen.end(), e) != chosen.end();
            }
        }
    }

    if (layerDirty[0]) ApplyLayerDirective(LayerKind::Background);
    if (layerDirty[1]) ApplyLayerDirective(LayerKind::Foreground);
}

bool SpiralControlBinder::OnInvoke(const std::string& id, const JsonValue&, std::string* error) {
    if (id == "scrapi.step") {
        clock_.RequestStep();
        return true;
    }
    if (error) *error = "unsupported action '" + id + "'";
    return false;
}

void SpiralControlBinder::PublishStatus() {
    if (!server_) return;
    for (LayerKind layer : {LayerKind::Background, LayerKind::Foreground}) {
        const auto status = engine_.Status(layer);
        const std::string p = Prefix(layer);
        server_->SetValue(p + ".currentEffect",
                          JsonValue::String(status.hasEffect ? fx::EffectIdToString(status.effect) : "(none)"));
        server_->SetValue(p + ".state", JsonValue::String(fx::FxStateToString(status.state)));
    }
}

} // namespace core
