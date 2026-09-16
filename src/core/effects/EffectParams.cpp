#include "EffectParams.h"

#include "EffectCatalog.h"

namespace core::fx {

namespace {

LayerEffectConfig MakeDefaultConfig(const std::vector<EffectId>& continuous,
                                     const std::vector<EffectId>& terminal, float intensity, float defaultMin,
                                     float defaultMax) {
    LayerEffectConfig config;
    config.defaultMinSeconds = defaultMin;
    config.defaultMaxSeconds = defaultMax;
    for (EffectId id : continuous) {
        EffectParams params;
        params.intensity = intensity;
        config.perEffect[id] = params;
    }
    for (EffectId id : terminal) {
        EffectParams params;
        params.intensity = intensity;
        config.perEffect[id] = params;
    }
    return config;
}

} // namespace

LayerEffectConfig MakeDefaultForegroundEffectConfig() {
    return MakeDefaultConfig(ForegroundContinuousCatalog(), ForegroundTerminalCatalog(), 0.7f, 5.0f, 10.0f);
}

LayerEffectConfig MakeDefaultBackgroundEffectConfig() {
    return MakeDefaultConfig(BackgroundContinuousCatalog(), BackgroundTerminalCatalog(), 0.6f, 8.0f, 15.0f);
}

EngineConfig MakeDefaultEngineConfig() {
    EngineConfig config;
    config.foreground = MakeDefaultForegroundEffectConfig();
    config.background = MakeDefaultBackgroundEffectConfig();
    return config;
}

} // namespace core::fx
