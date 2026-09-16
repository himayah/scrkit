#include "test_framework.h"

#include <cmath>

#include "../src/core/ParticleGrid.h"
#include "../src/core/effects/FragmentSystem.h"
#include "../src/core/effects/IEffect.h"

using core::Particle;
using core::fx::EffectFrame;
using core::fx::Fragment;
using core::fx::FragmentGroup;
using core::fx::FragmentSystem;

namespace {
std::vector<Particle> MakeCells(int n) {
    std::vector<Particle> cells(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        Particle p;
        p.x = static_cast<float>(i) * 10.0f;
        p.y = 0.0f;
        p.u0 = static_cast<float>(i) / n;
        p.v0 = 0.0f;
        p.u1 = static_cast<float>(i + 1) / n;
        p.v1 = 1.0f;
        cells[static_cast<size_t>(i)] = p;
    }
    return cells;
}

void MoveRight(Fragment& f, const FragmentGroup*, const EffectFrame&, const void*) {
    f.pos.x += 1.0f;
    if (f.pos.x > 50.0f) f.alive = false;
}
} // namespace

TEST_CASE(FragmentSystem_InitFromCellsMatchesCellCount) {
    FragmentSystem system;
    system.InitFromCells(MakeCells(5));
    CHECK_EQ(system.fragments.size(), static_cast<size_t>(5));
    for (size_t i = 0; i < system.fragments.size(); ++i) {
        CHECK_EQ(system.fragments[i].cellIndex, static_cast<int>(i));
        CHECK(system.fragments[i].alive);
    }
}

TEST_CASE(FragmentSystem_StepAllSkipsDeadFragments) {
    FragmentSystem system;
    system.InitFromCells(MakeCells(1));
    system.fragments[0].alive = false;
    const float originalX = system.fragments[0].pos.x;
    EffectFrame frame;
    system.StepAll(&MoveRight, frame, nullptr);
    CHECK_NEAR(system.fragments[0].pos.x, originalX, 1e-6f);
}

TEST_CASE(FragmentSystem_AllDeadBecomesTrueOnceEveryFragmentDies) {
    FragmentSystem system;
    system.InitFromCells(MakeCells(3));
    EffectFrame frame;
    for (int i = 0; i < 60 && !system.AllDead(); ++i) {
        system.StepAll(&MoveRight, frame, nullptr);
    }
    CHECK(system.AllDead());
}

TEST_CASE(FragmentSystem_EmitQuadsProducesRotatedScaledCorners) {
    FragmentSystem system;
    system.InitFromCells(MakeCells(1));
    Fragment& f = system.fragments[0];
    f.pos = {100.0f, 200.0f};
    f.rot = 3.14159265358979323846f / 2.0f; // 90 degrees
    f.scaleX = 2.0f;
    f.scaleY = 1.0f;

    std::vector<core::fx::QuadVertex> quads;
    const std::vector<Particle> cells = MakeCells(1);
    system.EmitQuads(cells, 10.0f, 5.0f, quads);

    CHECK_EQ(quads.size(), static_cast<size_t>(4));
    // Local corner (-halfW,-halfH) = (-20,-5) scaled; rotated 90deg (cos=0,sin=1):
    // (x,y) -> (-y, x) = (5, -20); translated by pos.
    CHECK_NEAR(quads[0].pos.x, 100.0f + 5.0f, 1e-3f);
    CHECK_NEAR(quads[0].pos.y, 200.0f - 20.0f, 1e-3f);
    CHECK_NEAR(quads[0].uv.x, cells[0].u0, 1e-6f);
    CHECK_NEAR(quads[0].uv.y, cells[0].v0, 1e-6f);
}

TEST_CASE(FragmentSystem_EmitQuadsSkipsDeadFragments) {
    FragmentSystem system;
    system.InitFromCells(MakeCells(2));
    system.fragments[0].alive = false;
    std::vector<core::fx::QuadVertex> quads;
    system.EmitQuads(MakeCells(2), 10.0f, 10.0f, quads);
    CHECK_EQ(quads.size(), static_cast<size_t>(4)); // only the alive fragment
}

TEST_CASE(FragmentSystem_StepGroupsUpdatesGroupsSharedByFragments) {
    FragmentSystem system;
    system.InitFromCells(MakeCells(2));
    system.groups.push_back(FragmentGroup{});
    system.fragments[0].group = 0;
    system.fragments[1].group = 0;

    EffectFrame frame;
    system.StepGroups(
        [](FragmentGroup& g, const EffectFrame&, const void*) { g.centroid.x += 7.0f; }, frame, nullptr);
    CHECK_NEAR(system.groups[0].centroid.x, 7.0f, 1e-6f);
}
