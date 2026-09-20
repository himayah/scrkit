#include "ControlModel.h"

#include <algorithm>
#include <cctype>

namespace scrapi {

namespace {

ValueResult Fail(const char* code, std::string message) {
    ValueResult r;
    r.errorCode = code;
    r.errorMessage = std::move(message);
    return r;
}
ValueResult Ok(JsonValue v) {
    ValueResult r;
    r.ok = true;
    r.value = std::move(v);
    return r;
}

const EnumOption* FindOption(const ControlNode& node, const std::string& value) {
    for (const auto& o : node.options) {
        if (o.value == value) return &o;
    }
    return nullptr;
}

bool IsHex(char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; }

} // namespace

ValueResult CoerceValue(const ControlNode& node, const JsonValue& in) {
    switch (node.type) {
        case ControlType::Bool:
            if (!in.IsBool()) return Fail(err::kTypeMismatch, "'" + node.id + "' expects a boolean");
            return Ok(in);

        case ControlType::Int: {
            int64_t v;
            if (!in.TryGetInt(v)) return Fail(err::kTypeMismatch, "'" + node.id + "' expects an integer");
            if (node.hasMin && static_cast<double>(v) < node.min) v = static_cast<int64_t>(node.min);
            if (node.hasMax && static_cast<double>(v) > node.max) v = static_cast<int64_t>(node.max);
            return Ok(JsonValue::Int(v));
        }

        case ControlType::Float: {
            if (!in.IsNumber()) return Fail(err::kTypeMismatch, "'" + node.id + "' expects a number");
            double v = in.AsDouble();
            if (node.hasMin && v < node.min) v = node.min;
            if (node.hasMax && v > node.max) v = node.max;
            return Ok(JsonValue::Double(v));
        }

        case ControlType::Enum: {
            if (!in.IsString()) return Fail(err::kTypeMismatch, "'" + node.id + "' expects a string option value");
            if (!FindOption(node, in.AsString())) {
                return Fail(err::kBadRequest, "'" + in.AsString() + "' is not an option of '" + node.id + "'");
            }
            return Ok(in);
        }

        case ControlType::Flags: {
            if (!in.IsArray()) return Fail(err::kTypeMismatch, "'" + node.id + "' expects an array of option values");
            std::vector<bool> chosen(node.options.size(), false);
            for (const auto& item : in.items()) {
                if (!item.IsString()) return Fail(err::kTypeMismatch, "'" + node.id + "' expects strings in its array");
                bool found = false;
                for (size_t i = 0; i < node.options.size(); ++i) {
                    if (node.options[i].value == item.AsString()) {
                        chosen[i] = true;
                        found = true;
                        break;
                    }
                }
                if (!found) return Fail(err::kBadRequest, "'" + item.AsString() + "' is not an option of '" + node.id + "'");
            }
            JsonValue out = JsonValue::Array();
            for (size_t i = 0; i < node.options.size(); ++i) {
                if (chosen[i]) out.Push(JsonValue::String(node.options[i].value));
            }
            return Ok(std::move(out));
        }

        case ControlType::String:
        case ControlType::Path: {
            if (!in.IsString()) return Fail(err::kTypeMismatch, "'" + node.id + "' expects a string");
            if (node.maxLength > 0 && in.AsString().size() > static_cast<size_t>(node.maxLength)) {
                return Fail(err::kBadRequest, "'" + node.id + "' exceeds maxLength");
            }
            return Ok(in);
        }

        case ControlType::Color: {
            if (!in.IsString()) return Fail(err::kTypeMismatch, "'" + node.id + "' expects a color string");
            std::string c = in.AsString();
            const size_t expected = node.alpha ? 9 : 7;
            bool ok = c.size() == expected && c[0] == '#';
            for (size_t i = 1; ok && i < c.size(); ++i) ok = IsHex(c[i]);
            if (!ok) {
                return Fail(err::kBadRequest, "'" + node.id + "' expects " + (node.alpha ? "#RRGGBBAA" : "#RRGGBB"));
            }
            for (auto& ch : c) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            return Ok(JsonValue::String(std::move(c)));
        }

        case ControlType::Readout:
            return Ok(in); // free-form: whatever the saver publishes

        case ControlType::Group:
        case ControlType::Button:
            return Fail(err::kUnsupported, "'" + node.id + "' holds no value");

        case ControlType::Unknown:
            return Ok(in); // forward compatibility: keep the raw value
    }
    return Fail(err::kInternal, "unhandled control type");
}

JsonValue InitialValue(const ControlNode& node) {
    if (node.hasDefault) return node.defaultValue;
    switch (node.type) {
        case ControlType::Bool: return JsonValue::Bool(false);
        case ControlType::Int: return JsonValue::Int(node.hasMin ? static_cast<int64_t>(node.min) : 0);
        case ControlType::Float: return JsonValue::Double(node.hasMin ? node.min : 0.0);
        case ControlType::Enum:
            return node.options.empty() ? JsonValue::Null() : JsonValue::String(node.options.front().value);
        case ControlType::Flags: return JsonValue::Array();
        case ControlType::String:
        case ControlType::Path: return JsonValue::String("");
        case ControlType::Color: return JsonValue::String(node.alpha ? "#00000000" : "#000000");
        default: return JsonValue::Null();
    }
}

ControlModel::ControlModel(Manifest manifest) : manifest_(std::move(manifest)) {
    // Walk the tree once, recording each node, its parent group and initial value.
    struct Walker {
        ControlModel& self;
        void Visit(const std::vector<ControlNode>& nodes, const std::string& parent) {
            for (const ControlNode& n : nodes) {
                self.index_[n.id] = &n;
                self.parent_[n.id] = parent;
                if (HoldsValue(n.type)) {
                    self.values_[n.id] = InitialValue(n);
                    self.valueIds_.push_back(n.id);
                }
                Visit(n.children, n.id);
            }
        }
    } walker{*this};
    walker.Visit(manifest_.controls, "");
}

const ControlNode* ControlModel::Find(const std::string& id) const {
    auto it = index_.find(id);
    return it == index_.end() ? nullptr : it->second;
}

const JsonValue* ControlModel::Get(const std::string& id) const {
    auto it = values_.find(id);
    return it == values_.end() ? nullptr : &it->second;
}

ValueResult ControlModel::Write(const std::string& id, const JsonValue& value, bool allowReadOnly) {
    const ControlNode* node = Find(id);
    if (!node) return Fail(err::kUnknownId, "no control '" + id + "'");
    if (!allowReadOnly && (node->readOnly || node->type == ControlType::Readout)) return Fail(err::kReadOnly, "'" + id + "' is read-only");
    ValueResult r = CoerceValue(*node, value);
    if (r.ok) values_[id] = r.value;
    return r;
}

ValueResult ControlModel::Set(const std::string& id, const JsonValue& value) { return Write(id, value, false); }
ValueResult ControlModel::SetFromSaver(const std::string& id, const JsonValue& value) {
    return Write(id, value, true);
}

void ControlModel::ResetToDefault(const std::string& id) {
    const ControlNode* node = Find(id);
    if (node && HoldsValue(node->type)) values_[id] = InitialValue(*node);
}

bool ControlModel::Evaluate(const Condition& c) const {
    switch (c.kind) {
        case Condition::Kind::None: return true;
        case Condition::Kind::Eq: {
            const JsonValue* v = Get(c.id);
            return v && *v == c.value;
        }
        case Condition::Kind::In: {
            const JsonValue* v = Get(c.id);
            if (!v) return false;
            return std::any_of(c.values.begin(), c.values.end(), [&](const JsonValue& x) { return *v == x; });
        }
        case Condition::Kind::All:
            return std::all_of(c.children.begin(), c.children.end(), [&](const Condition& x) { return Evaluate(x); });
        case Condition::Kind::Any:
            return std::any_of(c.children.begin(), c.children.end(), [&](const Condition& x) { return Evaluate(x); });
        case Condition::Kind::Not: return c.children.empty() ? true : !Evaluate(c.children[0]);
    }
    return true;
}

bool ControlModel::ChainHolds(const std::string& id, bool visibility) const {
    std::string cur = id;
    for (int guard = 0; !cur.empty() && guard < 64; ++guard) {
        const ControlNode* n = Find(cur);
        if (!n) return false;
        if (!Evaluate(visibility ? n->visibleWhen : n->enabledWhen)) return false;
        auto p = parent_.find(cur);
        cur = p == parent_.end() ? std::string() : p->second;
    }
    return true;
}

bool ControlModel::IsVisible(const std::string& id) const { return ChainHolds(id, true); }
bool ControlModel::IsEnabled(const std::string& id) const { return ChainHolds(id, false); }

} // namespace scrapi
