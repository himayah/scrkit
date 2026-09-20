#include "Manifest.h"

#include <set>

#include "ControlModel.h"

namespace scrapi {

namespace {
struct TypeName {
    ControlType type;
    const char* name;
};
constexpr TypeName kTypeNames[] = {
    {ControlType::Group, "group"}, {ControlType::Enum, "enum"},     {ControlType::Flags, "flags"},
    {ControlType::Bool, "bool"},   {ControlType::Int, "int"},       {ControlType::Float, "float"},
    {ControlType::String, "string"}, {ControlType::Color, "color"}, {ControlType::Path, "path"},
    {ControlType::Button, "button"}, {ControlType::Readout, "readout"},
};
} // namespace

const char* ControlTypeToString(ControlType type) {
    for (const auto& t : kTypeNames) {
        if (t.type == type) return t.name;
    }
    return "unknown";
}

ControlType ControlTypeFromString(const std::string& name) {
    for (const auto& t : kTypeNames) {
        if (name == t.name) return t.type;
    }
    return ControlType::Unknown;
}

bool HoldsValue(ControlType type) { return type != ControlType::Group && type != ControlType::Button; }

bool Manifest::HasCapability(const std::string& name) const {
    for (const auto& c : capabilities) {
        if (c == name) return true;
    }
    return false;
}

bool IsValidControlId(const std::string& id) {
    if (id.empty()) return false;
    for (char c : id) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
                        c == '.' || c == '-';
        if (!ok) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Condition <-> JSON

namespace {

JsonValue ConditionToJson(const Condition& c) {
    JsonValue o = JsonValue::Object();
    switch (c.kind) {
        case Condition::Kind::None: break;
        case Condition::Kind::Eq:
            o.Set("id", JsonValue::String(c.id));
            o.Set("eq", c.value);
            break;
        case Condition::Kind::In: {
            o.Set("id", JsonValue::String(c.id));
            JsonValue arr = JsonValue::Array();
            for (const auto& v : c.values) arr.Push(v);
            o.Set("in", std::move(arr));
            break;
        }
        case Condition::Kind::All:
        case Condition::Kind::Any: {
            JsonValue arr = JsonValue::Array();
            for (const auto& child : c.children) arr.Push(ConditionToJson(child));
            o.Set(c.kind == Condition::Kind::All ? "all" : "any", std::move(arr));
            break;
        }
        case Condition::Kind::Not:
            if (!c.children.empty()) o.Set("not", ConditionToJson(c.children[0]));
            break;
    }
    return o;
}

bool ConditionFromJson(const JsonValue& j, Condition& out, std::string* error, int depth = 0) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (depth > 16) return fail("condition nested too deeply");
    if (!j.IsObject()) return fail("condition must be an object");
    Condition c;
    if (const JsonValue* all = j.Find("all")) {
        c.kind = Condition::Kind::All;
        if (!all->IsArray()) return fail("'all' must be an array");
        for (const auto& item : all->items()) {
            Condition child;
            if (!ConditionFromJson(item, child, error, depth + 1)) return false;
            c.children.push_back(std::move(child));
        }
    } else if (const JsonValue* any = j.Find("any")) {
        c.kind = Condition::Kind::Any;
        if (!any->IsArray()) return fail("'any' must be an array");
        for (const auto& item : any->items()) {
            Condition child;
            if (!ConditionFromJson(item, child, error, depth + 1)) return false;
            c.children.push_back(std::move(child));
        }
    } else if (const JsonValue* n = j.Find("not")) {
        c.kind = Condition::Kind::Not;
        Condition child;
        if (!ConditionFromJson(*n, child, error, depth + 1)) return false;
        c.children.push_back(std::move(child));
    } else if (const JsonValue* id = j.Find("id")) {
        if (!id->IsString()) return fail("condition 'id' must be a string");
        c.id = id->AsString();
        if (const JsonValue* eq = j.Find("eq")) {
            c.kind = Condition::Kind::Eq;
            c.value = *eq;
        } else if (const JsonValue* in = j.Find("in")) {
            c.kind = Condition::Kind::In;
            if (!in->IsArray()) return fail("'in' must be an array");
            c.values = in->items();
        } else {
            return fail("condition needs 'eq' or 'in'");
        }
    } else {
        return fail("unrecognized condition");
    }
    out = std::move(c);
    return true;
}

// ---------------------------------------------------------------------------
// Node <-> JSON

JsonValue NodeToJson(const ControlNode& n) {
    JsonValue o = JsonValue::Object();
    o.Set("id", JsonValue::String(n.id));
    o.Set("type", JsonValue::String(n.type == ControlType::Unknown ? n.typeName : ControlTypeToString(n.type)));
    if (!n.label.empty()) o.Set("label", JsonValue::String(n.label));
    if (!n.description.empty()) o.Set("description", JsonValue::String(n.description));
    if (n.hasDefault) o.Set("default", n.defaultValue);
    if (n.readOnly && n.type != ControlType::Readout) o.Set("access", JsonValue::String("ro"));
    if (n.applyRestart) o.Set("apply", JsonValue::String("restart"));
    if (n.visibleWhen.IsSet()) o.Set("visibleWhen", ConditionToJson(n.visibleWhen));
    if (n.enabledWhen.IsSet()) o.Set("enabledWhen", ConditionToJson(n.enabledWhen));
    if (!n.options.empty()) {
        JsonValue arr = JsonValue::Array();
        for (const auto& opt : n.options) {
            JsonValue oo = JsonValue::Object();
            oo.Set("value", JsonValue::String(opt.value));
            oo.Set("label", JsonValue::String(opt.label.empty() ? opt.value : opt.label));
            if (!opt.description.empty()) oo.Set("description", JsonValue::String(opt.description));
            arr.Push(std::move(oo));
        }
        o.Set("options", std::move(arr));
    }
    if (!n.presentation.empty()) o.Set("presentation", JsonValue::String(n.presentation));
    if (n.hasMin) o.Set("min", JsonValue::Double(n.min));
    if (n.hasMax) o.Set("max", JsonValue::Double(n.max));
    if (n.hasStep) o.Set("step", JsonValue::Double(n.step));
    if (!n.unit.empty()) o.Set("unit", JsonValue::String(n.unit));
    if (!n.scale.empty()) o.Set("scale", JsonValue::String(n.scale));
    if (n.multiline) o.Set("multiline", JsonValue::Bool(true));
    if (n.maxLength > 0) o.Set("maxLength", JsonValue::Int(n.maxLength));
    if (!n.pattern.empty()) o.Set("pattern", JsonValue::String(n.pattern));
    if (n.alpha) o.Set("alpha", JsonValue::Bool(true));
    if (!n.palette.empty()) {
        JsonValue arr = JsonValue::Array();
        for (const auto& c : n.palette) arr.Push(JsonValue::String(c));
        o.Set("palette", std::move(arr));
    }
    if (!n.pathKind.empty()) o.Set("kind", JsonValue::String(n.pathKind));
    if (!n.filters.empty()) {
        JsonValue arr = JsonValue::Array();
        for (const auto& f : n.filters) {
            JsonValue fo = JsonValue::Object();
            fo.Set("label", JsonValue::String(f.first));
            fo.Set("pattern", JsonValue::String(f.second));
            arr.Push(std::move(fo));
        }
        o.Set("filters", std::move(arr));
    }
    if (!n.confirm.empty()) o.Set("confirm", JsonValue::String(n.confirm));
    if (!n.format.empty()) o.Set("format", JsonValue::String(n.format));
    if (!n.children.empty() || n.type == ControlType::Group) {
        JsonValue arr = JsonValue::Array();
        for (const auto& c : n.children) arr.Push(NodeToJson(c));
        o.Set("children", std::move(arr));
    }
    return o;
}

std::string StringMember(const JsonValue& o, const char* key) {
    const JsonValue* v = o.Find(key);
    return v && v->IsString() ? v->AsString() : std::string();
}
bool BoolMember(const JsonValue& o, const char* key) {
    const JsonValue* v = o.Find(key);
    return v && v->IsBool() && v->AsBool();
}

bool NodeFromJson(const JsonValue& j, ControlNode& out, std::string* error, int depth) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (depth > 16) return fail("controls nested too deeply");
    if (!j.IsObject()) return fail("control must be an object");
    const JsonValue* id = j.Find("id");
    const JsonValue* type = j.Find("type");
    if (!id || !id->IsString()) return fail("control is missing a string 'id'");
    if (!type || !type->IsString()) return fail("control '" + id->AsString() + "' is missing a string 'type'");

    ControlNode n;
    n.id = id->AsString();
    n.typeName = type->AsString();
    n.type = ControlTypeFromString(n.typeName);
    n.label = StringMember(j, "label");
    n.description = StringMember(j, "description");
    if (const JsonValue* d = j.Find("default")) {
        n.hasDefault = true;
        n.defaultValue = *d;
    }
    n.readOnly = StringMember(j, "access") == "ro" || n.type == ControlType::Readout;
    n.applyRestart = StringMember(j, "apply") == "restart";
    if (const JsonValue* v = j.Find("visibleWhen")) {
        if (!ConditionFromJson(*v, n.visibleWhen, error)) return false;
    }
    if (const JsonValue* v = j.Find("enabledWhen")) {
        if (!ConditionFromJson(*v, n.enabledWhen, error)) return false;
    }
    if (const JsonValue* opts = j.Find("options")) {
        if (!opts->IsArray()) return fail("'options' must be an array in '" + n.id + "'");
        for (const auto& item : opts->items()) {
            if (!item.IsObject() || !item.Find("value") || !item.Find("value")->IsString()) {
                return fail("bad option in '" + n.id + "'");
            }
            EnumOption opt;
            opt.value = item.Find("value")->AsString();
            opt.label = StringMember(item, "label");
            if (opt.label.empty()) opt.label = opt.value;
            opt.description = StringMember(item, "description");
            n.options.push_back(std::move(opt));
        }
    }
    n.presentation = StringMember(j, "presentation");
    if (const JsonValue* v = j.Find("min"); v && v->IsNumber()) { n.hasMin = true; n.min = v->AsDouble(); }
    if (const JsonValue* v = j.Find("max"); v && v->IsNumber()) { n.hasMax = true; n.max = v->AsDouble(); }
    if (const JsonValue* v = j.Find("step"); v && v->IsNumber()) { n.hasStep = true; n.step = v->AsDouble(); }
    n.unit = StringMember(j, "unit");
    n.scale = StringMember(j, "scale");
    n.multiline = BoolMember(j, "multiline");
    if (const JsonValue* v = j.Find("maxLength"); v && v->IsNumber()) n.maxLength = static_cast<int>(v->AsInt());
    n.pattern = StringMember(j, "pattern");
    n.alpha = BoolMember(j, "alpha");
    if (const JsonValue* pal = j.Find("palette"); pal && pal->IsArray()) {
        for (const auto& c : pal->items()) {
            if (c.IsString()) n.palette.push_back(c.AsString());
        }
    }
    n.pathKind = StringMember(j, "kind");
    if (const JsonValue* fl = j.Find("filters"); fl && fl->IsArray()) {
        for (const auto& f : fl->items()) {
            if (f.IsObject()) n.filters.emplace_back(StringMember(f, "label"), StringMember(f, "pattern"));
        }
    }
    n.confirm = StringMember(j, "confirm");
    n.format = StringMember(j, "format");
    if (const JsonValue* ch = j.Find("children")) {
        if (!ch->IsArray()) return fail("'children' must be an array in '" + n.id + "'");
        for (const auto& item : ch->items()) {
            ControlNode child;
            if (!NodeFromJson(item, child, error, depth + 1)) return false;
            n.children.push_back(std::move(child));
        }
    }
    out = std::move(n);
    return true;
}

} // namespace

JsonValue ManifestToJson(const Manifest& m) {
    JsonValue o = JsonValue::Object();
    o.Set("scrapi", JsonValue::String(m.scrapiVersion));
    o.Set("rev", JsonValue::Int(m.rev));
    JsonValue saver = JsonValue::Object();
    saver.Set("id", JsonValue::String(m.saver.id));
    saver.Set("name", JsonValue::String(m.saver.name));
    saver.Set("version", JsonValue::String(m.saver.version));
    o.Set("saver", std::move(saver));
    JsonValue caps = JsonValue::Array();
    for (const auto& c : m.capabilities) caps.Push(JsonValue::String(c));
    o.Set("capabilities", std::move(caps));
    JsonValue controls = JsonValue::Array();
    for (const auto& c : m.controls) controls.Push(NodeToJson(c));
    o.Set("controls", std::move(controls));
    return o;
}

bool ManifestFromJson(const JsonValue& j, Manifest& out, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (!j.IsObject()) return fail("manifest must be an object");
    Manifest m;
    const JsonValue* version = j.Find("scrapi");
    if (!version || !version->IsString()) return fail("manifest is missing 'scrapi' version");
    m.scrapiVersion = version->AsString();
    if (const JsonValue* rev = j.Find("rev"); rev && rev->IsNumber()) m.rev = static_cast<int>(rev->AsInt(1));
    if (const JsonValue* s = j.Find("saver"); s && s->IsObject()) {
        m.saver.id = StringMember(*s, "id");
        m.saver.name = StringMember(*s, "name");
        m.saver.version = StringMember(*s, "version");
    }
    if (const JsonValue* caps = j.Find("capabilities"); caps && caps->IsArray()) {
        for (const auto& c : caps->items()) {
            if (c.IsString()) m.capabilities.push_back(c.AsString());
        }
    }
    const JsonValue* controls = j.Find("controls");
    if (!controls || !controls->IsArray()) return fail("manifest is missing 'controls' array");
    for (const auto& item : controls->items()) {
        ControlNode node;
        if (!NodeFromJson(item, node, error, 0)) return false;
        m.controls.push_back(std::move(node));
    }
    out = std::move(m);
    return true;
}

const ControlNode* FindControl(const Manifest& manifest, const std::string& id) {
    const ControlNode* found = nullptr;
    ForEachControl(manifest.controls, [&](const ControlNode& n) {
        if (!found && n.id == id) found = &n;
    });
    return found;
}

// ---------------------------------------------------------------------------
// Validation

namespace {

bool CheckConditionRefs(const Condition& c, const std::set<std::string>& ids, const std::string& owner,
                        std::string* error) {
    if (c.kind == Condition::Kind::Eq || c.kind == Condition::Kind::In) {
        if (!ids.count(c.id)) {
            if (error) *error = "condition of '" + owner + "' references unknown control '" + c.id + "'";
            return false;
        }
    }
    for (const auto& child : c.children) {
        if (!CheckConditionRefs(child, ids, owner, error)) return false;
    }
    return true;
}

} // namespace

bool ValidateManifest(const Manifest& manifest, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };

    std::set<std::string> ids;
    std::string problem;
    ForEachControl(manifest.controls, [&](const ControlNode& n) {
        if (!problem.empty()) return;
        if (!IsValidControlId(n.id)) {
            problem = "illegal control id '" + n.id + "'";
            return;
        }
        if (!ids.insert(n.id).second) {
            problem = "duplicate control id '" + n.id + "'";
            return;
        }
        if ((n.type == ControlType::Enum || n.type == ControlType::Flags)) {
            if (n.options.empty()) {
                problem = "'" + n.id + "' has no options";
                return;
            }
            std::set<std::string> seen;
            for (const auto& o : n.options) {
                if (!seen.insert(o.value).second) {
                    problem = "'" + n.id + "' has duplicate option '" + o.value + "'";
                    return;
                }
            }
        }
        if (n.hasMin && n.hasMax && n.min > n.max) problem = "'" + n.id + "' has min > max";
        if (n.type == ControlType::Group && n.hasDefault) problem = "group '" + n.id + "' cannot have a default";
    });
    if (!problem.empty()) return fail(problem);

    // Defaults must satisfy their own control's constraints *without* being altered
    // by clamping (a default of 5 on a 0..1 slider is a manifest bug, not a clamp).
    ForEachControl(manifest.controls, [&](const ControlNode& n) {
        if (!problem.empty() || !n.hasDefault) return;
        const ValueResult r = CoerceValue(n, n.defaultValue);
        if (!r.ok) problem = "default of '" + n.id + "' is invalid: " + r.errorMessage;
        else if (!(r.value == n.defaultValue)) problem = "default of '" + n.id + "' is out of range or not canonical";
    });
    if (!problem.empty()) return fail(problem);

    ForEachControl(manifest.controls, [&](const ControlNode& n) {
        if (!problem.empty()) return;
        if (!CheckConditionRefs(n.visibleWhen, ids, n.id, &problem)) return;
        CheckConditionRefs(n.enabledWhen, ids, n.id, &problem);
    });
    if (!problem.empty()) return fail(problem);
    return true;
}

} // namespace scrapi
