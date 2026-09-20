#pragma once
// Saver-side protocol engine (docs/SCRAPI_SPEC.md §4, §7): turns incoming request
// lines into validated changes on a ControlModel, calls back into the application to
// apply them, and produces the response/event lines to send. Transport-agnostic and
// single-threaded by design: the host feeds it whole lines from the *render thread at
// a frame boundary*, so every `set` is applied atomically with respect to rendering.

#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "ControlModel.h"
#include "Manifest.h"
#include "Protocol.h"

namespace scrapi {

class ServerCore {
public:
    struct Change {
        std::string id;
        JsonValue value; // already validated/clamped
    };
    struct Hooks {
        // Called once per `set` request with every change that validated, in request
        // order, after the model already holds the new values.
        std::function<void(const std::vector<Change>&)> onSet;
        // Called for `invoke` of a button. Return false (and fill *error) to report
        // a failure back to the viewer.
        std::function<bool(const std::string& id, const JsonValue& args, std::string* error)> onInvoke;
    };
    using Send = std::function<void(const std::string& line)>;

    ServerCore(Manifest manifest, Hooks hooks, Send send);

    // Feed one received line (no trailing newline). Never throws; malformed input just
    // produces an error response (or is dropped if it has no usable id).
    void HandleLine(const std::string& line);

    // The window the viewer may resize (`viewport.resize`); reported in the hello reply.
    void SetViewportHandle(int64_t handle) { viewportHandle_ = handle; }

    // Saver-driven value changes (readouts, or values the saver itself changed). They
    // are sent to the viewer as `changed` events on the next Flush().
    void SetValue(const std::string& id, const JsonValue& value);

    // Emits pending `changed` events for subscribed controls, no more often than the
    // subscription's maxHz. `nowSeconds` is any monotonic clock.
    void Flush(double nowSeconds);

    // Replaces the manifest (e.g. options changed). Bumps `rev`, keeps values of controls
    // that survive with a compatible value, and notifies the viewer with `manifestChanged`.
    void ReplaceManifest(Manifest manifest);

    void SendLog(const std::string& level, const std::string& message);

    const ControlModel& model() const { return model_; }
    bool handshakeDone() const { return handshakeDone_; }
    bool closed() const { return closed_; }

private:
    void Respond(const std::string& line) { if (send_) send_(line); }
    void HandleHello(const Request& r);
    void HandleManifest(const Request& r);
    void HandleGet(const Request& r);
    void HandleSet(const Request& r);
    void HandleInvoke(const Request& r);
    void HandleSubscribe(const Request& r);
    bool Subscribed(const std::string& id) const;

    ControlModel model_;
    Hooks hooks_;
    Send send_;
    bool handshakeDone_ = false;
    bool closed_ = false;
    int64_t viewportHandle_ = 0;

    bool subscribeAll_ = false;
    std::set<std::string> subscribed_;
    double minIntervalSeconds_ = 0.1;
    double lastFlushSeconds_ = -1e9;
    std::vector<std::string> dirty_; // ids awaiting a `changed` event, in first-change order
};

} // namespace scrapi
