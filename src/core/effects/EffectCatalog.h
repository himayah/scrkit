#pragma once
// Single source of truth for which EffectIds exist per layer and kind
// (DESIGN_EFFECTS.md §5.3/§5.4 catalogs). Used both to build default
// per-effect configs (EffectParams.cpp) and by EffectScheduler to know what
// it's allowed to pick.

#include <optional>
#include <vector>

#include "EffectTypes.h"

namespace core::fx {

const std::vector<EffectId>& ForegroundContinuousCatalog();
const std::vector<EffectId>& ForegroundTerminalCatalog();
const std::vector<EffectId>& BackgroundContinuousCatalog();
const std::vector<EffectId>& BackgroundTerminalCatalog(); // just {BackgroundSuction}

const std::vector<EffectId>& CatalogFor(LayerKind layer, EffectKind kind);

// Which kind (continuous/terminal) `id` is on `layer`, or nullopt if `id` isn't
// in either of that layer's catalogs (e.g. a foreground effect asked of the
// background layer).
std::optional<EffectKind> EffectKindOn(LayerKind layer, EffectId id);

} // namespace core::fx
