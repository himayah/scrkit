#include "EffectRegistry.h"

#include "SuctionEffect.h"
#include "fg/ClothBend.h"
#include "fg/FlagWave.h"
#include "fg/InfiniteRotation.h"
#include "fg/InfiniteScroll.h"
#include "fg/Kaleidoscope.h"
#include "fg/LiquidDistort.h"
#include "fg/NorenSwing.h"
#include "fg/SegmentWave.h"

namespace core::fx {

namespace EffectRegistry {

std::unique_ptr<IEffect> Create(EffectId id) {
    switch (id) {
        // VortexSuction/BackgroundSuction share one implementation -- see
        // SuctionEffect.h for why they don't have their own fg/bg files.
        case EffectId::VortexSuction:
        case EffectId::BackgroundSuction:
            return std::make_unique<SuctionEffect>(id);

        case EffectId::FlagWave:
            return std::make_unique<FlagWaveEffect>();
        case EffectId::NorenSwing:
            return std::make_unique<NorenSwingEffect>();
        case EffectId::ClothBend:
            return std::make_unique<ClothBendEffect>();
        case EffectId::LiquidDistort:
            return std::make_unique<LiquidDistortEffect>();
        case EffectId::InfiniteScroll:
            return std::make_unique<InfiniteScrollEffect>();
        case EffectId::InfiniteRotation:
            return std::make_unique<InfiniteRotationEffect>();
        case EffectId::Kaleidoscope:
            return std::make_unique<KaleidoscopeEffect>();
        case EffectId::SegmentWave:
            return std::make_unique<SegmentWaveEffect>();

        // The remaining effects (§16 Step 9) are registered here as they
        // land, one fg/*.cpp or bg/*.cpp at a time.
        default:
            return nullptr;
    }
}

} // namespace EffectRegistry

} // namespace core::fx
