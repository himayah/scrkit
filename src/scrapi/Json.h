#pragma once
// Minimal JSON value + parser + serializer for the SCRAPI control channel
// (docs/SCRAPI_SPEC.md). Deliberately tiny and dependency-free, like the rest
// of this project: objects keep insertion order (so manifests read the way they
// were written), integers stay integers, and parsing is hardened against
// hostile input (depth limit, no trailing garbage, strict escapes).

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace scrapi {

class JsonValue {
public:
    enum class Type { Null, Bool, Int, Double, String, Array, Object };
    using Member = std::pair<std::string, JsonValue>;

    JsonValue() = default;
    static JsonValue Null() { return JsonValue(); }
    static JsonValue Bool(bool v);
    static JsonValue Int(int64_t v);
    static JsonValue Double(double v);
    // A float given to JSON should print as the decimal the author meant
    // (0.7f -> 0.7, not 0.699999988...), so it is rounded to 7 significant digits.
    static JsonValue FromFloat(float v);
    static JsonValue String(std::string v);
    static JsonValue Array();
    static JsonValue Object();

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsInt() const { return type_ == Type::Int; }
    bool IsNumber() const { return type_ == Type::Int || type_ == Type::Double; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool(bool fallback = false) const { return type_ == Type::Bool ? bool_ : fallback; }
    // Int, or a Double holding an exactly-integral value.
    bool TryGetInt(int64_t& out) const;
    int64_t AsInt(int64_t fallback = 0) const;
    double AsDouble(double fallback = 0.0) const;
    const std::string& AsString() const { return string_; } // empty unless IsString()
    std::string AsString(const std::string& fallback) const { return IsString() ? string_ : fallback; }

    const std::vector<JsonValue>& items() const { return array_; }
    const std::vector<Member>& members() const { return object_; }

    // Object access. Find returns nullptr if absent (or if this isn't an object).
    const JsonValue* Find(const std::string& key) const;
    // Set replaces an existing key (keeping its position) or appends. No-op on non-objects.
    JsonValue& Set(const std::string& key, JsonValue value);
    // Array append. No-op on non-arrays.
    JsonValue& Push(JsonValue value);

    bool operator==(const JsonValue& other) const;
    bool operator!=(const JsonValue& other) const { return !(*this == other); }

private:
    Type type_ = Type::Null;
    bool bool_ = false;
    int64_t int_ = 0;
    double double_ = 0.0;
    std::string string_;
    std::vector<JsonValue> array_;
    std::vector<Member> object_;
};

// Parses `text` (whole input must be exactly one JSON value plus whitespace).
// On failure returns false, leaves `out` untouched and, if non-null, sets *error.
bool ParseJson(const std::string& text, JsonValue& out, std::string* error = nullptr);

// Compact by default (single line, safe for JSON Lines); `pretty` indents with 2 spaces.
std::string SerializeJson(const JsonValue& value, bool pretty = false);

} // namespace scrapi
