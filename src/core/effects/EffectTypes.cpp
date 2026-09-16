#include "EffectTypes.h"

#include <algorithm>
#include <array>
#include <cctype>

namespace core::fx {

namespace {

struct NameEntry {
    EffectId id;
    const char* name;
};

// Single source of truth for the name <-> EffectId mapping (§9.2's
// `EffectIdToString` reference). Keep in sync with the EffectId enum.
constexpr std::array<NameEntry, kEffectIdCount> kNames{{
    {EffectId::FlagWave, "FlagWave"},
    {EffectId::NorenSwing, "NorenSwing"},
    {EffectId::InfiniteScroll, "InfiniteScroll"},
    {EffectId::InfiniteRotation, "InfiniteRotation"},
    {EffectId::ClothBend, "ClothBend"},
    {EffectId::LiquidDistort, "LiquidDistort"},
    {EffectId::Kaleidoscope, "Kaleidoscope"},
    {EffectId::SegmentWave, "SegmentWave"},
    {EffectId::VortexSuction, "VortexSuction"},
    {EffectId::FragmentFlyAway, "FragmentFlyAway"},
    {EffectId::GlassShatter, "GlassShatter"},
    {EffectId::ConfettiFall, "ConfettiFall"},
    {EffectId::MosaicCollapse, "MosaicCollapse"},
    {EffectId::NoiseDissolve, "NoiseDissolve"},
    {EffectId::Ripple, "Ripple"},
    {EffectId::FadeOutIn, "FadeOutIn"},
    {EffectId::ZoomShake, "ZoomShake"},
    {EffectId::Tilt, "Tilt"},
    {EffectId::LensDistort, "LensDistort"},
    {EffectId::BackgroundKaleidoscope, "BackgroundKaleidoscope"},
    {EffectId::NoiseRipple, "NoiseRipple"},
    {EffectId::HueShift, "HueShift"},
    {EffectId::GlitchShift, "GlitchShift"},
    {EffectId::ParallaxTilt, "ParallaxTilt"},
    {EffectId::WaveZoom, "WaveZoom"},
    {EffectId::BackgroundSuction, "BackgroundSuction"},
}};

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

} // namespace

const char* EffectIdToString(EffectId id) {
    for (const auto& entry : kNames) {
        if (entry.id == id) return entry.name;
    }
    return "";
}

bool EffectIdFromString(const std::string& name, EffectId& out) {
    const std::string lower = ToLower(name);
    for (const auto& entry : kNames) {
        if (ToLower(entry.name) == lower) {
            out = entry.id;
            return true;
        }
    }
    return false;
}

} // namespace core::fx
