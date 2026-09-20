#pragma once
// The SCRAPI manifest: a saver's self-description as a tree of controls
// (docs/SCRAPI_SPEC.md §5). Pure data + JSON (de)serialization + structural
// validation; no I/O, no platform dependency.

#include <string>
#include <vector>

#include "Json.h"

namespace scrapi {

enum class ControlType { Group, Enum, Flags, Bool, Int, Float, String, Color, Path, Button, Readout, Unknown };

const char* ControlTypeToString(ControlType type); // "group", "enum", ... ("unknown" for Unknown)
ControlType ControlTypeFromString(const std::string& name);

// True if a control of this type carries a value (everything except group/button).
bool HoldsValue(ControlType type);

struct EnumOption {
    std::string value;
    std::string label;
    std::string description;
};

// visibleWhen / enabledWhen expression (SPEC §5.3).
struct Condition {
    enum class Kind { None, Eq, In, All, Any, Not };
    Kind kind = Kind::None;
    std::string id;                   // Eq / In: the control whose value is tested
    JsonValue value;                  // Eq
    std::vector<JsonValue> values;    // In
    std::vector<Condition> children;  // All / Any / Not
    bool IsSet() const { return kind != Kind::None; }
};

struct ControlNode {
    std::string id;
    ControlType type = ControlType::Unknown;
    std::string typeName; // the raw "type" string, preserved for Unknown types
    std::string label;
    std::string description;
    bool hasDefault = false;
    JsonValue defaultValue;
    bool readOnly = false;      // access == "ro" (always true for readouts)
    bool applyRestart = false;  // apply == "restart"
    Condition visibleWhen;
    Condition enabledWhen;

    // enum / flags
    std::vector<EnumOption> options;
    // group: section|tab|collapsible; enum: dropdown|radio|list; int/float: slider|knob|spinner
    std::string presentation;
    // int / float
    bool hasMin = false, hasMax = false, hasStep = false;
    double min = 0.0, max = 0.0, step = 0.0;
    std::string unit;
    std::string scale; // "linear" (default) | "log"
    // string
    bool multiline = false;
    int maxLength = 0; // 0 = unlimited
    std::string pattern;
    // color
    bool alpha = false;
    std::vector<std::string> palette;
    // path
    std::string pathKind; // "file" | "folder"
    std::vector<std::pair<std::string, std::string>> filters; // {label, pattern}
    // button
    std::string confirm;
    // readout
    std::string format; // text | number | gauge | bar

    std::vector<ControlNode> children; // group
};

struct SaverIdentity {
    std::string id;
    std::string name;
    std::string version;
};

struct Manifest {
    std::string scrapiVersion = "1.0";
    int rev = 1;
    SaverIdentity saver;
    std::vector<std::string> capabilities;
    std::vector<ControlNode> controls;

    bool HasCapability(const std::string& name) const;
};

JsonValue ManifestToJson(const Manifest& manifest);
// Lenient about unknown properties/types (forward compatibility); strict about the
// structural essentials (every node needs a string id and type). Returns false and
// sets *error otherwise.
bool ManifestFromJson(const JsonValue& json, Manifest& out, std::string* error = nullptr);

// Structural checks a saver should pass before serving its manifest: legal unique
// ids, non-empty enum option lists with unique values, consistent min/max, defaults
// that satisfy their own control's constraints, and condition references that resolve.
bool ValidateManifest(const Manifest& manifest, std::string* error = nullptr);

// Depth-first visit of every node (groups included) in manifest order.
template <typename Fn>
void ForEachControl(const std::vector<ControlNode>& nodes, Fn&& fn) {
    for (const ControlNode& node : nodes) {
        fn(node);
        ForEachControl(node.children, fn);
    }
}

const ControlNode* FindControl(const Manifest& manifest, const std::string& id);

// [A-Za-z0-9_.-]+
bool IsValidControlId(const std::string& id);

} // namespace scrapi
