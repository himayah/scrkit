#pragma once
// Owns a layer's per-cell fragments for GeometryKind::Fragments effects
// (DESIGN_EFFECTS.md §4.7, §6.3.1). No per-frame allocation on the hot path:
// fragments/groups are sized once at Begin() and reused every Step().

#include <vector>

#include "../ParticleGrid.h"
#include "DrawList.h"
#include "Fragment.h"

namespace core::fx {

struct EffectFrame;

using GroupIntegrator = void (*)(FragmentGroup& g, const EffectFrame& frame, const void* params);

class FragmentSystem {
public:
    std::vector<Fragment> fragments;
    std::vector<FragmentGroup> groups;

    // Builds one fragment per cell, at rest (pos=restPos, everything else
    // default). Callers then set up per-fragment delay/velocity/etc. in
    // their own Begin().
    void InitFromCells(const std::vector<core::Particle>& cells);

    // Advances every alive fragment via `integrator`. Group-based effects
    // (GlassShatter, SegmentWave) call StepGroups first so each fragment's
    // FragmentGroup* is already up to date for this frame.
    void StepGroups(GroupIntegrator integrator, const EffectFrame& frame, const void* params);
    void StepAll(FragmentIntegrator integrator, const EffectFrame& frame, const void* params);

    bool AllDead() const;

    // Appends one quad (4 QuadVertex) per alive fragment, using `cells` for
    // each fragment's UV rect (§6.3.1 FragmentQuad).
    void EmitQuads(const std::vector<core::Particle>& cells, float cellHalfW, float cellHalfH,
                    std::vector<QuadVertex>& outQuads) const;
};

} // namespace core::fx
