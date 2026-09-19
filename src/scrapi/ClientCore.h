#pragma once
// Viewer-side protocol engine (docs/SCRAPI_SPEC.md §4): numbers requests, matches
// responses to them, keeps a value cache (a ControlModel built from the saver's
// manifest) up to date from `set` replies and `changed` events, and reports what
// happened through callbacks. Transport-agnostic; UI-agnostic.

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "ControlModel.h"
#include "Manifest.h"

namespace scrapi {

class ClientCore {
public:
    using Send = std::function<void(const std::string& line)>;

    struct Reply {
        bool ok = false;
        std::string errorCode;
        std::string errorMessage;
        JsonValue body; // the whole response object
    };
    using ReplyFn = std::function<void(const Reply&)>;

    enum class State { Idle, Handshaking, Ready, Failed, Closed };

    explicit ClientCore(Send send) : send_(std::move(send)) {}

    // hello -> manifest -> get(all) -> subscribe("*"); onReady fires when done, or
    // onFailed with a reason if any step is rejected.
    void StartSession(const std::string& clientName, double subscribeMaxHz = 10.0);

    // Individual requests (all also usable directly). `cb` is optional.
    void Hello(const std::string& clientName, ReplyFn cb = {});
    void FetchManifest(ReplyFn cb = {});
    void Get(const std::vector<std::string>& ids, ReplyFn cb = {}); // empty = all
    void Set(const std::vector<std::pair<std::string, JsonValue>>& values, ReplyFn cb = {});
    void Invoke(const std::string& id, JsonValue args = JsonValue::Null(), ReplyFn cb = {});
    void SubscribeAll(double maxHz, ReplyFn cb = {});
    void Bye();

    // Feed one received line (no trailing newline).
    void OnLine(const std::string& line);

    State state() const { return state_; }
    const std::string& failureReason() const { return failureReason_; }
    const SaverIdentity& saver() const { return saver_; }
    const std::vector<std::string>& capabilities() const { return capabilities_; }
    int64_t viewportHandle() const { return viewportHandle_; }
    // Null until the manifest has arrived.
    const ControlModel* model() const { return model_.get(); }
    ControlModel* mutableModel() { return model_.get(); }

    // Callbacks (all optional).
    std::function<void()> onReady;
    std::function<void(const std::string& reason)> onFailed;
    std::function<void()> onManifestChanged;                             // model() was replaced
    std::function<void(const std::vector<std::string>& ids)> onValuesChanged; // cache changed
    std::function<void(const std::string& level, const std::string& message)> onLog;
    std::function<void()> onClosed;

private:
    int64_t SendRequest(const std::string& op, const JsonValue& body, ReplyFn cb);
    void Fail(const std::string& reason);
    void ApplyValues(const JsonValue* valuesObject, bool fromSaver);

    Send send_;
    State state_ = State::Idle;
    std::string failureReason_;
    int64_t nextId_ = 1;
    std::map<int64_t, ReplyFn> pending_;
    SaverIdentity saver_;
    std::vector<std::string> capabilities_;
    int64_t viewportHandle_ = 0;
    std::unique_ptr<ControlModel> model_;
    double subscribeMaxHz_ = 10.0;
};

} // namespace scrapi
