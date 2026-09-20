#include "test_framework.h"

#include "../src/scrapi/Protocol.h"

using namespace scrapi;

TEST_CASE(Protocol_ParsesARequest) {
    Request r;
    std::string error;
    CHECK(ParseRequest("{\"id\":12,\"op\":\"set\",\"values\":{\"a\":1}}", r, &error));
    CHECK(r.hasId);
    CHECK_EQ(r.id, static_cast<int64_t>(12));
    CHECK(r.op == "set");
    CHECK(r.body.Find("values") != nullptr);
}

TEST_CASE(Protocol_RequestWithoutIdIsAllowedToParse) {
    Request r;
    CHECK(ParseRequest("{\"op\":\"bye\"}", r));
    CHECK(!r.hasId);
}

TEST_CASE(Protocol_RejectsBadRequests) {
    Request r;
    CHECK(!ParseRequest("not json", r));
    CHECK(!ParseRequest("[]", r));
    CHECK(!ParseRequest("{\"id\":1}", r));                      // no op
    CHECK(!ParseRequest("{\"id\":\"x\",\"op\":\"get\"}", r));   // id not an integer
    CHECK(!ParseRequest("{\"id\":1,\"op\":5}", r));             // op not a string
    CHECK(!ParseRequest(std::string(kMaxMessageBytes + 1, ' '), r)); // over the size cap
}

TEST_CASE(Protocol_BuildersProduceSingleLineJsonThatParsesBack) {
    JsonValue body = JsonValue::Object();
    body.Set("values", JsonValue::Object());
    const std::string req = MakeRequestLine(3, "set", body);
    CHECK(req.find('\n') == std::string::npos);
    Request r;
    CHECK(ParseRequest(req, r));
    CHECK(r.op == "set");
    CHECK_EQ(r.id, static_cast<int64_t>(3));

    const Incoming ok = ParseIncoming(MakeOkLine(3, body));
    CHECK(ok.kind == Incoming::Kind::Response);
    CHECK(ok.ok);
    CHECK_EQ(ok.id, static_cast<int64_t>(3));

    const Incoming bad = ParseIncoming(MakeErrorLine(4, "unknownId", "no control 'x'"));
    CHECK(bad.kind == Incoming::Kind::Response);
    CHECK(!bad.ok);
    CHECK(bad.errorCode == "unknownId");
    CHECK(bad.errorMessage == "no control 'x'");

    const Incoming ev = ParseIncoming(MakeEventLine("changed", body));
    CHECK(ev.kind == Incoming::Kind::Event);
    CHECK(ev.ev == "changed");
}

TEST_CASE(Protocol_MessageTextWithNewlinesStaysOnOneLine) {
    const std::string line = MakeErrorLine(1, "internal", "line1\nline2");
    CHECK(line.find('\n') == std::string::npos);
    CHECK(ParseIncoming(line).errorMessage == "line1\nline2");
}

TEST_CASE(Protocol_IncomingGarbageIsInvalidNotFatal) {
    CHECK(ParseIncoming("").kind == Incoming::Kind::Invalid);
    CHECK(ParseIncoming("{\"hello\":1}").kind == Incoming::Kind::Invalid);
    CHECK(ParseIncoming("[1,2]").kind == Incoming::Kind::Invalid);
    CHECK(ParseIncoming("{\"id\":1}").kind == Incoming::Kind::Invalid); // response needs "ok"
}

TEST_CASE(Protocol_ApiMajorVersion) {
    CHECK_EQ(ApiMajorVersion("1.0"), 1);
    CHECK_EQ(ApiMajorVersion("12.34"), 12);
    CHECK_EQ(ApiMajorVersion("1"), -1);
    CHECK_EQ(ApiMajorVersion(".5"), -1);
    CHECK_EQ(ApiMajorVersion("x.1"), -1);
    CHECK_EQ(ApiMajorVersion(""), -1);
}
