#include "EffectParams.h"

#include <cstddef>

namespace core::fx {

namespace {

constexpr EffectId kForegroundCatalog[] = {
    EffectId::FlagWave,       EffectId::NorenSwing,     EffectId::InfiniteScroll,
    EffectId::InfiniteRotation, EffectId::ClothBend,    EffectId::LiquidDistort,
    EffectId::Kaleidoscope,   EffectId::SegmentWave,
    EffectId::VortexSuction,  EffectId::FragmentFlyAway, EffectId::GlassShatter,
    EffectId::ConfettiFall,   EffectId::MosaicCollapse,  EffectId::NoiseDissolve,
};

constexpr EffectId kBackgroundCatalog[] = {
    EffectId::Ripple,     EffectId::FadeOutIn,  EffectId::ZoomShake,
    EffectId::Tilt,       EffectId::LensDistort, EffectId::BackgroundKaleidoscope,
    EffectId::NoiseRipple, EffectId::HueShift,   EffectId::GlitchShift,
    EffectId::ParallaxTilt, EffectId::WaveZoom,
    EffectId::BackgroundSuction,
};

LayerEffectConfig MakeDefaultConfig(const EffectId* ids, size_t count, float intensity,
                                     float defaultMin, float defaultMax) {
    LayerEffectConfig config;
    config.defaultMinSeconds = defaultMin;
    config.defaultMaxSeconds = defaultMax;
    for (size_t i = 0; i < count; ++i) {
        EffectParams params;
        params.intensity = intensity;
        config.perEffect[ids[i]] = params;
    }
    return config;
}

} // namespace

LayerEffectConfig MakeDefaultForegroundEffectConfig() {
    return MakeDefaultConfig(kForegroundCatalog, sizeof(kForegroundCatalog) / sizeof(kForegroundCatalog[0]),
                              0.7f, 5.0f, 10.0f);
}

LayerEffectConfig MakeDefaultBackgroundEffectConfig() {
    return MakeDefaultConfig(kBackgroundCatalog, sizeof(kBackgroundCatalog) / sizeof(kBackgroundCatalog[0]),
                              0.6f, 8.0f, 15.0f);
}

EngineConfig MakeDefaultEngineConfig() {
    EngineConfig config;
    config.foreground = MakeDefaultForegroundEffectConfig();
    config.background = MakeDefaultBackgroundEffectConfig();
    return config;
}

} // namespace core::fx
