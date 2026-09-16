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

const EffectParams& ParamsFor(const LayerEffectConfig& config, EffectId id) {
    static const EffectParams kDefault;
    auto it = config.perEffect.find(id);
    return it != config.perEffect.end() ? it->second : kDefault;
}

} // namespace core::fx
