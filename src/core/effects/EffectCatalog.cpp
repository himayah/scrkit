#include "EffectCatalog.h"

namespace core::fx {

const std::vector<EffectId>& ForegroundContinuousCatalog() {
    static const std::vector<EffectId> catalog = {
        EffectId::FlagWave,   EffectId::NorenSwing, EffectId::InfiniteScroll, EffectId::InfiniteRotation,
        EffectId::ClothBend,  EffectId::LiquidDistort, EffectId::Kaleidoscope, EffectId::SegmentWave,
    };
    return catalog;
}

const std::vector<EffectId>& ForegroundTerminalCatalog() {
    static const std::vector<EffectId> catalog = {
        EffectId::VortexSuction, EffectId::FragmentFlyAway, EffectId::GlassShatter,
        EffectId::ConfettiFall,  EffectId::MosaicCollapse,  EffectId::NoiseDissolve,
    };
    return catalog;
}

const std::vector<EffectId>& BackgroundContinuousCatalog() {
    static const std::vector<EffectId> catalog = {
        EffectId::Ripple,   EffectId::FadeOutIn,  EffectId::ZoomShake,          EffectId::Tilt,
        EffectId::LensDistort, EffectId::BackgroundKaleidoscope, EffectId::NoiseRipple,
        EffectId::HueShift, EffectId::GlitchShift, EffectId::ParallaxTilt,      EffectId::WaveZoom,
    };
    return catalog;
}

const std::vector<EffectId>& BackgroundTerminalCatalog() {
    static const std::vector<EffectId> catalog = {EffectId::BackgroundSuction};
    return catalog;
}

const std::vector<EffectId>& CatalogFor(LayerKind layer, EffectKind kind) {
    if (layer == LayerKind::Foreground) {
        return kind == EffectKind::Continuous ? ForegroundContinuousCatalog() : ForegroundTerminalCatalog();
    }
    return kind == EffectKind::Continuous ? BackgroundContinuousCatalog() : BackgroundTerminalCatalog();
}

} // namespace core::fx
