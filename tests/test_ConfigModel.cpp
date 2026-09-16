#include "test_framework.h"

#include "../src/core/ConfigModel.h"
#include "../src/core/effects/EffectCatalog.h"

using core::ConfigModel;
using core::ParseConfigIni;
using core::ParticlePreset;
using core::SerializeConfigIni;
using core::fx::EffectId;

TEST_CASE(ConfigModel_PresetParticleCounts) {
    CHECK_EQ(ConfigModel::ParticleCountForPreset(ParticlePreset::Low), 1000);
    CHECK_EQ(ConfigModel::ParticleCountForPreset(ParticlePreset::Mid), 3000);
    CHECK_EQ(ConfigModel::ParticleCountForPreset(ParticlePreset::High), 6000);
    CHECK_EQ(ConfigModel::ParticleCountForPreset(ParticlePreset::Max), 12000);
}

TEST_CASE(ConfigModel_ResolveParticleCountHandlesCustomAndAuto) {
    ConfigModel config;
    config.preset = ParticlePreset::Custom;
    config.customParticleCount = 4242;
    CHECK_EQ(config.ResolveParticleCount(9999), 4242);

    config.preset = ParticlePreset::Auto;
    CHECK_EQ(config.ResolveParticleCount(7777), 7777);

    config.preset = ParticlePreset::High;
    CHECK_EQ(config.ResolveParticleCount(1), 6000);
}

TEST_CASE(ConfigModel_SerializeThenParseRoundTrips) {
    ConfigModel original;
    original.preset = ParticlePreset::Custom;
    original.customParticleCount = 8500;
    original.backgroundImageOverridePath = "C:/Users/me/Pictures/wall.jpg";

    const std::string ini = SerializeConfigIni(original);
    const ConfigModel parsed = ParseConfigIni(ini);

    CHECK(parsed.preset == ParticlePreset::Custom);
    CHECK_EQ(parsed.customParticleCount, 8500);
    CHECK_EQ(parsed.backgroundImageOverridePath, std::string("C:/Users/me/Pictures/wall.jpg"));
}

TEST_CASE(ConfigModel_MalformedIniFallsBackToDefaults) {
    const std::string garbage = "this is not an ini file\n=== ??? ###\nPreset=NotARealPreset\nCustomParticleCount=notanumber\n";
    const ConfigModel parsed = ParseConfigIni(garbage);
    ConfigModel defaults;
    CHECK(parsed.preset == defaults.preset);
    CHECK_EQ(parsed.customParticleCount, defaults.customParticleCount);
    CHECK_EQ(parsed.backgroundImageOverridePath, defaults.backgroundImageOverridePath);
}

TEST_CASE(ConfigModel_EmptyIniYieldsDefaults) {
    const ConfigModel parsed = ParseConfigIni("");
    ConfigModel defaults;
    CHECK(parsed.preset == defaults.preset);
    CHECK_EQ(parsed.customParticleCount, defaults.customParticleCount);
}

TEST_CASE(ConfigModel_LoadSaveFileRoundTrips) {
    const std::string path = "/tmp/spiral_suction_saver_test_config.ini";
    ConfigModel original;
    original.preset = ParticlePreset::Low;
    original.backgroundImageOverridePath = "/tmp/some_image.png";
    CHECK(core::SaveConfigToFile(path, original));

    ConfigModel loaded;
    CHECK(core::LoadConfigFromFile(path, loaded));
    CHECK(loaded.preset == ParticlePreset::Low);
    CHECK_EQ(loaded.backgroundImageOverridePath, std::string("/tmp/some_image.png"));
}

TEST_CASE(ConfigModel_LoadMissingFileReturnsFalse) {
    ConfigModel loaded;
    CHECK(!core::LoadConfigFromFile("/tmp/this_file_should_not_exist_ssaver.ini", loaded));
}

// ---- §9 effects config extension ----------------------------------------

TEST_CASE(ConfigModel_DefaultEffectsConfigHasEveryCatalogEffectEnabled) {
    ConfigModel config;
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) {
        CHECK(config.effects.foreground.perEffect.count(id) == 1);
        CHECK(config.effects.foreground.perEffect.at(id).enabled);
    }
    for (EffectId id : core::fx::BackgroundContinuousCatalog()) {
        CHECK(config.effects.background.perEffect.count(id) == 1);
        CHECK(config.effects.background.perEffect.at(id).enabled);
    }
    CHECK_NEAR(config.effects.foreground.defaultMinSeconds, 5.0f, 1e-6f);
    CHECK_NEAR(config.effects.foreground.defaultMaxSeconds, 10.0f, 1e-6f);
    CHECK_NEAR(config.effects.background.defaultMinSeconds, 8.0f, 1e-6f);
    CHECK_NEAR(config.effects.background.defaultMaxSeconds, 15.0f, 1e-6f);
}

TEST_CASE(ConfigModel_FourSectionRoundTrip) {
    ConfigModel original;
    original.effects.enabled = false;
    original.effects.transitionSeconds = 0.8f;
    original.effects.foregroundShowcaseSeconds = 33.0f;
    original.effects.terminalMaxSeconds = 15.0f;
    original.effects.foreground.defaultMinSeconds = 6.0f;
    original.effects.foreground.defaultMaxSeconds = 9.0f;
    original.effects.background.defaultMinSeconds = 11.0f;
    original.effects.background.defaultMaxSeconds = 14.0f;
    original.effects.scriptedForeground = {EffectId::FlagWave, EffectId::GlassShatter};
    original.effects.foreground.perEffect[EffectId::FlagWave].enabled = false;
    original.effects.foreground.perEffect[EffectId::FlagWave].intensity = 0.3f;
    original.effects.foreground.perEffect[EffectId::FlagWave].weight = 2.5f;
    original.effects.foreground.perEffect[EffectId::FlagWave].minSeconds = 4.0f;
    original.effects.foreground.perEffect[EffectId::FlagWave].maxSeconds = 6.0f;
    original.effects.background.perEffect[EffectId::Ripple].intensity = 0.9f;

    const ConfigModel parsed = ParseConfigIni(SerializeConfigIni(original));

    CHECK(!parsed.effects.enabled);
    CHECK_NEAR(parsed.effects.transitionSeconds, 0.8f, 1e-4f);
    CHECK_NEAR(parsed.effects.foregroundShowcaseSeconds, 33.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.terminalMaxSeconds, 15.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.foreground.defaultMinSeconds, 6.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.foreground.defaultMaxSeconds, 9.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.background.defaultMinSeconds, 11.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.background.defaultMaxSeconds, 14.0f, 1e-4f);
    CHECK_EQ(parsed.effects.scriptedForeground.size(), static_cast<size_t>(2));
    CHECK(parsed.effects.scriptedForeground[0] == EffectId::FlagWave);
    CHECK(parsed.effects.scriptedForeground[1] == EffectId::GlassShatter);
    CHECK(!parsed.effects.foreground.perEffect.at(EffectId::FlagWave).enabled);
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::FlagWave).intensity, 0.3f, 1e-4f);
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::FlagWave).weight, 2.5f, 1e-4f);
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::FlagWave).minSeconds, 4.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::FlagWave).maxSeconds, 6.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.background.perEffect.at(EffectId::Ripple).intensity, 0.9f, 1e-4f);
}

TEST_CASE(ConfigModel_ExistingThreeKeysAcceptedOutsideTheirSection) {
    // §9.3: the pre-existing 3 keys are accepted anywhere, e.g. inside
    // [Effects] by mistake, matching the old parser's "no section tracking"
    // behavior for hand-edited files.
    const std::string ini =
        "[Effects]\n"
        "Preset=High\n"
        "Enabled=0\n";
    const ConfigModel parsed = ParseConfigIni(ini);
    CHECK(parsed.preset == ParticlePreset::High);
    CHECK(!parsed.effects.enabled);
}

TEST_CASE(ConfigModel_EffectsKeysClampOutOfRangeValues) {
    const std::string ini =
        "[Effects]\n"
        "TransitionSeconds=99\n"
        "ForegroundShowcaseSeconds=-5\n"
        "TerminalMaxSeconds=0\n";
    const ConfigModel parsed = ParseConfigIni(ini);
    CHECK_NEAR(parsed.effects.transitionSeconds, 3.0f, 1e-4f);       // clamped to max 3.0
    CHECK_NEAR(parsed.effects.foregroundShowcaseSeconds, 0.0f, 1e-4f); // clamped to min 0
    CHECK_NEAR(parsed.effects.terminalMaxSeconds, 5.0f, 1e-4f);      // clamped to min 5
}

TEST_CASE(ConfigModel_LayerDefaultMinGreaterThanMaxRevertsBothToFactoryDefault) {
    const std::string ini =
        "[Effects]\n"
        "ForegroundMinSeconds=9\n"
        "ForegroundMaxSeconds=3\n"
        "BackgroundMinSeconds=14\n"
        "BackgroundMaxSeconds=2\n";
    const ConfigModel parsed = ParseConfigIni(ini);
    CHECK_NEAR(parsed.effects.foreground.defaultMinSeconds, 5.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.foreground.defaultMaxSeconds, 10.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.background.defaultMinSeconds, 8.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.background.defaultMaxSeconds, 15.0f, 1e-4f);
}

TEST_CASE(ConfigModel_PerEffectZeroOrEitherBoundHandling) {
    const std::string ini =
        "[ForegroundEffects]\n"
        "FlagWave.MinSeconds=0\n"
        "FlagWave.MaxSeconds=7\n"
        "NorenSwing.MinSeconds=200\n"
        "NorenSwing.MaxSeconds=abc\n";
    const ConfigModel parsed = ParseConfigIni(ini);
    // Either bound literally 0 is kept as 0 (the "use layer default" sentinel)
    // -- clamping/fallback happens later at Pick() time, not at parse time.
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::FlagWave).minSeconds, 0.0f, 1e-6f);
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::FlagWave).maxSeconds, 7.0f, 1e-4f);
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::NorenSwing).minSeconds, 120.0f, 1e-4f); // clamped
    CHECK_NEAR(parsed.effects.foreground.perEffect.at(EffectId::NorenSwing).maxSeconds, 0.0f, 1e-6f); // unparsable -> 0
}

TEST_CASE(ConfigModel_UnknownEffectNameAndWrongSectionAreIgnored) {
    const std::string ini =
        "[ForegroundEffects]\n"
        "NotARealEffect.Enabled=0\n"
        "Ripple.Enabled=0\n"; // Ripple belongs to BackgroundEffects, not this section
    const ConfigModel parsed = ParseConfigIni(ini);
    ConfigModel defaults;
    // Neither line should have changed anything from the defaults.
    CHECK(parsed.effects.background.perEffect.at(EffectId::Ripple).enabled ==
          defaults.effects.background.perEffect.at(EffectId::Ripple).enabled);
    for (EffectId id : core::fx::ForegroundContinuousCatalog()) {
        CHECK(parsed.effects.foreground.perEffect.at(id).enabled ==
              defaults.effects.foreground.perEffect.at(id).enabled);
    }
}

TEST_CASE(ConfigModel_SequenceParsesKnownNamesAndDropsUnknown) {
    const std::string ini = "[Effects]\nForegroundSequence=FlagWave, NotReal, GlassShatter\n";
    const ConfigModel parsed = ParseConfigIni(ini);
    CHECK_EQ(parsed.effects.scriptedForeground.size(), static_cast<size_t>(2));
    CHECK(parsed.effects.scriptedForeground[0] == EffectId::FlagWave);
    CHECK(parsed.effects.scriptedForeground[1] == EffectId::GlassShatter);
}
