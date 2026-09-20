#pragma once
// A small saver-agnostic manifest exercising every control type, shared by the
// SCRAPI tests. Deliberately unrelated to any specific saver (ScrKit's spiral-suction one included).

#include "../src/scrapi/Manifest.h"

namespace scrapi_fixture {

inline scrapi::EnumOption Opt(const char* value, const char* label = "") {
    scrapi::EnumOption o;
    o.value = value;
    o.label = label;
    return o;
}

inline scrapi::Manifest MakeManifest() {
    using namespace scrapi;
    Manifest m;
    m.rev = 1;
    m.saver = {"example.demo", "Demo Saver", "0.1.0"};
    m.capabilities = {"scrapi.paused", "viewport.resize"};

    ControlNode mode;
    mode.id = "mode";
    mode.type = ControlType::Enum;
    mode.label = "Mode";
    mode.options = {Opt("auto", "Automatic"), Opt("fixed", "Fixed")};
    mode.presentation = "radio";
    mode.hasDefault = true;
    mode.defaultValue = JsonValue::String("auto");

    ControlNode speed;
    speed.id = "fixed.speed";
    speed.type = ControlType::Float;
    speed.hasMin = speed.hasMax = true;
    speed.min = 0.0;
    speed.max = 10.0;
    speed.hasDefault = true;
    speed.defaultValue = JsonValue::Double(1.0);
    speed.presentation = "knob";
    speed.visibleWhen.kind = Condition::Kind::Eq;
    speed.visibleWhen.id = "mode";
    speed.visibleWhen.value = JsonValue::String("fixed");

    ControlNode count;
    count.id = "fixed.count";
    count.type = ControlType::Int;
    count.hasMin = count.hasMax = true;
    count.min = 1;
    count.max = 100;
    count.hasDefault = true;
    count.defaultValue = JsonValue::Int(10);

    ControlNode fixedGroup;
    fixedGroup.id = "fixed";
    fixedGroup.type = ControlType::Group;
    fixedGroup.label = "Fixed settings";
    fixedGroup.visibleWhen = speed.visibleWhen;
    fixedGroup.children = {speed, count};
    // (speed keeps its own identical condition; the group's condition is what hides the whole section)

    ControlNode enabled;
    enabled.id = "enabled";
    enabled.type = ControlType::Bool;
    enabled.hasDefault = true;
    enabled.defaultValue = JsonValue::Bool(true);

    ControlNode tags;
    tags.id = "tags";
    tags.type = ControlType::Flags;
    tags.options = {Opt("a"), Opt("b"), Opt("c")};

    ControlNode title;
    title.id = "title";
    title.type = ControlType::String;
    title.maxLength = 8;

    ControlNode tint;
    tint.id = "tint";
    tint.type = ControlType::Color;
    tint.hasDefault = true;
    tint.defaultValue = JsonValue::String("#336699");

    ControlNode file;
    file.id = "file";
    file.type = ControlType::Path;
    file.pathKind = "file";
    file.enabledWhen.kind = Condition::Kind::Eq;
    file.enabledWhen.id = "enabled";
    file.enabledWhen.value = JsonValue::Bool(true);

    ControlNode go;
    go.id = "go";
    go.type = ControlType::Button;
    go.label = "Go";

    ControlNode status;
    status.id = "status";
    status.type = ControlType::Readout;
    status.format = "text";

    m.controls = {mode, fixedGroup, enabled, tags, title, tint, file, go, status};
    return m;
}

} // namespace scrapi_fixture
