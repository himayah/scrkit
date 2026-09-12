#include "test_framework.h"

#include "../src/core/ConfigModel.h"

using core::ConfigModel;
using core::ParseConfigIni;
using core::ParticlePreset;
using core::SerializeConfigIni;

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
