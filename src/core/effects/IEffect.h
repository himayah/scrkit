#pragma once
// Effect interface and its execution context (DESIGN_EFFECTS.md §4.5).

#include <cstdint>

#include "DrawList.h"
#include "EffectParams.h"
#include "EffectTypes.h"
#include "Layer.h"

namespace core::fx {

// Fixed at Begin() time; the effect keeps a pointer to it only for the
// duration of its own lifetime (owned by EffectEngine, outlives the effect).
struct EffectContext {
    const LayerSource* layer = nullptr;
    const EffectParams* params = nullptr;
    const EngineConfig* engine = nullptr;
    float screenW = 1920.0f;
    float screenH = 1080.0f;
    // Raw TimelineEntry.seed for this instance. Begin() uses this (and only
    // this) to construct the effect's own core::Mt19937RandomSource -- see
    // the determinism contract in IEffect::Begin's doc comment below (D-11).
    uint32_t seed = 0;
};

struct EffectFrame {
    float t = 0.0f;
    float dt = 0.0f;
    float intensity = 0.0f;      // params.intensity * envelope, §6.0.1
    Vec2 suctionCenter{0.0f, 0.0f};
    LayerGeometry* out = nullptr;
};

class IEffect {
public:
    virtual ~IEffect() = default;

    virtual EffectId Id() const = 0;
    virtual EffectKind Kind() const = 0;
    virtual GeometryKind Geometry() const = 0;
    virtual ExitStrategy Exit() const = 0;

    // Fixes this instance's individual randomness. Implementations must build
    // their own RNG from ctx.seed here (e.g. `rng_ = core::Mt19937RandomSource(ctx.seed);`)
    // and use only that RNG for every subsequent random draw in Begin/Step --
    // never a shared/engine-wide RNG. This is what makes "same ctx.seed ->
    // same output sequence" literally true regardless of execution order or
    // other effects' state (§4.5 / D-11, 外部レビュー対応不要 -- this predates
    // the external review and was already fixed in the self-review pass).
    virtual void Begin(const EffectContext& ctx) = 0;

    virtual void Step(EffectFrame& frame) = 0;

    // Terminal: true once every fragment is dead. Continuous: always false.
    virtual bool IsFinished() const = 0;

    // ReturnToRest exit strategy only: start sliding phase towards rest.
    virtual void RequestExit(float exitSeconds) { (void)exitSeconds; }
    // ReturnToRest exit strategy only: has the slide reached rest yet?
    virtual bool IsAtRest() const { return true; }
};

} // namespace core::fx
