#include "test_framework.h"

#include "../src/core/RandomSource.h"

TEST_CASE(Mt19937RandomSource_ValuesStayInUnitRange) {
    core::Mt19937RandomSource rng(1234);
    for (int i = 0; i < 10000; ++i) {
        float v = rng.NextFloat01();
        CHECK(v >= 0.0f);
        CHECK(v < 1.0f);
    }
}

TEST_CASE(Mt19937RandomSource_SameSeedIsReproducible) {
    core::Mt19937RandomSource a(99);
    core::Mt19937RandomSource b(99);
    for (int i = 0; i < 50; ++i) {
        CHECK_NEAR(a.NextFloat01(), b.NextFloat01(), 0.00001f);
    }
}
