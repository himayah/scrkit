#pragma once
// Configuration model + ini (de)serialization (要件.txt §6, extended per
// docs/DESIGN_EFFECTS.md §9 for the layer-separated effect system).
// Pure string <-> struct logic here; actual file I/O is a thin wrapper in
// LoadConfigFromFile/SaveConfigToFile so the parsing itself stays testable
// without touching the filesystem.

#include <string>

#include "effects/EffectParams.h"

namespace core {

enum class ParticlePreset {
    Low,     // 1000
    Mid,     // 3000
    High,    // 6000
    Max,     // 12000
    Auto,    // decided from GPU vendor/renderer/version at runtime
    Custom,  // explicit particleCount below
};

struct ConfigModel {
    ParticlePreset preset = ParticlePreset::Mid;
    int customParticleCount = 3000; // used only when preset == Custom
    std::string backgroundImageOverridePath; // empty = use system wallpaper

    // §9: layer-separated effect system config ([Effects]/[ForegroundEffects]/
    // [BackgroundEffects]). Defaulted via the free function (not in-class
    // field initializers) so a bare ConfigModel{} already has every catalog
    // effect populated and the correct per-layer default durations/intensity,
    // not just whatever ParseConfigIni happens to produce.
    core::fx::EngineConfig effects = core::fx::MakeDefaultEngineConfig();

    static int ParticleCountForPreset(ParticlePreset preset) {
        switch (preset) {
            case ParticlePreset::Low: return 1000;
            case ParticlePreset::Mid: return 3000;
            case ParticlePreset::High: return 6000;
            case ParticlePreset::Max: return 12000;
            default: return 3000;
        }
    }

    // Resolves the preset (and Auto/Custom special cases) into a concrete
    // particle count. `autoResolvedCount` is supplied by the caller after
    // running GpuTierClassifier against a real GL context.
    int ResolveParticleCount(int autoResolvedCount) const {
        switch (preset) {
            case ParticlePreset::Custom: return customParticleCount;
            case ParticlePreset::Auto: return autoResolvedCount;
            default: return ParticleCountForPreset(preset);
        }
    }
};

// Serializes a ConfigModel to ini text.
std::string SerializeConfigIni(const ConfigModel& config);

// Parses ini text into a ConfigModel. Any missing or malformed field falls
// back to the ConfigModel default for that field rather than failing, so a
// corrupt config.ini can never crash the saver (要件.txt: エラーハンドリング方針).
ConfigModel ParseConfigIni(const std::string& iniText);

// Thin file I/O wrappers (still portable std::fstream, no Win32 dependency).
// Returns false (and leaves `out` untouched) if the file could not be read.
bool LoadConfigFromFile(const std::string& path, ConfigModel& out);
bool SaveConfigToFile(const std::string& path, const ConfigModel& config);

} // namespace core
