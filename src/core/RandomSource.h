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

    float NextFloat01() override { return dist_(engine_); }

private:
    std::mt19937 engine_;
    std::uniform_real_distribution<float> dist_;
};

} // namespace core
