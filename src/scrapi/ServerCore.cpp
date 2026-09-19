#include "ServerCore.h"

#include <algorithm>

#include "Protocol.h"

namespace scrapi {

ServerCore::ServerCore(Manifest manifest, Hooks hooks, Send send)
    : model_(std::move(manifest)), hooks_(std::move(hooks)), send_(std::move(send)) {}

void ServerCore::HandleLine(const std::string& line) {
    Request r;
    std::string error;
    if (!ParseRequest(line, r, &error)) {
        Respond(MakeErrorLine(0, err::kBadRequest, error));
        return;
    }
    if (r.op == "bye") {
        closed_ = true;
        if (r.hasId) Respond(MakeOkLine(r.id));
        return;
    }
    if (!r.hasId) {
        Respond(MakeErrorLine(0, err::kBadRequest, "request is missing an integer 'id'"));
        return;
    }
    if (r.op == "hello") {
        HandleHello(r);
        return;
    }
    if (!handshakeDone_) {
        Respond(MakeErrorLine(r.id, err::kNotReady, "send 'hello' first"));
        return;
    }
    if (r.op == "manifest") HandleManifest(r);
    else if (r.op == "get") HandleGet(r);
    else if (r.op == "set") HandleSet(r);
    else if (r.op == "invoke") HandleInvoke(r);
    else if (r.op == "subscribe") HandleSubscribe(r);
    else Respond(MakeErrorLine(r.id, err::kUnsupported, "unknown op '" + r.op + "'"));
}

void ServerCore::HandleHello(const Request& r) {
    const JsonValue* version = r.body.Find("apiVersion");
    if (!version || !version->IsString() || ApiMajorVersion(version->AsString()) < 0) {
        Respond(MakeErrorLine(r.id, err::kBadRequest, "hello needs a string 'apiVersion' like \"1.0\""));
        return;
    }
    if (ApiMajorVersion(version->AsString()) != ApiMajorVersion(kApiVersion)) {
        Respond(MakeErrorLine(r.id, err::kUnsupported,
                              "unsupported apiVersion '" + version->AsString() + "' (this saver speaks " + kApiVersion + ")"));
        return;
    }
    handshakeDone_ = true;
    const Manifest& m = model_.manifest();
    JsonValue body = JsonValue::Object();
    body.Set("apiVersion", JsonValue::String(kApiVersion));
    JsonValue saver = JsonValue::Object();
    saver.Set("id", JsonValue::String(m.saver.id));
    saver.Set("name", JsonValue::String(m.saver.name));
    saver.Set("version", JsonValue::String(m.saver.version));
    body.Set("saver", std::move(saver));
    JsonValue caps = JsonValue::Array();
    for (const auto& c : m.capabilities) caps.Push(JsonValue::String(c));
    body.Set("capabilities", std::move(caps));
    if (viewportHandle_ != 0) body.Set("viewportHwnd", JsonValue::Int(viewportHandle_));
    Respond(MakeOkLine(r.id, body));
}

void ServerCore::HandleManifest(const Request& r) {
    JsonValue body = JsonValue::Object();
    body.Set("manifest", ManifestToJson(model_.manifest()));
    Respond(MakeOkLine(r.id, body));
}

void ServerCore::HandleGet(const Request& r) {
    std::vector<std::string> ids;
    if (const JsonValue* list = r.body.Find("ids")) {
        if (!list->IsArray()) {
            Respond(MakeErrorLine(r.id, err::kBadRequest, "'ids' must be an array"));
            return;
        }
        for (const auto& item : list->items()) {
            if (!item.IsString()) {
                Respond(MakeErrorLine(r.id, err::kBadRequest, "'ids' must contain strings"));
                return;
            }
            ids.push_back(item.AsString());
        }
    } else {
        ids = model_.ValueIds();
    }
    JsonValue values = JsonValue::Object();
    for (const auto& id : ids) {
        const JsonValue* v = model_.Get(id);
        if (!v) {
            Respond(MakeErrorLine(r.id, err::kUnknownId, "no value-holding control '" + id + "'"));
            return;
        }
        values.Set(id, *v);
    }
    JsonValue body = JsonValue::Object();
    body.Set("values", std::move(values));
    Respond(MakeOkLine(r.id, body));
}

void ServerCore::HandleSet(const Request& r) {
    const JsonValue* values = r.body.Find("values");
    if (!values || !values->IsObject()) {
        Respond(MakeErrorLine(r.id, err::kBadRequest, "'values' must be an object"));
        return;
    }
    JsonValue results = JsonValue::Object();
    std::vector<Change> applied;
    for (const auto& m : values->members()) {
        const ValueResult res = model_.Set(m.first, m.second);
        JsonValue entry = JsonValue::Object();
        entry.Set("ok", JsonValue::Bool(res.ok));
        if (res.ok) {
            entry.Set("value", res.value);
            applied.push_back({m.first, res.value});
        } else {
            JsonValue e = JsonValue::Object();
            e.Set("code", JsonValue::String(res.errorCode));
            e.Set("message", JsonValue::String(res.errorMessage));
            entry.Set("error", std::move(e));
        }
        results.Set(m.first, std::move(entry));
    }
    if (!applied.empty() && hooks_.onSet) hooks_.onSet(applied);
    JsonValue body = JsonValue::Object();
    body.Set("results", std::move(results));
    Respond(MakeOkLine(r.id, body));
}

void ServerCore::HandleInvoke(const Request& r) {
    // The target is "control" (not "id": that is the request envelope's own number).
    const JsonValue* id = r.body.Find("control");
    if (!id || !id->IsString()) {
        Respond(MakeErrorLine(r.id, err::kBadRequest, "'control' must be a string"));
        return;
    }
    const ControlNode* node = model_.Find(id->AsString());
    if (!node) {
        Respond(MakeErrorLine(r.id, err::kUnknownId, "no control '" + id->AsString() + "'"));
        return;
    }
    if (node->type != ControlType::Button) {
        Respond(MakeErrorLine(r.id, err::kBadRequest, "'" + node->id + "' is not a button"));
        return;
    }
    std::string error;
    JsonValue args = JsonValue::Object();
    if (const JsonValue* a = r.body.Find("args")) args = *a;
    if (hooks_.onInvoke && !hooks_.onInvoke(node->id, args, &error)) {
        Respond(MakeErrorLine(r.id, err::kBadRequest, error.empty() ? "invoke failed" : error));
        return;
    }
    Respond(MakeOkLine(r.id));
}

void ServerCore::HandleSubscribe(const Request& r) {
    const JsonValue* ids = r.body.Find("ids");
    bool all = false;
    std::set<std::string> chosen;
    if (ids && ids->IsString() && ids->AsString() == "*") {
        all = true;
    } else if (ids && ids->IsArray()) {
        for (const auto& item : ids->items()) {
            if (!item.IsString()) {
                Respond(MakeErrorLine(r.id, err::kBadRequest, "'ids' must contain strings"));
                return;
            }
            chosen.insert(item.AsString());
        }
    } else {
        Respond(MakeErrorLine(r.id, err::kBadRequest, "'ids' must be \"*\" or an array of ids"));
        return;
    }
    double maxHz = 10.0;
    if (const JsonValue* hz = r.body.Find("maxHz"); hz && hz->IsNumber()) maxHz = hz->AsDouble();
    maxHz = std::clamp(maxHz, 0.1, 60.0);
    subscribeAll_ = all;
    subscribed_ = std::move(chosen);
    minIntervalSeconds_ = 1.0 / maxHz;
    Respond(MakeOkLine(r.id));
}

bool ServerCore::Subscribed(const std::string& id) const { return subscribeAll_ || subscribed_.count(id) > 0; }

void ServerCore::SetValue(const std::string& id, const JsonValue& value) {
    const JsonValue* before = model_.Get(id);
    const JsonValue previous = before ? *before : JsonValue::Null();
    const ValueResult res = model_.SetFromSaver(id, value);
    if (!res.ok || res.value == previous) return; // unchanged or invalid: nothing to report
    if (std::find(dirty_.begin(), dirty_.end(), id) == dirty_.end()) dirty_.push_back(id);
}

void ServerCore::Flush(double nowSeconds) {
    if (dirty_.empty() || nowSeconds - lastFlushSeconds_ < minIntervalSeconds_) return;
    JsonValue values = JsonValue::Object();
    for (const auto& id : dirty_) {
        if (!Subscribed(id)) continue; // dropped: an unsubscribed viewer can still `get` it
        if (const JsonValue* v = model_.Get(id)) values.Set(id, *v);
    }
    dirty_.clear();
    if (values.members().empty()) return;
    lastFlushSeconds_ = nowSeconds;
    JsonValue body = JsonValue::Object();
    body.Set("values", std::move(values));
    Respond(MakeEventLine("changed", body));
}

void ServerCore::ReplaceManifest(Manifest manifest) {
    manifest.rev = model_.manifest().rev + 1;
    ControlModel next(std::move(manifest));
    for (const auto& id : next.ValueIds()) {
        if (const JsonValue* old = model_.Get(id)) next.SetFromSaver(id, *old); // keeps it only if still valid
    }
    model_ = std::move(next);
    JsonValue body = JsonValue::Object();
    body.Set("rev", JsonValue::Int(model_.manifest().rev));
    Respond(MakeEventLine("manifestChanged", body));
}

void ServerCore::SendLog(const std::string& level, const std::string& message) {
    JsonValue body = JsonValue::Object();
    body.Set("level", JsonValue::String(level));
    body.Set("message", JsonValue::String(message));
    Respond(MakeEventLine("log", body));
}

} // namespace scrapi
