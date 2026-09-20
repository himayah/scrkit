#include "test_framework.h"

#include "scrapi_fixture.h"
#include "../src/scrapi/ControlModel.h"
#include "../src/scrapi/Manifest.h"

using namespace scrapi;

TEST_CASE(Manifest_FixtureIsValid) {
    std::string error;
    CHECK(ValidateManifest(scrapi_fixture::MakeManifest(), &error));
    if (!error.empty()) std::fprintf(stderr, "%s\n", error.c_str());
}

TEST_CASE(Manifest_JsonRoundTripPreservesEverything) {
    const Manifest original = scrapi_fixture::MakeManifest();
    const JsonValue json = ManifestToJson(original);
    Manifest back;
    std::string error;
    CHECK(ManifestFromJson(json, back, &error));
    CHECK(ManifestToJson(back) == json);
    CHECK(back.saver.name == "Demo Saver");
    CHECK(back.HasCapability("viewport.resize"));
    CHECK(!back.HasCapability("nope"));
    const ControlNode* speed = FindControl(back, "fixed.speed");
    CHECK(speed != nullptr);
    if (speed) {
        CHECK(speed->type == ControlType::Float);
        CHECK(speed->presentation == "knob");
        CHECK(speed->visibleWhen.kind == Condition::Kind::Eq);
    }
}

TEST_CASE(Manifest_SurvivesTextSerializationToo) {
    const std::string text = SerializeJson(ManifestToJson(scrapi_fixture::MakeManifest()));
    CHECK(text.find('\n') == std::string::npos); // safe as one JSON Lines message
    JsonValue json;
    CHECK(ParseJson(text, json));
    Manifest back;
    CHECK(ManifestFromJson(json, back));
    CHECK(ValidateManifest(back));
}

TEST_CASE(Manifest_UnknownTypesAndPropertiesAreToleratedForForwardCompat) {
    JsonValue json;
    CHECK(ParseJson(
        "{\"scrapi\":\"1.3\",\"futureTopLevel\":1,\"saver\":{\"id\":\"x\",\"name\":\"X\",\"version\":\"1\"},"
        "\"controls\":[{\"id\":\"v\",\"type\":\"vec2\",\"newProp\":true,\"default\":[1,2]}]}",
        json));
    Manifest m;
    std::string error;
    CHECK(ManifestFromJson(json, m, &error));
    CHECK_EQ(m.controls.size(), static_cast<size_t>(1));
    CHECK(m.controls[0].type == ControlType::Unknown);
    CHECK(m.controls[0].typeName == "vec2");
    // and a model can still hold its raw value
    ControlModel model(std::move(m));
    CHECK(model.Get("v") != nullptr);
    CHECK(ManifestToJson(model.manifest()).Find("controls")->items()[0].Find("type")->AsString() == "vec2");
}

TEST_CASE(Manifest_RejectsStructurallyBrokenInput) {
    Manifest m;
    JsonValue json;
    CHECK(ParseJson("[]", json));
    CHECK(!ManifestFromJson(json, m));
    CHECK(ParseJson("{\"controls\":[]}", json));            // no version
    CHECK(!ManifestFromJson(json, m));
    CHECK(ParseJson("{\"scrapi\":\"1.0\"}", json));         // no controls
    CHECK(!ManifestFromJson(json, m));
    CHECK(ParseJson("{\"scrapi\":\"1.0\",\"controls\":[{\"type\":\"bool\"}]}", json)); // node without id
    CHECK(!ManifestFromJson(json, m));
    CHECK(ParseJson("{\"scrapi\":\"1.0\",\"controls\":[{\"id\":\"a\"}]}", json));      // node without type
    CHECK(!ManifestFromJson(json, m));
}

TEST_CASE(Manifest_ValidationCatchesAuthorMistakes) {
    auto broken = [](auto mutate) {
        Manifest m = scrapi_fixture::MakeManifest();
        mutate(m);
        std::string error;
        const bool ok = ValidateManifest(m, &error);
        CHECK(!ok);
        CHECK(!error.empty());
    };
    broken([](Manifest& m) { m.controls.push_back(m.controls[0]); });                       // duplicate id
    broken([](Manifest& m) { m.controls[0].id = "bad id!"; });                               // illegal id
    broken([](Manifest& m) { m.controls[0].options.clear(); });                              // enum without options
    broken([](Manifest& m) { m.controls[0].options.push_back(m.controls[0].options[0]); });  // duplicate option
    broken([](Manifest& m) { m.controls[0].defaultValue = JsonValue::String("nope"); });     // default not an option
    broken([](Manifest& m) { m.controls[1].children[1].defaultValue = JsonValue::Int(500); });// default out of range
    broken([](Manifest& m) { m.controls[1].children[1].min = 1000; });                       // min > max
    broken([](Manifest& m) { m.controls[1].visibleWhen.id = "ghost"; });                     // dangling condition
    broken([](Manifest& m) { m.controls[5].defaultValue = JsonValue::String("#abcdef"); });  // color not canonical (lower-case)
}

TEST_CASE(Manifest_IdCharset) {
    CHECK(IsValidControlId("fx.FlagWave.intensity"));
    CHECK(IsValidControlId("scrapi.paused"));
    CHECK(IsValidControlId("a-b_c.9"));
    CHECK(!IsValidControlId(""));
    CHECK(!IsValidControlId("a b"));
    CHECK(!IsValidControlId("a/b"));
    CHECK(!IsValidControlId("日本"));
}

TEST_CASE(Manifest_ForEachControlVisitsGroupsAndChildrenDepthFirst) {
    const Manifest m = scrapi_fixture::MakeManifest();
    std::vector<std::string> order;
    ForEachControl(m.controls, [&](const ControlNode& n) { order.push_back(n.id); });
    CHECK(order[0] == "mode");
    CHECK(order[1] == "fixed");
    CHECK(order[2] == "fixed.speed");
    CHECK(order[3] == "fixed.count");
    CHECK_EQ(order.size(), static_cast<size_t>(11));
}
