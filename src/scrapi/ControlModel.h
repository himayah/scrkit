#pragma once
// The live value store behind a manifest: type validation, clamping, defaults and
// visibleWhen/enabledWhen evaluation (docs/SCRAPI_SPEC.md §4.4, §5). Shared by both
// ends of the channel -- the saver-side ServerCore validates incoming `set`s with
// it, and the viewer-side ClientCore keeps its cache (and drives UI visibility)
// with it.

#include <string>
#include <unordered_map>
#include <vector>

#include "ErrorCodes.h"
#include "Json.h"
#include "Manifest.h"

namespace scrapi {

struct ValueResult {
    bool ok = false;
    std::string errorCode;
    std::string errorMessage;
    JsonValue value; // the coerced (validated, clamped) value when ok
};

// Validates `in` against `node`'s type and constraints and returns the value that
// would be stored: numbers are clamped into [min,max] (never an error), enum values
// must be a listed option, flags are de-duplicated into option order, colors are
// upper-cased. Group/button nodes hold no value (`unsupported`).
ValueResult CoerceValue(const ControlNode& node, const JsonValue& in);

// The value a control has before anything sets it: its declared default, else a
// type-appropriate zero (false / min-or-0 / first option / [] / "" / "#000000"),
// else null (readouts, until the saver publishes one).
JsonValue InitialValue(const ControlNode& node);

class ControlModel {
public:
    ControlModel() = default;
    explicit ControlModel(Manifest manifest);
    // Not copyable: the id index points into the owned manifest. Moving is fine
    // (a moved vector keeps its element addresses).
    ControlModel(const ControlModel&) = delete;
    ControlModel& operator=(const ControlModel&) = delete;
    ControlModel(ControlModel&&) = default;
    ControlModel& operator=(ControlModel&&) = default;

    const Manifest& manifest() const { return manifest_; }
    const ControlNode* Find(const std::string& id) const;
    const JsonValue* Get(const std::string& id) const;
    // Ids of every value-holding control, in manifest order.
    const std::vector<std::string>& ValueIds() const { return valueIds_; }

    // Viewer-facing write: rejects read-only controls (`readOnly`) and unknown ids.
    ValueResult Set(const std::string& id, const JsonValue& value);
    // Saver-side write: same validation, but readouts and read-only controls may be
    // written (this is how the saver publishes them).
    ValueResult SetFromSaver(const std::string& id, const JsonValue& value);
    void ResetToDefault(const std::string& id);

    bool Evaluate(const Condition& condition) const;
    // A control is visible/enabled only if its own condition and every ancestor
    // group's condition hold.
    bool IsVisible(const std::string& id) const;
    bool IsEnabled(const std::string& id) const;

private:
    ValueResult Write(const std::string& id, const JsonValue& value, bool allowReadOnly);
    bool ChainHolds(const std::string& id, bool visibility) const;

    Manifest manifest_;
    std::unordered_map<std::string, const ControlNode*> index_;
    std::unordered_map<std::string, std::string> parent_; // id -> parent group id ("" for top level)
    std::unordered_map<std::string, JsonValue> values_;
    std::vector<std::string> valueIds_;
};

} // namespace scrapi
