#include "test_framework.h"

#include "scrapi_fixture.h"
#include "../src/scrapi/ControlModel.h"

using namespace scrapi;

namespace {
ControlModel MakeModel() { return ControlModel(scrapi_fixture::MakeManifest()); }
} // namespace

TEST_CASE(ControlModel_StartsAtDeclaredDefaultsThenTypeZeros) {
    ControlModel m = MakeModel();
    CHECK(*m.Get("mode") == JsonValue::String("auto"));
    CHECK(*m.Get("fixed.speed") == JsonValue::Double(1.0));
    CHECK(*m.Get("fixed.count") == JsonValue::Int(10));
    CHECK(*m.Get("enabled") == JsonValue::Bool(true));
    CHECK(m.Get("tags")->IsArray());              // flags -> []
    CHECK(*m.Get("title") == JsonValue::String("")); // string -> ""
    CHECK(*m.Get("tint") == JsonValue::String("#336699"));
    CHECK(m.Get("status")->IsNull());             // readout -> null until published
    CHECK(m.Get("go") == nullptr);                // buttons and groups hold no value
    CHECK(m.Get("fixed") == nullptr);
    CHECK(m.Get("nope") == nullptr);
}

TEST_CASE(ControlModel_NumbersAreClampedNotRejected) {
    ControlModel m = MakeModel();
    auto r = m.Set("fixed.speed", JsonValue::Double(99.0));
    CHECK(r.ok);
    CHECK(r.value == JsonValue::Double(10.0));
    r = m.Set("fixed.speed", JsonValue::Int(-5));
    CHECK(r.ok);
    CHECK(r.value == JsonValue::Double(0.0));
    r = m.Set("fixed.count", JsonValue::Int(1000));
    CHECK(r.ok);
    CHECK(r.value == JsonValue::Int(100));
    CHECK(*m.Get("fixed.count") == JsonValue::Int(100));
}

TEST_CASE(ControlModel_IntAcceptsIntegralDoubleButNotFractions) {
    ControlModel m = MakeModel();
    CHECK(m.Set("fixed.count", JsonValue::Double(7.0)).ok);
    const auto r = m.Set("fixed.count", JsonValue::Double(7.5));
    CHECK(!r.ok);
    CHECK(r.errorCode == err::kTypeMismatch);
}

TEST_CASE(ControlModel_TypeMismatchAndBadValuesAreRejectedWithoutChangingState) {
    ControlModel m = MakeModel();
    auto r = m.Set("enabled", JsonValue::String("yes"));
    CHECK(!r.ok);
    CHECK(r.errorCode == err::kTypeMismatch);
    CHECK(*m.Get("enabled") == JsonValue::Bool(true));

    r = m.Set("mode", JsonValue::String("turbo"));
    CHECK(!r.ok);
    CHECK(r.errorCode == err::kBadRequest);
    CHECK(*m.Get("mode") == JsonValue::String("auto"));

    r = m.Set("title", JsonValue::String("waytoolongtitle"));
    CHECK(!r.ok);
    r = m.Set("tint", JsonValue::String("blue"));
    CHECK(!r.ok);
    r = m.Set("nope", JsonValue::Int(1));
    CHECK(!r.ok);
    CHECK(r.errorCode == err::kUnknownId);
}

TEST_CASE(ControlModel_FlagsAreDedupedIntoOptionOrderAndColorsUpperCased) {
    ControlModel m = MakeModel();
    JsonValue in = JsonValue::Array();
    in.Push(JsonValue::String("c")).Push(JsonValue::String("a")).Push(JsonValue::String("c"));
    const auto r = m.Set("tags", in);
    CHECK(r.ok);
    CHECK(SerializeJson(r.value) == "[\"a\",\"c\"]");
    JsonValue bad = JsonValue::Array();
    bad.Push(JsonValue::String("z"));
    CHECK(!m.Set("tags", bad).ok);

    const auto c = m.Set("tint", JsonValue::String("#aabbcc"));
    CHECK(c.ok);
    CHECK(c.value == JsonValue::String("#AABBCC"));
}

TEST_CASE(ControlModel_ReadoutsAndButtonsAreNotClientWritable) {
    ControlModel m = MakeModel();
    auto r = m.Set("status", JsonValue::String("hax"));
    CHECK(!r.ok);
    CHECK(r.errorCode == err::kReadOnly);
    r = m.Set("go", JsonValue::Bool(true));
    CHECK(!r.ok);
    CHECK(r.errorCode == err::kUnsupported);
    // the saver itself can publish a readout
    CHECK(m.SetFromSaver("status", JsonValue::String("running")).ok);
    CHECK(*m.Get("status") == JsonValue::String("running"));
}

TEST_CASE(ControlModel_VisibilityFollowsConditionsIncludingAncestors) {
    ControlModel m = MakeModel();
    CHECK(!m.IsVisible("fixed"));        // mode == auto
    CHECK(!m.IsVisible("fixed.speed"));
    CHECK(!m.IsVisible("fixed.count"));  // no condition of its own, but its group is hidden
    CHECK(m.IsVisible("mode"));
    m.Set("mode", JsonValue::String("fixed"));
    CHECK(m.IsVisible("fixed"));
    CHECK(m.IsVisible("fixed.speed"));
    CHECK(m.IsVisible("fixed.count"));
}

TEST_CASE(ControlModel_EnabledWhenAndCompositeConditions) {
    ControlModel m = MakeModel();
    CHECK(m.IsEnabled("file"));
    m.Set("enabled", JsonValue::Bool(false));
    CHECK(!m.IsEnabled("file"));

    Condition all;
    all.kind = Condition::Kind::All;
    Condition a;
    a.kind = Condition::Kind::In;
    a.id = "mode";
    a.values = {JsonValue::String("auto"), JsonValue::String("other")};
    Condition notEnabled;
    notEnabled.kind = Condition::Kind::Not;
    Condition enabledIsTrue;
    enabledIsTrue.kind = Condition::Kind::Eq;
    enabledIsTrue.id = "enabled";
    enabledIsTrue.value = JsonValue::Bool(true);
    notEnabled.children = {enabledIsTrue};
    all.children = {a, notEnabled};
    CHECK(m.Evaluate(all));                       // mode in [auto,..] AND NOT enabled (now false)
    m.Set("enabled", JsonValue::Bool(true));
    CHECK(!m.Evaluate(all));
    CHECK(m.Evaluate(Condition{}));               // no condition = always true
}

TEST_CASE(ControlModel_ResetToDefaultRestoresInitialValue) {
    ControlModel m = MakeModel();
    m.Set("fixed.count", JsonValue::Int(50));
    m.ResetToDefault("fixed.count");
    CHECK(*m.Get("fixed.count") == JsonValue::Int(10));
}

TEST_CASE(ControlModel_MoveKeepsIndexValid) {
    ControlModel a = MakeModel();
    a.Set("fixed.count", JsonValue::Int(42));
    ControlModel b = std::move(a);
    CHECK(b.Find("fixed.count") != nullptr);
    CHECK(*b.Get("fixed.count") == JsonValue::Int(42));
    CHECK(b.Set("fixed.count", JsonValue::Int(43)).ok);
}
