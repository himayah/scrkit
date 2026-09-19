#pragma once
// Concrete core::IRandomSource backed by std::mt19937, used by the real
// (non-test) saver run. Tests use their own deterministic fake instead.

#include <cstdint>
#include <random>

#include "SuctionCenterWalker.h"

namespace core {

class Mt19937RandomSource : public IRandomSource {
public:
    explicit Mt19937RandomSource(uint32_t seed) : engine_(seed), dist_(0.0f, 1.0f) {}

    // Restarts the sequence from `seed` in place (the effect engine keeps a pointer to this
    // object, so reseeding must not replace it). Used by the SCRAPI preview's "restart".
    void Seed(uint32_t seed) { engine_.seed(seed); }

    float NextFloat01() override { return dist_(engine_); }

    // Returns the generator's native 32-bit word directly instead of going
    // through NextFloat01()'s [0,1) quantization (§5.5 / D-11).
    uint32_t NextUInt32() override { return engine_(); }

private:
    std::mt19937 engine_;
    std::uniform_real_distribution<float> dist_;
};

} // namespace core
