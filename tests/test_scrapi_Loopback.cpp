#include "test_framework.h"

#include <deque>

#include "scrapi_fixture.h"
#include "../src/scrapi/ClientCore.h"
#include "../src/scrapi/Protocol.h"
#include "../src/scrapi/ServerCore.h"

using namespace scrapi;

namespace {

// Two in-memory line queues wired between a ServerCore and a ClientCore, delivered
// only when Pump() is called -- like a real pipe (no re-entrant delivery).
struct Loopback {
    std::deque<std::string> toServer, toClient;
    std::vector<ServerCore::Change> setSeen;
    std::vector<std::string> invoked;
    bool failInvoke = false;
    ServerCore server;
    ClientCore client;

    explicit Loopback(Manifest manifest = scrapi_fixture::MakeManifest())
        : server(std::move(manifest),
                 ServerCore::Hooks{[this](const std::vector<ServerCore::Change>& c) {
                                       for (const auto& ch : c) setSeen.push_back(ch);
                                   },
                                   [this](const std::string& id, const JsonValue&, std::string* error) {
                                       invoked.push_back(id);
                                       if (failInvoke) {
                                           *error = "refused";
                                           return false;
                                       }
                                       return true;
                                   }},
                 [this](const std::string& line) { toClient.push_back(line); }),
          client([this](const std::string& line) { toServer.push_back(line); }) {}

    void Pump() {
        for (int guard = 0; guard < 1000 && (!toServer.empty() || !toClient.empty()); ++guard) {
            while (!toServer.empty()) {
                const std::string line = std::move(toServer.front());
                toServer.pop_front();
                server.HandleLine(line);
            }
            while (!toClient.empty()) {
                const std::string line = std::move(toClient.front());
                toClient.pop_front();
                client.OnLine(line);
            }
        }
    }
    void Connect() {
        client.StartSession("test");
        Pump();
    }
};

} // namespace

TEST_CASE(Loopback_SessionReachesReadyWithManifestAndValues) {
    Loopback lb;
    lb.server.SetViewportHandle(4242);
    bool ready = false;
    lb.client.onReady = [&] { ready = true; };
    lb.Connect();
    CHECK(ready);
    CHECK(lb.client.state() == ClientCore::State::Ready);
    CHECK(lb.client.saver().name == "Demo Saver");
    CHECK(lb.client.saver().version == "0.1.0");
    CHECK_EQ(lb.client.viewportHandle(), static_cast<int64_t>(4242));
    CHECK(lb.client.capabilities().size() == 2);
    const ControlModel* model = lb.client.model();
    CHECK(model != nullptr);
    if (!model) return;
    CHECK(model->Find("fixed.speed") != nullptr);
    CHECK(*model->Get("fixed.count") == JsonValue::Int(10));
}

TEST_CASE(Loopback_SetIsValidatedClampedAndAppliedThroughTheHook) {
    Loopback lb;
    lb.Connect();
    std::vector<std::string> changedIds;
    lb.client.onValuesChanged = [&](const std::vector<std::string>& ids) { changedIds = ids; };
    lb.client.Set({{"fixed.count", JsonValue::Int(500)}, {"mode", JsonValue::String("fixed")}});
    lb.Pump();
    // hook saw the coerced values, in order
    CHECK_EQ(lb.setSeen.size(), static_cast<size_t>(2));
    CHECK(lb.setSeen[0].id == "fixed.count");
    CHECK(lb.setSeen[0].value == JsonValue::Int(100)); // clamped
    // the client cache adopted the *applied* value, not what it asked for
    CHECK(*lb.client.model()->Get("fixed.count") == JsonValue::Int(100));
    CHECK(*lb.client.model()->Get("mode") == JsonValue::String("fixed"));
    CHECK_EQ(changedIds.size(), static_cast<size_t>(2));
    CHECK(lb.client.model()->IsVisible("fixed.speed")); // visibleWhen re-evaluates from the cache
}

TEST_CASE(Loopback_PartiallyInvalidSetAppliesTheValidPartAndReportsPerIdErrors) {
    Loopback lb;
    lb.Connect();
    bool got = false;
    lb.client.Set({{"enabled", JsonValue::Bool(false)}, {"title", JsonValue::String("waytoolongtitle")},
                   {"ghost", JsonValue::Int(1)}},
                  [&](const ClientCore::Reply& r) {
                      got = true;
                      CHECK(r.ok); // the request itself succeeded
                      const JsonValue* results = r.body.Find("results");
                      CHECK(results != nullptr);
                      if (!results) return;
                      CHECK(results->Find("enabled")->Find("ok")->AsBool());
                      CHECK(!results->Find("title")->Find("ok")->AsBool(true));
                      CHECK(results->Find("ghost")->Find("error")->Find("code")->AsString() == err::kUnknownId);
                  });
    lb.Pump();
    CHECK(got);
    CHECK_EQ(lb.setSeen.size(), static_cast<size_t>(1)); // only "enabled" reached the app
    CHECK(*lb.client.model()->Get("enabled") == JsonValue::Bool(false));
    CHECK(*lb.client.model()->Get("title") == JsonValue::String(""));
}

TEST_CASE(Loopback_ReadoutsArriveAsRateLimitedChangedEvents) {
    Loopback lb;
    lb.Connect(); // subscribes "*" at 10 Hz
    std::vector<std::string> changed;
    lb.client.onValuesChanged = [&](const std::vector<std::string>& ids) {
        for (const auto& id : ids) changed.push_back(id);
    };

    lb.server.SetValue("status", JsonValue::String("one"));
    lb.server.Flush(100.0);
    lb.Pump();
    CHECK_EQ(changed.size(), static_cast<size_t>(1));
    CHECK(*lb.client.model()->Get("status") == JsonValue::String("one"));

    lb.server.SetValue("status", JsonValue::String("two"));
    lb.server.Flush(100.01); // 10 ms later: under the 100 ms interval -> held back
    lb.Pump();
    CHECK_EQ(changed.size(), static_cast<size_t>(1));
    lb.server.Flush(100.2);  // interval elapsed -> delivered
    lb.Pump();
    CHECK_EQ(changed.size(), static_cast<size_t>(2));
    CHECK(*lb.client.model()->Get("status") == JsonValue::String("two"));

    lb.server.SetValue("status", JsonValue::String("two")); // no actual change -> no event
    lb.server.Flush(200.0);
    lb.Pump();
    CHECK_EQ(changed.size(), static_cast<size_t>(2));
}

TEST_CASE(Loopback_ManyChangesBetweenFlushesCoalesceIntoOneEvent) {
    Loopback lb;
    lb.Connect();
    int events = 0;
    lb.client.onValuesChanged = [&](const std::vector<std::string>&) { ++events; };
    for (int i = 0; i < 50; ++i) lb.server.SetValue("status", JsonValue::String("v" + std::to_string(i)));
    lb.server.Flush(500.0);
    lb.Pump();
    CHECK_EQ(events, 1);
    CHECK(*lb.client.model()->Get("status") == JsonValue::String("v49"));
}

TEST_CASE(Loopback_InvokeReachesTheHookAndFailuresComeBack) {
    Loopback lb;
    lb.Connect();
    bool ok1 = false, ok2 = true;
    lb.client.Invoke("go", JsonValue::Null(), [&](const ClientCore::Reply& r) { ok1 = r.ok; });
    lb.Pump();
    CHECK(ok1);
    CHECK_EQ(lb.invoked.size(), static_cast<size_t>(1));

    lb.failInvoke = true;
    std::string message;
    lb.client.Invoke("go", JsonValue::Null(), [&](const ClientCore::Reply& r) {
        ok2 = r.ok;
        message = r.errorMessage;
    });
    lb.Pump();
    CHECK(!ok2);
    CHECK(message == "refused");

    // not a button / unknown
    std::string code;
    lb.client.Invoke("enabled", JsonValue::Null(), [&](const ClientCore::Reply& r) { code = r.errorCode; });
    lb.Pump();
    CHECK(code == err::kBadRequest);
    lb.client.Invoke("ghost", JsonValue::Null(), [&](const ClientCore::Reply& r) { code = r.errorCode; });
    lb.Pump();
    CHECK(code == err::kUnknownId);
}

TEST_CASE(Loopback_NothingWorksBeforeHello) {
    Loopback lb;
    std::string code;
    lb.client.Get({}, [&](const ClientCore::Reply& r) { code = r.errorCode; });
    lb.Pump();
    CHECK(code == err::kNotReady);
    CHECK(!lb.server.handshakeDone());
}

TEST_CASE(Loopback_MajorVersionMismatchIsRefusedAndFailsTheSession) {
    Loopback lb;
    std::string reason;
    lb.client.onFailed = [&](const std::string& r) { reason = r; };
    lb.server.HandleLine("{\"id\":1,\"op\":\"hello\",\"apiVersion\":\"2.0\"}");
    // (drive the client through its own path too, with a wrong version)
    bool refused = false;
    lb.toClient.clear();
    lb.client.Hello("test", [&](const ClientCore::Reply&) {});
    lb.toServer.clear();
    lb.server.HandleLine("{\"id\":5,\"op\":\"hello\",\"apiVersion\":\"9.9\"}");
    Incoming in = ParseIncoming(lb.toClient.back());
    refused = in.kind == Incoming::Kind::Response && !in.ok && in.errorCode == err::kUnsupported;
    CHECK(refused);
    CHECK(!lb.server.handshakeDone());
}

TEST_CASE(Loopback_MinorVersionDifferenceIsAccepted) {
    Loopback lb;
    lb.server.HandleLine("{\"id\":1,\"op\":\"hello\",\"apiVersion\":\"1.7\"}");
    CHECK(lb.server.handshakeDone());
}

TEST_CASE(Loopback_ServerSurvivesGarbageAndUnknownOps) {
    Loopback lb;
    lb.Connect();
    lb.toClient.clear();
    lb.server.HandleLine("this is not json");
    lb.server.HandleLine("{\"op\":\"set\"}");                  // no id
    lb.server.HandleLine("{\"id\":9,\"op\":\"teleport\"}");    // unknown op
    lb.server.HandleLine("{\"id\":10,\"op\":\"set\",\"values\":5}");
    lb.server.HandleLine("{\"id\":11,\"op\":\"get\",\"ids\":\"x\"}");
    lb.server.HandleLine("{\"id\":12,\"op\":\"invoke\"}");
    lb.server.HandleLine("{\"id\":13,\"op\":\"subscribe\"}");
    CHECK_EQ(lb.toClient.size(), static_cast<size_t>(7)); // every one answered with an error, none crashed
    for (const auto& line : lb.toClient) {
        const Incoming in = ParseIncoming(line);
        CHECK(in.kind == Incoming::Kind::Response);
        CHECK(!in.ok);
    }
    // and the session still works afterwards
    lb.toClient.clear();
    lb.client.Get({"enabled"});
    lb.Pump();
    CHECK(lb.client.state() == ClientCore::State::Ready);
}

TEST_CASE(Loopback_GetUnknownIdFailsTheRequest) {
    Loopback lb;
    lb.Connect();
    std::string code;
    lb.client.Get({"enabled", "ghost"}, [&](const ClientCore::Reply& r) { code = r.errorCode; });
    lb.Pump();
    CHECK(code == err::kUnknownId);
}

TEST_CASE(Loopback_SubscribingToASubsetOnlyReportsThatSubset) {
    Loopback lb;
    lb.Connect();
    lb.server.HandleLine("{\"id\":50,\"op\":\"subscribe\",\"ids\":[\"title\"],\"maxHz\":60}");
    lb.toClient.clear();
    lb.server.SetValue("status", JsonValue::String("hidden"));
    lb.server.SetValue("title", JsonValue::String("shown"));
    lb.server.Flush(10.0);
    CHECK_EQ(lb.toClient.size(), static_cast<size_t>(1));
    const Incoming ev = ParseIncoming(lb.toClient.back());
    CHECK(ev.body.Find("values")->Find("title") != nullptr);
    CHECK(ev.body.Find("values")->Find("status") == nullptr);
}

TEST_CASE(Loopback_ManifestChangeIsAnnouncedAndRefetched) {
    Loopback lb;
    lb.Connect();
    lb.client.Set({{"mode", JsonValue::String("fixed")}});
    lb.Pump();
    int manifestChanges = 0;
    lb.client.onManifestChanged = [&] { ++manifestChanges; };

    Manifest next = scrapi_fixture::MakeManifest();
    next.controls[0].options.push_back(scrapi_fixture::Opt("extra", "Extra"));
    lb.server.ReplaceManifest(std::move(next));
    lb.Pump();
    CHECK_EQ(manifestChanges, 1);
    CHECK_EQ(lb.client.model()->manifest().rev, 2);
    CHECK_EQ(lb.client.model()->Find("mode")->options.size(), static_cast<size_t>(3));
    // values of surviving controls carried over
    CHECK(*lb.server.model().Get("mode") == JsonValue::String("fixed"));
    CHECK(*lb.client.model()->Get("mode") == JsonValue::String("fixed"));
}

TEST_CASE(Loopback_ByeClosesBothSides) {
    Loopback lb;
    lb.Connect();
    bool closed = false;
    lb.client.onClosed = [&] { closed = true; };
    lb.client.Bye();
    lb.Pump();
    CHECK(closed);
    CHECK(lb.server.closed());
    CHECK(lb.client.state() == ClientCore::State::Closed);
}

TEST_CASE(Loopback_ClientIgnoresGarbageAndUnknownEvents) {
    Loopback lb;
    lb.Connect();
    lb.client.OnLine("garbage");
    lb.client.OnLine("{\"ev\":\"someFutureEvent\",\"x\":1}");
    lb.client.OnLine("{\"id\":99999,\"ok\":true}"); // response nobody is waiting for
    CHECK(lb.client.state() == ClientCore::State::Ready);
}

TEST_CASE(Loopback_LogEventsReachTheViewer) {
    Loopback lb;
    lb.Connect();
    std::string level, message;
    lb.client.onLog = [&](const std::string& l, const std::string& m) {
        level = l;
        message = m;
    };
    lb.server.SendLog("warn", "something odd");
    lb.Pump();
    CHECK(level == "warn");
    CHECK(message == "something odd");
}
