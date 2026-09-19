#include "Protocol.h"

#include <cstdlib>

namespace scrapi {

bool ParseRequest(const std::string& line, Request& out, std::string* error) {
    auto fail = [&](const std::string& msg) {
        if (error) *error = msg;
        return false;
    };
    if (line.size() > kMaxMessageBytes) return fail("message too large");
    JsonValue json;
    std::string parseError;
    if (!ParseJson(line, json, &parseError)) return fail("invalid JSON: " + parseError);
    if (!json.IsObject()) return fail("request must be a JSON object");
    const JsonValue* op = json.Find("op");
    if (!op || !op->IsString()) return fail("request is missing a string 'op'");
    Request r;
    r.op = op->AsString();
    if (const JsonValue* id = json.Find("id")) {
        if (!id->TryGetInt(r.id)) return fail("'id' must be an integer");
        r.hasId = true;
    }
    r.body = std::move(json);
    out = std::move(r);
    return true;
}

Incoming ParseIncoming(const std::string& line) {
    Incoming in;
    if (line.size() > kMaxMessageBytes) return in;
    JsonValue json;
    if (!ParseJson(line, json) || !json.IsObject()) return in;
    if (const JsonValue* ev = json.Find("ev"); ev && ev->IsString()) {
        in.kind = Incoming::Kind::Event;
        in.ev = ev->AsString();
        in.body = std::move(json);
        return in;
    }
    const JsonValue* id = json.Find("id");
    const JsonValue* ok = json.Find("ok");
    if (id && ok && ok->IsBool() && id->TryGetInt(in.id)) {
        in.kind = Incoming::Kind::Response;
        in.ok = ok->AsBool();
        if (!in.ok) {
            if (const JsonValue* e = json.Find("error"); e && e->IsObject()) {
                if (const JsonValue* c = e->Find("code"); c && c->IsString()) in.errorCode = c->AsString();
                if (const JsonValue* m = e->Find("message"); m && m->IsString()) in.errorMessage = m->AsString();
            }
        }
        in.body = std::move(json);
    }
    return in;
}

namespace {
void Merge(JsonValue& target, const JsonValue& body) {
    if (!body.IsObject()) return;
    for (const auto& m : body.members()) target.Set(m.first, m.second);
}
} // namespace

std::string MakeRequestLine(int64_t id, const std::string& op, const JsonValue& body) {
    JsonValue o = JsonValue::Object();
    o.Set("id", JsonValue::Int(id));
    o.Set("op", JsonValue::String(op));
    Merge(o, body);
    return SerializeJson(o);
}

std::string MakeOkLine(int64_t id, const JsonValue& body) {
    JsonValue o = JsonValue::Object();
    o.Set("id", JsonValue::Int(id));
    o.Set("ok", JsonValue::Bool(true));
    Merge(o, body);
    return SerializeJson(o);
}

std::string MakeErrorLine(int64_t id, const std::string& code, const std::string& message) {
    JsonValue o = JsonValue::Object();
    o.Set("id", JsonValue::Int(id));
    o.Set("ok", JsonValue::Bool(false));
    JsonValue e = JsonValue::Object();
    e.Set("code", JsonValue::String(code));
    e.Set("message", JsonValue::String(message));
    o.Set("error", std::move(e));
    return SerializeJson(o);
}

std::string MakeEventLine(const std::string& ev, const JsonValue& body) {
    JsonValue o = JsonValue::Object();
    o.Set("ev", JsonValue::String(ev));
    Merge(o, body);
    return SerializeJson(o);
}

int ApiMajorVersion(const std::string& version) {
    const size_t dot = version.find('.');
    if (dot == std::string::npos || dot == 0) return -1;
    for (size_t i = 0; i < dot; ++i) {
        if (version[i] < '0' || version[i] > '9') return -1;
    }
    return std::atoi(version.substr(0, dot).c_str());
}

} // namespace scrapi
