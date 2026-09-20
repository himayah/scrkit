#include "test_framework.h"

#include "../src/scrapi/Json.h"

using scrapi::JsonValue;
using scrapi::ParseJson;
using scrapi::SerializeJson;

namespace {
JsonValue MustParse(const std::string& text) {
    JsonValue v;
    std::string error;
    const bool ok = ParseJson(text, v, &error);
    CHECK(ok);
    return v;
}
bool Rejects(const std::string& text) {
    JsonValue v = JsonValue::Bool(true);
    const bool ok = ParseJson(text, v, nullptr);
    return !ok && v.IsBool(); // and `out` untouched
}
} // namespace

TEST_CASE(Json_ParsesScalars) {
    CHECK(MustParse("null").IsNull());
    CHECK(MustParse("true").AsBool());
    CHECK(!MustParse("false").AsBool(true));
    CHECK(MustParse("42").IsInt());
    CHECK_EQ(MustParse("42").AsInt(), static_cast<int64_t>(42));
    CHECK_EQ(MustParse("-7").AsInt(), static_cast<int64_t>(-7));
    CHECK(!MustParse("2.5").IsInt());
    CHECK(MustParse("2.5").AsDouble() == 2.5);
    CHECK(MustParse("1e3").AsDouble() == 1000.0);
    CHECK(MustParse("  \"hi\"  ").AsString() == "hi");
}

TEST_CASE(Json_IntegralDoubleCountsAsInt) {
    int64_t v = 0;
    CHECK(MustParse("3.0").TryGetInt(v));
    CHECK_EQ(v, static_cast<int64_t>(3));
    CHECK(!MustParse("3.5").TryGetInt(v));
}

TEST_CASE(Json_ObjectsKeepInsertionOrderAndArraysWork) {
    const JsonValue v = MustParse("{\"b\":1,\"a\":[1,2,3],\"c\":{\"x\":null}}");
    CHECK(v.IsObject());
    CHECK_EQ(v.members().size(), static_cast<size_t>(3));
    CHECK(v.members()[0].first == "b");
    CHECK(v.members()[1].first == "a");
    CHECK_EQ(v.Find("a")->items().size(), static_cast<size_t>(3));
    CHECK(v.Find("c")->Find("x")->IsNull());
    CHECK(v.Find("missing") == nullptr);
}

TEST_CASE(Json_StringEscapesAndUnicode) {
    CHECK(MustParse("\"a\\n\\t\\\"\\\\\\/b\"").AsString() == "a\n\t\"\\/b");
    CHECK(MustParse("\"\\u00e9\"").AsString() == "\xC3\xA9");             // é
    CHECK(MustParse("\"\\u65e5\"").AsString() == "\xE6\x97\xA5");         // 日
    CHECK(MustParse("\"\\ud83d\\ude00\"").AsString() == "\xF0\x9F\x98\x80"); // U+1F600 via surrogate pair
    CHECK(MustParse("\"日本語\"").AsString() == "日本語");                 // raw UTF-8 passes through
}

TEST_CASE(Json_RejectsMalformedInput) {
    CHECK(Rejects(""));
    CHECK(Rejects("{"));
    CHECK(Rejects("[1,]"));
    CHECK(Rejects("{\"a\":1,}"));
    CHECK(Rejects("{\"a\" 1}"));
    CHECK(Rejects("{a:1}"));
    CHECK(Rejects("'x'"));
    CHECK(Rejects("tru"));
    CHECK(Rejects("01"));
    CHECK(Rejects("1."));
    CHECK(Rejects("1e"));
    CHECK(Rejects("\"unterminated"));
    CHECK(Rejects("\"bad\\q\""));
    CHECK(Rejects("\"\\ud83d\""));   // lone high surrogate
    CHECK(Rejects("\"\\ude00\""));   // lone low surrogate
    CHECK(Rejects("1 2"));           // trailing garbage
    CHECK(Rejects("{} x"));
    CHECK(Rejects("\"tab\there\""));  // raw control character
}

TEST_CASE(Json_RejectsPathologicalNesting) {
    std::string deep(1000, '[');
    deep += std::string(1000, ']');
    CHECK(Rejects(deep));
    std::string ok(50, '[');
    ok += std::string(50, ']');
    JsonValue v;
    CHECK(ParseJson(ok, v));
}

TEST_CASE(Json_SerializeRoundTrips) {
    const std::string text =
        "{\"s\":\"a\\\"b\\n\",\"i\":-3,\"d\":0.25,\"b\":true,\"n\":null,\"arr\":[1,[2],{}],\"o\":{\"k\":\"v\"}}";
    const JsonValue v = MustParse(text);
    CHECK(SerializeJson(v) == text);
    CHECK(MustParse(SerializeJson(v, /*pretty=*/true)) == v);
}

TEST_CASE(Json_DoublesStayDistinguishableFromInts) {
    CHECK(SerializeJson(JsonValue::Double(2.0)) == "2.0");
    CHECK(SerializeJson(JsonValue::Int(2)) == "2");
    CHECK(SerializeJson(JsonValue::Double(0.1)) == "0.1");
    const JsonValue back = MustParse(SerializeJson(JsonValue::Double(1.0 / 3.0)));
    CHECK(back.AsDouble() == 1.0 / 3.0);
}

TEST_CASE(Json_FromFloatPrintsTheDecimalTheAuthorMeant) {
    CHECK(SerializeJson(JsonValue::FromFloat(0.7f)) == "0.7");
    CHECK(SerializeJson(JsonValue::FromFloat(0.03f)) == "0.03");
}

TEST_CASE(Json_ControlCharactersAreEscapedInOutput) {
    const std::string out = SerializeJson(JsonValue::String(std::string("a\x01" "b")));
    CHECK(out == "\"a\\u0001b\"");
    CHECK(out.find('\n') == std::string::npos);
}

TEST_CASE(Json_NumbersCompareAcrossIntAndDouble) {
    CHECK(JsonValue::Int(1) == JsonValue::Double(1.0));
    CHECK(JsonValue::Int(1) != JsonValue::Double(1.5));
    CHECK(JsonValue::String("1") != JsonValue::Int(1));
}

TEST_CASE(Json_SetReplacesInPlaceAndPushAppends) {
    JsonValue o = JsonValue::Object();
    o.Set("a", JsonValue::Int(1));
    o.Set("b", JsonValue::Int(2));
    o.Set("a", JsonValue::Int(9));
    CHECK_EQ(o.members().size(), static_cast<size_t>(2));
    CHECK(o.members()[0].first == "a");
    CHECK_EQ(o.Find("a")->AsInt(), static_cast<int64_t>(9));
    JsonValue a = JsonValue::Array();
    a.Push(JsonValue::Int(1)).Push(JsonValue::Int(2));
    CHECK_EQ(a.items().size(), static_cast<size_t>(2));
    // wrong-type mutation is a no-op, not a crash
    a.Set("x", JsonValue::Null());
    o.Push(JsonValue::Null());
    CHECK_EQ(a.items().size(), static_cast<size_t>(2));
    CHECK_EQ(o.members().size(), static_cast<size_t>(2));
}
