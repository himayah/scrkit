#include "GpuTierClassifier.h"

#include <algorithm>
#include <cctype>

namespace core {

namespace {

std::string ToLowerCopy(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

bool Contains(const std::string& haystackLower, const char* needle) {
    return haystackLower.find(needle) != std::string::npos;
}

int ParseGlMajorVersion(const std::string& version) {
    // GL_VERSION strings look like "4.6.0 NVIDIA 555.42" or
    // "OpenGL ES 3.2 ..." -- find the first digit and read the major number.
    for (size_t i = 0; i < version.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(version[i]))) {
            try {
                return std::stoi(version.substr(i));
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

} // namespace

int ClassifyGpuParticleCount(const std::string& vendor, const std::string& renderer,
                              const std::string& version) {
    const std::string v = ToLowerCopy(vendor);
    const std::string r = ToLowerCopy(renderer);

    const bool isSoftwareRenderer =
        Contains(r, "llvmpipe") || Contains(r, "software") ||
        Contains(r, "swiftshader") || Contains(r, "basic render") ||
        Contains(r, "microsoft basic");

    if (isSoftwareRenderer) {
        return ConfigModel::ParticleCountForPreset(ParticlePreset::Low);
    }

    const int glMajor = ParseGlMajorVersion(version);
    if (glMajor > 0 && glMajor < 2) {
        return ConfigModel::ParticleCountForPreset(ParticlePreset::Low);
    }

    const bool looksHighEnd =
        Contains(r, "rtx") || Contains(r, "quadro") || Contains(r, "radeon pro") ||
        Contains(r, "titan") || Contains(r, "arc a7");

    const bool isDiscreteVendor =
        Contains(v, "nvidia") || Contains(v, "amd") || Contains(v, "ati");

    if (looksHighEnd) {
        return ConfigModel::ParticleCountForPreset(ParticlePreset::Max);
    }
    if (isDiscreteVendor) {
        return ConfigModel::ParticleCountForPreset(ParticlePreset::High);
    }
    if (Contains(v, "intel")) {
        return ConfigModel::ParticleCountForPreset(ParticlePreset::Mid);
    }

    // Unknown vendor/renderer: assume a reasonably capable GPU.
    return ConfigModel::ParticleCountForPreset(ParticlePreset::Mid);
}

} // namespace core
