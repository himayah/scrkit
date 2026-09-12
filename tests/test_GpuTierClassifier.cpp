#include "test_framework.h"

#include "../src/core/GpuTierClassifier.h"

using core::ClassifyGpuParticleCount;

TEST_CASE(GpuTierClassifier_SoftwareRendererGetsLowTier) {
    CHECK_EQ(ClassifyGpuParticleCount("Mesa", "llvmpipe (LLVM 15.0.0, 256 bits)", "4.5 (Core Profile) Mesa 23.0"), 1000);
    CHECK_EQ(ClassifyGpuParticleCount("Google", "Google SwiftShader", "3.3"), 1000);
    CHECK_EQ(ClassifyGpuParticleCount("Microsoft", "Microsoft Basic Render Driver", "4.0"), 1000);
}

TEST_CASE(GpuTierClassifier_OldGlVersionGetsLowTier) {
    CHECK_EQ(ClassifyGpuParticleCount("SomeVendor", "SomeRenderer", "1.4"), 1000);
}

TEST_CASE(GpuTierClassifier_IntelIntegratedGetsMidTier) {
    CHECK_EQ(ClassifyGpuParticleCount("Intel", "Intel(R) UHD Graphics 630", "4.6.0"), 3000);
}

TEST_CASE(GpuTierClassifier_DiscreteNvidiaAmdGetHighTier) {
    CHECK_EQ(ClassifyGpuParticleCount("NVIDIA Corporation", "GeForce GTX 1660", "4.6.0 NVIDIA 555.42"), 6000);
    CHECK_EQ(ClassifyGpuParticleCount("ATI Technologies Inc.", "AMD Radeon RX 580", "4.6.0"), 6000);
}

TEST_CASE(GpuTierClassifier_TopTierGpuGetsMaxTier) {
    CHECK_EQ(ClassifyGpuParticleCount("NVIDIA Corporation", "NVIDIA GeForce RTX 4090", "4.6.0 NVIDIA 555.42"), 12000);
    CHECK_EQ(ClassifyGpuParticleCount("NVIDIA Corporation", "Quadro RTX 6000", "4.6.0"), 12000);
}

TEST_CASE(GpuTierClassifier_UnknownVendorFallsBackToMid) {
    CHECK_EQ(ClassifyGpuParticleCount("Unknown", "Mystery GPU 3000", "4.2.0"), 3000);
}
