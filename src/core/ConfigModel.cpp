#include "ConfigModel.h"

#include <algorithm>
#include <fstream>
#include <sstream>

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

} // namespace

std::string SerializeConfigIni(const ConfigModel& config) {
    std::ostringstream out;
    out << "[SpiralSuctionSaver]\n";
    out << "Preset=" << PresetToString(config.preset) << "\n";
    out << "CustomParticleCount=" << config.customParticleCount << "\n";
    out << "BackgroundImageOverride=" << config.backgroundImageOverridePath << "\n";
    return out.str();
}

ConfigModel ParseConfigIni(const std::string& iniText) {
    ConfigModel config; // defaults act as the fallback for any missing/bad field

    std::istringstream stream(iniText);
    std::string line;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line[0] == ';' || line[0] == '#' || line[0] == '[') {
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = ToLower(Trim(line.substr(0, eq)));
        const std::string value = Trim(line.substr(eq + 1));

        if (key == "preset") {
            config.preset = PresetFromString(value, config.preset);
        } else if (key == "customparticlecount") {
            try {
                int n = std::stoi(value);
                if (n > 0) {
                    config.customParticleCount = n;
                }
            } catch (...) {
                // keep default on malformed integer
            }
        } else if (key == "backgroundimageoverride") {
            config.backgroundImageOverridePath = value;
        }
        // unknown keys are ignored rather than treated as an error
    }

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
