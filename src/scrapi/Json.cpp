#include "Json.h"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace scrapi {

JsonValue JsonValue::Bool(bool v) {
    JsonValue j;
    j.type_ = Type::Bool;
    j.bool_ = v;
    return j;
}
JsonValue JsonValue::Int(int64_t v) {
    JsonValue j;
    j.type_ = Type::Int;
    j.int_ = v;
    return j;
}
JsonValue JsonValue::Double(double v) {
    JsonValue j;
    j.type_ = Type::Double;
    j.double_ = v;
    return j;
}
JsonValue JsonValue::FromFloat(float v) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.7g", static_cast<double>(v));
    return Double(std::strtod(buf, nullptr));
}
JsonValue JsonValue::String(std::string v) {
    JsonValue j;
    j.type_ = Type::String;
    j.string_ = std::move(v);
    return j;
}
JsonValue JsonValue::Array() {
    JsonValue j;
    j.type_ = Type::Array;
    return j;
}
JsonValue JsonValue::Object() {
    JsonValue j;
    j.type_ = Type::Object;
    return j;
}

bool JsonValue::TryGetInt(int64_t& out) const {
    if (type_ == Type::Int) {
        out = int_;
        return true;
    }
    if (type_ == Type::Double && std::isfinite(double_) && std::floor(double_) == double_ &&
        std::fabs(double_) < 9.0e18) {
        out = static_cast<int64_t>(double_);
        return true;
    }
    return false;
}
int64_t JsonValue::AsInt(int64_t fallback) const {
    int64_t v;
    return TryGetInt(v) ? v : fallback;
}
double JsonValue::AsDouble(double fallback) const {
    if (type_ == Type::Int) return static_cast<double>(int_);
    if (type_ == Type::Double) return double_;
    return fallback;
}

const JsonValue* JsonValue::Find(const std::string& key) const {
    if (type_ != Type::Object) return nullptr;
    for (const auto& m : object_) {
        if (m.first == key) return &m.second;
    }
    return nullptr;
}
JsonValue& JsonValue::Set(const std::string& key, JsonValue value) {
    if (type_ != Type::Object) return *this;
    for (auto& m : object_) {
        if (m.first == key) {
            m.second = std::move(value);
            return *this;
        }
    }
    object_.emplace_back(key, std::move(value));
    return *this;
}
JsonValue& JsonValue::Push(JsonValue value) {
    if (type_ == Type::Array) array_.push_back(std::move(value));
    return *this;
}

bool JsonValue::operator==(const JsonValue& o) const {
    if (IsNumber() && o.IsNumber()) {
        if (type_ == Type::Int && o.type_ == Type::Int) return int_ == o.int_;
        return AsDouble() == o.AsDouble();
    }
    if (type_ != o.type_) return false;
    switch (type_) {
        case Type::Null: return true;
        case Type::Bool: return bool_ == o.bool_;
        case Type::String: return string_ == o.string_;
        case Type::Array: return array_ == o.array_;
        case Type::Object: {
            if (object_.size() != o.object_.size()) return false;
            for (const auto& m : object_) {
                const JsonValue* other = o.Find(m.first);
                if (!other || !(m.second == *other)) return false;
            }
            return true;
        }
        default: return false;
    }
}

namespace {

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(const std::string& text) : s_(text) {}

    bool Parse(JsonValue& out, std::string* error) {
        SkipWs();
        JsonValue v;
        if (!ParseValue(v, 0)) return Fail(error);
        SkipWs();
        if (pos_ != s_.size()) {
            err_ = "trailing characters after JSON value";
            return Fail(error);
        }
        out = std::move(v);
        return true;
    }

private:
    bool Fail(std::string* error) {
        if (error) *error = err_ + " (at offset " + std::to_string(pos_) + ")";
        return false;
    }
    bool Err(const char* msg) {
        err_ = msg;
        return false;
    }
    void SkipWs() {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }
    bool Consume(const char* literal) {
        size_t i = 0;
        while (literal[i]) {
            if (pos_ + i >= s_.size() || s_[pos_ + i] != literal[i]) return false;
            ++i;
        }
        pos_ += i;
        return true;
    }

    bool ParseValue(JsonValue& out, int depth) {
        if (depth > kMaxDepth) return Err("nesting too deep");
        if (pos_ >= s_.size()) return Err("unexpected end of input");
        const char c = s_[pos_];
        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            std::string str;
            if (!ParseString(str)) return false;
            out = JsonValue::String(std::move(str));
            return true;
        }
        if (c == 't') return Consume("true") ? (out = JsonValue::Bool(true), true) : Err("invalid literal");
        if (c == 'f') return Consume("false") ? (out = JsonValue::Bool(false), true) : Err("invalid literal");
        if (c == 'n') return Consume("null") ? (out = JsonValue::Null(), true) : Err("invalid literal");
        if (c == '-' || (c >= '0' && c <= '9')) return ParseNumber(out);
        return Err("unexpected character");
    }

    bool ParseObject(JsonValue& out, int depth) {
        ++pos_; // '{'
        JsonValue obj = JsonValue::Object();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            out = std::move(obj);
            return true;
        }
        while (true) {
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return Err("expected object key string");
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return Err("expected ':' after object key");
            ++pos_;
            SkipWs();
            JsonValue value;
            if (!ParseValue(value, depth + 1)) return false;
            obj.Set(key, std::move(value));
            SkipWs();
            if (pos_ >= s_.size()) return Err("unterminated object");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == '}') {
                ++pos_;
                break;
            }
            return Err("expected ',' or '}' in object");
        }
        out = std::move(obj);
        return true;
    }

    bool ParseArray(JsonValue& out, int depth) {
        ++pos_; // '['
        JsonValue arr = JsonValue::Array();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            out = std::move(arr);
            return true;
        }
        while (true) {
            SkipWs();
            JsonValue value;
            if (!ParseValue(value, depth + 1)) return false;
            arr.Push(std::move(value));
            SkipWs();
            if (pos_ >= s_.size()) return Err("unterminated array");
            if (s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (s_[pos_] == ']') {
                ++pos_;
                break;
            }
            return Err("expected ',' or ']' in array");
        }
        out = std::move(arr);
        return true;
    }

    static void AppendUtf8(std::string& out, uint32_t cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    bool ParseHex4(uint32_t& out) {
        if (pos_ + 4 > s_.size()) return Err("truncated \\u escape");
        uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = s_[pos_ + i];
            v <<= 4;
            if (h >= '0' && h <= '9') v |= static_cast<uint32_t>(h - '0');
            else if (h >= 'a' && h <= 'f') v |= static_cast<uint32_t>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') v |= static_cast<uint32_t>(h - 'A' + 10);
            else return Err("invalid \\u escape");
        }
        pos_ += 4;
        out = v;
        return true;
    }

    bool ParseString(std::string& out) {
        ++pos_; // opening quote
        std::string result;
        while (true) {
            if (pos_ >= s_.size()) return Err("unterminated string");
            const unsigned char c = static_cast<unsigned char>(s_[pos_++]);
            if (c == '"') break;
            if (c < 0x20) return Err("unescaped control character in string");
            if (c != '\\') {
                result += static_cast<char>(c);
                continue;
            }
            if (pos_ >= s_.size()) return Err("unterminated escape");
            const char e = s_[pos_++];
            switch (e) {
                case '"': result += '"'; break;
                case '\\': result += '\\'; break;
                case '/': result += '/'; break;
                case 'b': result += '\b'; break;
                case 'f': result += '\f'; break;
                case 'n': result += '\n'; break;
                case 'r': result += '\r'; break;
                case 't': result += '\t'; break;
                case 'u': {
                    uint32_t cp;
                    if (!ParseHex4(cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        if (pos_ + 2 > s_.size() || s_[pos_] != '\\' || s_[pos_ + 1] != 'u') return Err("lone high surrogate");
                        pos_ += 2;
                        uint32_t low;
                        if (!ParseHex4(low)) return false;
                        if (low < 0xDC00 || low > 0xDFFF) return Err("invalid low surrogate");
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                        return Err("lone low surrogate");
                    }
                    AppendUtf8(result, cp);
                    break;
                }
                default: return Err("invalid escape sequence");
            }
        }
        out = std::move(result);
        return true;
    }

    bool ParseNumber(JsonValue& out) {
        const size_t start = pos_;
        if (s_[pos_] == '-') ++pos_;
        if (pos_ >= s_.size()) return Err("truncated number");
        if (s_[pos_] == '0') {
            ++pos_;
        } else if (s_[pos_] >= '1' && s_[pos_] <= '9') {
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        } else {
            return Err("invalid number");
        }
        bool isInt = true;
        if (pos_ < s_.size() && s_[pos_] == '.') {
            isInt = false;
            ++pos_;
            if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return Err("invalid fraction");
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        if (pos_ < s_.size() && (s_[pos_] == 'e' || s_[pos_] == 'E')) {
            isInt = false;
            ++pos_;
            if (pos_ < s_.size() && (s_[pos_] == '+' || s_[pos_] == '-')) ++pos_;
            if (pos_ >= s_.size() || s_[pos_] < '0' || s_[pos_] > '9') return Err("invalid exponent");
            while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
        }
        const std::string token = s_.substr(start, pos_ - start);
        if (isInt) {
            errno = 0;
            char* end = nullptr;
            const long long v = std::strtoll(token.c_str(), &end, 10);
            if (errno == 0 && end && *end == '\0') {
                out = JsonValue::Int(v);
                return true;
            }
            // Integer literal too large for int64: fall through to double.
        }
        const double d = std::strtod(token.c_str(), nullptr);
        if (!std::isfinite(d)) return Err("number out of range");
        out = JsonValue::Double(d);
        return true;
    }

    const std::string& s_;
    size_t pos_ = 0;
    std::string err_ = "parse error";
};

void EscapeString(const std::string& in, std::string& out) {
    out += '"';
    for (unsigned char c : in) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

std::string FormatDouble(double d) {
    if (!std::isfinite(d)) return "null"; // JSON has no NaN/Inf
    char buf[40];
    for (int precision : {15, 17}) {
        std::snprintf(buf, sizeof(buf), "%.*g", precision, d);
        if (std::strtod(buf, nullptr) == d) break;
    }
    std::string s = buf;
    // Keep doubles distinguishable from ints on a round trip ("2" -> "2.0").
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    return s;
}

void Write(const JsonValue& v, bool pretty, int indent, std::string& out) {
    auto newline = [&](int level) {
        if (!pretty) return;
        out += '\n';
        out.append(static_cast<size_t>(level) * 2, ' ');
    };
    switch (v.type()) {
        case JsonValue::Type::Null: out += "null"; break;
        case JsonValue::Type::Bool: out += v.AsBool() ? "true" : "false"; break;
        case JsonValue::Type::Int: out += std::to_string(v.AsInt()); break;
        case JsonValue::Type::Double: out += FormatDouble(v.AsDouble()); break;
        case JsonValue::Type::String: EscapeString(v.AsString(), out); break;
        case JsonValue::Type::Array: {
            if (v.items().empty()) {
                out += "[]";
                break;
            }
            out += '[';
            bool first = true;
            for (const auto& item : v.items()) {
                if (!first) out += ',';
                first = false;
                newline(indent + 1);
                Write(item, pretty, indent + 1, out);
            }
            newline(indent);
            out += ']';
            break;
        }
        case JsonValue::Type::Object: {
            if (v.members().empty()) {
                out += "{}";
                break;
            }
            out += '{';
            bool first = true;
            for (const auto& m : v.members()) {
                if (!first) out += ',';
                first = false;
                newline(indent + 1);
                EscapeString(m.first, out);
                out += pretty ? ": " : ":";
                Write(m.second, pretty, indent + 1, out);
            }
            newline(indent);
            out += '}';
            break;
        }
    }
}

} // namespace

bool ParseJson(const std::string& text, JsonValue& out, std::string* error) {
    Parser parser(text);
    return parser.Parse(out, error);
}

std::string SerializeJson(const JsonValue& value, bool pretty) {
    std::string out;
    Write(value, pretty, 0, out);
    return out;
}

} // namespace scrapi
