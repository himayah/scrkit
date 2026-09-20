#include "ClientCore.h"

#include "Protocol.h"

namespace scrapi {

int64_t ClientCore::SendRequest(const std::string& op, const JsonValue& body, ReplyFn cb) {
    const int64_t id = nextId_++;
    pending_[id] = std::move(cb); // registered BEFORE sending: a loopback may reply synchronously
    if (send_) send_(MakeRequestLine(id, op, body));
    return id;
}

void ClientCore::Fail(const std::string& reason) {
    if (state_ == State::Failed || state_ == State::Closed) return;
    state_ = State::Failed;
    failureReason_ = reason;
    if (onFailed) onFailed(reason);
}

void ClientCore::Hello(const std::string& clientName, ReplyFn cb) {
    JsonValue body = JsonValue::Object();
    body.Set("apiVersion", JsonValue::String(kApiVersion));
    body.Set("client", JsonValue::String(clientName));
    SendRequest("hello", body, [this, cb](const Reply& r) {
        if (r.ok) {
            if (const JsonValue* s = r.body.Find("saver"); s && s->IsObject()) {
                saver_.id = s->Find("id") ? s->Find("id")->AsString("") : "";
                saver_.name = s->Find("name") ? s->Find("name")->AsString("") : "";
                saver_.version = s->Find("version") ? s->Find("version")->AsString("") : "";
            }
            capabilities_.clear();
            if (const JsonValue* c = r.body.Find("capabilities"); c && c->IsArray()) {
                for (const auto& item : c->items()) {
                    if (item.IsString()) capabilities_.push_back(item.AsString());
                }
            }
            if (const JsonValue* h = r.body.Find("viewportHwnd")) viewportHandle_ = h->AsInt(0);
        }
        if (cb) cb(r);
    });
}

void ClientCore::FetchManifest(ReplyFn cb) {
    SendRequest("manifest", JsonValue::Null(), [this, cb](const Reply& r) {
        if (r.ok) {
            Manifest manifest;
            std::string error;
            const JsonValue* m = r.body.Find("manifest");
            if (!m || !ManifestFromJson(*m, manifest, &error)) {
                Fail("bad manifest: " + (m ? error : std::string("missing")));
            } else {
                model_ = std::make_unique<ControlModel>(std::move(manifest));
                if (onManifestChanged) onManifestChanged();
            }
        }
        if (cb) cb(r);
    });
}

void ClientCore::ApplyValues(const JsonValue* values, bool fromSaver) {
    if (!model_ || !values || !values->IsObject()) return;
    std::vector<std::string> changed;
    for (const auto& m : values->members()) {
        const JsonValue* before = model_->Get(m.first);
        const JsonValue previous = before ? *before : JsonValue::Null();
        const ValueResult res = fromSaver ? model_->SetFromSaver(m.first, m.second) : model_->Set(m.first, m.second);
        if (res.ok && !(res.value == previous)) changed.push_back(m.first);
    }
    if (!changed.empty() && onValuesChanged) onValuesChanged(changed);
}

void ClientCore::Get(const std::vector<std::string>& ids, ReplyFn cb) {
    JsonValue body = JsonValue::Object();
    if (!ids.empty()) {
        JsonValue arr = JsonValue::Array();
        for (const auto& id : ids) arr.Push(JsonValue::String(id));
        body.Set("ids", std::move(arr));
    }
    SendRequest("get", body, [this, cb](const Reply& r) {
        if (r.ok) ApplyValues(r.body.Find("values"), /*fromSaver=*/true);
        if (cb) cb(r);
    });
}

void ClientCore::Set(const std::vector<std::pair<std::string, JsonValue>>& values, ReplyFn cb) {
    JsonValue obj = JsonValue::Object();
    for (const auto& v : values) obj.Set(v.first, v.second);
    JsonValue body = JsonValue::Object();
    body.Set("values", std::move(obj));
    SendRequest("set", body, [this, cb](const Reply& r) {
        if (r.ok) {
            // Adopt the saver's *applied* values (clamped/canonicalized) for every accepted id.
            JsonValue accepted = JsonValue::Object();
            if (const JsonValue* results = r.body.Find("results"); results && results->IsObject()) {
                for (const auto& m : results->members()) {
                    const JsonValue* ok = m.second.Find("ok");
                    const JsonValue* value = m.second.Find("value");
                    if (ok && ok->AsBool() && value) accepted.Set(m.first, *value);
                }
            }
            ApplyValues(&accepted, /*fromSaver=*/true);
        }
        if (cb) cb(r);
    });
}

void ClientCore::Invoke(const std::string& id, JsonValue args, ReplyFn cb) {
    JsonValue body = JsonValue::Object();
    body.Set("control", JsonValue::String(id));
    if (!args.IsNull()) body.Set("args", std::move(args));
    SendRequest("invoke", body, [cb](const Reply& r) {
        if (cb) cb(r);
    });
}

void ClientCore::SubscribeAll(double maxHz, ReplyFn cb) {
    JsonValue body = JsonValue::Object();
    body.Set("ids", JsonValue::String("*"));
    body.Set("maxHz", JsonValue::Double(maxHz));
    SendRequest("subscribe", body, [cb](const Reply& r) {
        if (cb) cb(r);
    });
}

void ClientCore::Bye() {
    if (state_ == State::Closed) return;
    if (send_) send_(SerializeJson([] {
                JsonValue o = JsonValue::Object();
                o.Set("op", JsonValue::String("bye"));
                return o;
            }()));
    state_ = State::Closed;
    if (onClosed) onClosed();
}

void ClientCore::StartSession(const std::string& clientName, double subscribeMaxHz) {
    state_ = State::Handshaking;
    subscribeMaxHz_ = subscribeMaxHz;
    Hello(clientName, [this](const Reply& hello) {
        if (!hello.ok) return Fail("hello rejected: " + hello.errorMessage);
        FetchManifest([this](const Reply& manifest) {
            if (!manifest.ok) return Fail("manifest rejected: " + manifest.errorMessage);
            if (state_ == State::Failed) return; // bad manifest content
            Get({}, [this](const Reply& values) {
                if (!values.ok) return Fail("get rejected: " + values.errorMessage);
                SubscribeAll(subscribeMaxHz_, [this](const Reply& sub) {
                    if (!sub.ok) return Fail("subscribe rejected: " + sub.errorMessage);
                    if (state_ == State::Handshaking) {
                        state_ = State::Ready;
                        if (onReady) onReady();
                    }
                });
            });
        });
    });
}

void ClientCore::OnLine(const std::string& line) {
    const Incoming in = ParseIncoming(line);
    switch (in.kind) {
        case Incoming::Kind::Invalid: return; // ignore garbage (SPEC §1.5: keep going)
        case Incoming::Kind::Response: {
            auto it = pending_.find(in.id);
            if (it == pending_.end()) return;
            ReplyFn cb = std::move(it->second);
            pending_.erase(it);
            if (!cb) return;
            Reply reply;
            reply.ok = in.ok;
            reply.errorCode = in.errorCode;
            reply.errorMessage = in.errorMessage;
            reply.body = in.body;
            cb(reply);
            return;
        }
        case Incoming::Kind::Event: {
            if (in.ev == "changed") {
                ApplyValues(in.body.Find("values"), /*fromSaver=*/true);
            } else if (in.ev == "manifestChanged") {
                FetchManifest([this](const Reply& r) {
                    if (r.ok) Get({});
                });
            } else if (in.ev == "log") {
                if (onLog) {
                    const JsonValue* level = in.body.Find("level");
                    const JsonValue* message = in.body.Find("message");
                    onLog(level ? level->AsString("info") : "info", message ? message->AsString("") : "");
                }
            }
            // Unknown events are ignored (forward compatibility).
            return;
        }
    }
}

} // namespace scrapi
