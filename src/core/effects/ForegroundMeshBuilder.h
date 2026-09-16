#pragma once
// Shared "build the foreground diff mesh" logic (§4.4.1's gridN<48
// subdivision), used both for the layer's shared rest mesh (EffectEngine)
// and for effects that need their own mesh with extra seam columns
// (NorenSwing's strip boundaries, §6.1.2) -- the subdivision math is
// identical in both cases, only the seam columns differ.

#include <vector>

#include "Layer.h"
#include "Mesh.h"

namespace core::fx {

// seamFractions are positions across the full width in [0,1] (e.g. 1/6, 2/6,
// ... for 6 strips); they're converted to the actual (post-subdivision)
// column index internally, since callers shouldn't need to know cellSubdiv.
Mesh BuildForegroundRestMesh(const LayerSource& layer, const std::vector<float>& seamFractions = {});

} // namespace core::fx
