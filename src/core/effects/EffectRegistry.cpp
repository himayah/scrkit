#include "EffectRegistry.h"

#include "SuctionEffect.h"

namespace core::fx {

namespace EffectRegistry {

std::unique_ptr<IEffect> Create(EffectId id) {
    switch (id) {
        // VortexSuction/BackgroundSuction share one implementation -- see
        // SuctionEffect.h for why they don't have their own fg/bg files.
        case EffectId::VortexSuction:
        case EffectId::BackgroundSuction:
            return std::make_unique<SuctionEffect>(id);

        // The remaining 23 effects (§16 Step 9) are registered here as they
        // land, one fg/*.cpp or bg/*.cpp at a time.
        default:
            return nullptr;
    }
}

} // namespace EffectRegistry

} // namespace core::fx
