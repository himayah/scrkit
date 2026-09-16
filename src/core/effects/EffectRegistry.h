#pragma once
// The single registration point for EffectId -> IEffect factory (§3.1, §15).

#include <memory>

#include "IEffect.h"

namespace core::fx {

namespace EffectRegistry {

// Returns nullptr for an EffectId with no implementation registered yet
// (during incremental implementation of §16 Step 9, most ids will hit this).
std::unique_ptr<IEffect> Create(EffectId id);

} // namespace EffectRegistry

} // namespace core::fx
