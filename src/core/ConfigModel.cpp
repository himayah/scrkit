#include "ConfigModel.h"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "Logger.h"
#include "effects/EffectCatalog.h"

namespace core {

namespace {

std::string Trim(const std::string& s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

const char* PresetToString(ParticlePreset preset) {
    switch (preset) {
        case ParticlePreset::Low: return "Low";
        case ParticlePreset::Mid: return "Mid";
        case ParticlePreset::High: return "High";
        case ParticlePreset::Max: return "Max";
        case ParticlePreset::Auto: return "Auto";
        case ParticlePreset::Custom: return "Custom";
    }
    return "Mid";
}

ParticlePreset PresetFromString(const std::string& raw, ParticlePreset fallback) {
    const std::string s = ToLower(Trim(raw));
    if (s == "low") return ParticlePreset::Low;
    if (s == "mid" || s == "medium") return ParticlePreset::Mid;
    if (s == "high") return ParticlePreset::High;
    if (s == "max" || s == "highest") return ParticlePreset::Max;
    if (s == "auto") return ParticlePreset::Auto;
    if (s == "custom") return ParticlePreset::Custom;
    return fallback;
}

// ---- §9.2 effects-section value parsing ---------------------------------

bool TryParseFloat(const std::string& value, float& out) {
    try {
        size_t idx = 0;
        out = std::stof(value, &idx);
        return true;
    } catch (...) {
        return false;
    }
}

float ClampedFloat(const std::string& value, float lo, float hi, float fallback) {
    float v = 0.0f;
    if (!TryParseFloat(value, v)) return fallback;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 0 is a valid sentinel ("use the layer default", §5.5); any other value
// clamps into [lo,hi] same as ClampedFloat. An unparsable value also means
// "use the layer default" (0), not a fixed fallback constant.
float ZeroOrClampedFloat(const std::string& value, float lo, float hi) {
    float v = 0.0f;
    if (!TryParseFloat(value, v)) return 0.0f;
    if (v == 0.0f) return 0.0f;
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

bool ParseBool01(const std::string& value, bool fallback) {
    const std::string v = Trim(value);
    if (v == "1") return true;
    if (v == "0") return false;
    return fallback;
}

float ParseWeight(const std::string& value, float fallback) {
    float v = 0.0f;
    if (!TryParseFloat(value, v) || v <= 0.0f) return fallback;
    return v;
}

bool CatalogContains(const std::vector<fx::EffectId>& catalog, fx::EffectId id) {
    return std::find(catalog.begin(), catalog.end(), id) != catalog.end();
}

std::vector<fx::EffectId> ParseSequence(const std::string& value) {
    std::vector<fx::EffectId> result;
    std::istringstream ss(value);
    std::string token;
    while (std::getline(ss, token, ',')) {
        const std::string name = Trim(token);
        if (name.empty()) continue;
        fx::EffectId id;
        if (fx::EffectIdFromString(name, id)) {
            result.push_back(id);
        } else {
            Logger::Warn("config.ini: unknown effect name in sequence: " + name);
        }
    }
    return result;
}

std::string SerializeSequence(const std::vector<fx::EffectId>& sequence) {
    std::string out;
    for (size_t i = 0; i < sequence.size(); ++i) {
        if (i > 0) out += ",";
        out += fx::EffectIdToString(sequence[i]);
    }
    return out;
}

void SerializeLayerEffects(std::ostringstream& out, const fx::LayerEffectConfig& layer) {
    for (const auto& [id, params] : layer.perEffect) {
        const std::string name = fx::EffectIdToString(id);
        out << name << ".Enabled=" << (params.enabled ? 1 : 0) << "\n";
        out << name << ".Intensity=" << params.intensity << "\n";
        out << name << ".Weight=" << params.weight << "\n";
        out << name << ".MinSeconds=" << params.minSeconds << "\n";
        out << name << ".MaxSeconds=" << params.maxSeconds << "\n";
    }
}

enum class Section { Other, Effects, ForegroundEffects, BackgroundEffects };

Section SectionFromHeaderLine(const std::string& line) {
    const auto close = line.find(']');
    const std::string name = ToLower(Trim(line.substr(1, close == std::string::npos ? std::string::npos : close - 1)));
    if (name == "effects") return Section::Effects;
    if (name == "foregroundeffects") return Section::ForegroundEffects;
    if (name == "backgroundeffects") return Section::BackgroundEffects;
    return Section::Other; // includes [SpiralSuctionSaver] and anything unknown
}

} // namespace

std::string SerializeConfigIni(const ConfigModel& config) {
    std::ostringstream out;
    out << "[SpiralSuctionSaver]\n";
    out << "Preset=" << PresetToString(config.preset) << "\n";
    out << "CustomParticleCount=" << config.customParticleCount << "\n";
    out << "BackgroundImageOverride=" << config.backgroundImageOverridePath << "\n";

    out << "\n[Effects]\n";
    out << "Enabled=" << (config.effects.enabled ? 1 : 0) << "\n";
    out << "TransitionSeconds=" << config.effects.transitionSeconds << "\n";
    out << "ForegroundShowcaseSeconds=" << config.effects.foregroundShowcaseSeconds << "\n";
    out << "ForegroundMinSeconds=" << config.effects.foreground.defaultMinSeconds << "\n";
    out << "ForegroundMaxSeconds=" << config.effects.foreground.defaultMaxSeconds << "\n";
    out << "BackgroundMinSeconds=" << config.effects.background.defaultMinSeconds << "\n";
    out << "BackgroundMaxSeconds=" << config.effects.background.defaultMaxSeconds << "\n";
    out << "TerminalMaxSeconds=" << config.effects.terminalMaxSeconds << "\n";
    out << "; comma-separated EffectName list; empty = random\n";
    out << "ForegroundSequence=" << SerializeSequence(config.effects.scriptedForeground) << "\n";
    out << "BackgroundSequence=" << SerializeSequence(config.effects.scriptedBackground) << "\n";

    out << "\n[ForegroundEffects]\n";
    out << "; <EffectName>.Enabled=0|1  .Intensity=0..1  .Weight=>0  .MinSeconds/.MaxSeconds (0 = layer default)\n";
    SerializeLayerEffects(out, config.effects.foreground);

    out << "\n[BackgroundEffects]\n";
    SerializeLayerEffects(out, config.effects.background);

    return out.str();
}

ConfigModel ParseConfigIni(const std::string& iniText) {
    ConfigModel config; // defaults (incl. effects) act as the fallback for any missing/bad field

    // The two layer-default duration pairs need both keys seen before the
    // "Min > Max -> both revert to the factory default" rule (§9.2) can be
    // applied, so they're accumulated locally and only written back at the end.
    float fgMin = config.effects.foreground.defaultMinSeconds;
    float fgMax = config.effects.foreground.defaultMaxSeconds;
    float bgMin = config.effects.background.defaultMinSeconds;
    float bgMax = config.effects.background.defaultMaxSeconds;

    Section section = Section::Other;

    std::istringstream stream(iniText);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            section = SectionFromHeaderLine(line);
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string rawKey = Trim(line.substr(0, eq));
        const std::string key = ToLower(rawKey);
        const std::string value = Trim(line.substr(eq + 1));

        // Existing 3 keys: accepted regardless of section, for back-compat
        // with hand-edited files (§9.3, A.6's pre-existing parser behavior).
        if (key == "preset") {
            config.preset = PresetFromString(value, config.preset);
            continue;
        }
        if (key == "customparticlecount") {
            try {
                int n = std::stoi(value);
                if (n > 0) config.customParticleCount = n;
            } catch (...) {
            }
            continue;
        }
        if (key == "backgroundimageoverride") {
            config.backgroundImageOverridePath = value;
            continue;
        }

        if (section == Section::Effects) {
            if (key == "enabled") {
                config.effects.enabled = ParseBool01(value, config.effects.enabled);
            } else if (key == "transitionseconds") {
                config.effects.transitionSeconds = ClampedFloat(value, 0.1f, 3.0f, 0.5f);
            } else if (key == "foregroundshowcaseseconds") {
                config.effects.foregroundShowcaseSeconds = ClampedFloat(value, 0.0f, 600.0f, 40.0f);
            } else if (key == "terminalmaxseconds") {
                config.effects.terminalMaxSeconds = ClampedFloat(value, 5.0f, 120.0f, 20.0f);
            } else if (key == "foregroundminseconds") {
                fgMin = ClampedFloat(value, 1.0f, 120.0f, fgMin);
            } else if (key == "foregroundmaxseconds") {
                fgMax = ClampedFloat(value, 1.0f, 120.0f, fgMax);
            } else if (key == "backgroundminseconds") {
                bgMin = ClampedFloat(value, 1.0f, 120.0f, bgMin);
            } else if (key == "backgroundmaxseconds") {
                bgMax = ClampedFloat(value, 1.0f, 120.0f, bgMax);
            } else if (key == "foregroundsequence") {
                config.effects.scriptedForeground = ParseSequence(value);
            } else if (key == "backgroundsequence") {
                config.effects.scriptedBackground = ParseSequence(value);
            } else {
                Logger::Warn("config.ini: unknown [Effects] key: " + rawKey);
            }
        } else if (section == Section::ForegroundEffects || section == Section::BackgroundEffects) {
            const auto dot = rawKey.find_last_of('.');
            if (dot == std::string::npos) {
                Logger::Warn("config.ini: malformed effect key (missing '.'): " + rawKey);
                continue;
            }
            const std::string effectName = rawKey.substr(0, dot);
            const std::string field = ToLower(rawKey.substr(dot + 1));

            fx::EffectId id;
            if (!fx::EffectIdFromString(effectName, id)) {
                Logger::Warn("config.ini: unknown effect name: " + effectName);
                continue;
            }
            const bool isForegroundSection = section == Section::ForegroundEffects;
            const bool belongsToThisLayer =
                isForegroundSection
                    ? (CatalogContains(fx::ForegroundContinuousCatalog(), id) ||
                       CatalogContains(fx::ForegroundTerminalCatalog(), id))
                    : (CatalogContains(fx::BackgroundContinuousCatalog(), id) ||
                       CatalogContains(fx::BackgroundTerminalCatalog(), id));
            if (!belongsToThisLayer) {
                Logger::Warn("config.ini: effect name in wrong section: " + effectName);
                continue;
            }

            fx::LayerEffectConfig& layerConfig = isForegroundSection ? config.effects.foreground : config.effects.background;
            fx::EffectParams& params = layerConfig.perEffect[id]; // MakeDefaultEngineConfig already populated this

            if (field == "enabled") {
                params.enabled = ParseBool01(value, params.enabled);
            } else if (field == "intensity") {
                params.intensity = ClampedFloat(value, 0.0f, 1.0f, params.intensity);
            } else if (field == "weight") {
                params.weight = ParseWeight(value, params.weight);
            } else if (field == "minseconds") {
                params.minSeconds = ZeroOrClampedFloat(value, 1.0f, 120.0f);
            } else if (field == "maxseconds") {
                params.maxSeconds = ZeroOrClampedFloat(value, 1.0f, 120.0f);
            } else {
                Logger::Warn("config.ini: unknown field '" + field + "' for effect " + effectName);
            }
        }
        // Section::Other (includes [SpiralSuctionSaver]): only the 3
        // back-compat keys above apply; anything else here is ignored.
    }

    // §9.2: Min > Max reverts the *pair* to the factory default, not a clamp
    // or swap (applies whether or not both keys were actually present in the
    // file -- an unset one already sits at its factory default value).
    if (fgMin > fgMax) {
        fgMin = 5.0f;
        fgMax = 10.0f;
    }
    if (bgMin > bgMax) {
        bgMin = 8.0f;
        bgMax = 15.0f;
    }
    config.effects.foreground.defaultMinSeconds = fgMin;
    config.effects.foreground.defaultMaxSeconds = fgMax;
    config.effects.background.defaultMinSeconds = bgMin;
    config.effects.background.defaultMaxSeconds = bgMax;

    return config;
}

bool LoadConfigFromFile(const std::string& path, ConfigModel& out) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    out = ParseConfigIni(ss.str());
    return true;
}

bool SaveConfigToFile(const std::string& path, const ConfigModel& config) {
    std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!file) {
        return false;
    }
    file << SerializeConfigIni(config);
    return static_cast<bool>(file);
}

} // namespace core
