#pragma once
// Per-effect and per-layer configuration structs (DESIGN_EFFECTS.md §4.2, §9).
// Defaults here are the single source of truth referenced throughout §6.1/§6.2.

#include <cstdint>
#include <map>
#include <vector>

#include "EffectTypes.h"

namespace core::fx {

// Common to every effect, and the only part exposed in config.ini per effect
// (§9.2). minSeconds/maxSeconds of 0 means "use the layer default" (§5.5).
struct EffectParams {
    bool enabled = true;
    float intensity = 0.7f;
    float weight = 1.0f;
    float minSeconds = 0.0f;
    float maxSeconds = 0.0f;
};

// ---- Foreground effect-specific parameters (§6.1) ----------------------

struct FlagWaveParams {
    float ampRatio = 0.03f;
    float wavesAcross = 1.5f;
    float hz = 0.8f;
    float shear = 0.25f;
    float shadeDepth = 0.25f;
};

struct NorenSwingParams {
    int strips = 6;
    float ampRatio = 0.06f;
    float hz = 0.5f;
    float phaseStep = 0.9f;
    float pinPow = 1.5f;
};

struct InfiniteScrollParams {
    float speedRatio = 0.15f;
};

struct InfiniteRotationParams {
    float secondsPerTurn = 12.0f;
};

struct ClothBendParams {
    float sagRatio = 0.08f;
    float hz = 0.35f;
    float contractRatio = 0.03f;
    float shadeDepth = 0.2f;
};

struct LiquidDistortParams {
    float ampRatio = 0.04f;
    float wavelengthRatio = 0.25f;
    float timeScale = 0.6f;
    float refract = 0.3f;
};

struct KaleidoscopeParams {
    float rotHz = 0.05f;
    float zoomAmp = 0.1f;
    float zoomHz = 0.15f;
};

struct SegmentWaveParams {
    int bandRows = 2;
    float ampRatio = 0.05f;
    float hz = 0.7f;
    float phaseStep = 0.6f;
    bool alternate = true;
};

struct FragmentFlyAwayParams {
    float speedMin = 400.0f;
    float speedMax = 900.0f;
    float propagation = 2000.0f;
    float drag = 0.6f;
    float gravity = 300.0f;
    float spinMax = 3.0f;
    float fadeSeconds = 1.5f;
};

struct GlassShatterParams {
    int shardsMin = 12;
    int shardsMax = 30;
    float propagation = 2500.0f;
    float gravity = 1200.0f;
    float spinMax = 2.0f;
    float kickMin = 60.0f;
    float kickMax = 220.0f;
};

struct ConfettiFallParams {
    float totalStaggerSeconds = 2.5f;
    float jitter = 0.4f;
    float fallMin = 250.0f;
    float fallMax = 450.0f;
    float swayMin = 20.0f;
    float swayMax = 60.0f;
    float swayHzMin = 0.6f;
    float swayHzMax = 1.4f;
    float tumbleHz = 1.2f;
};

struct MosaicCollapseParams {
    float collapseSeconds = 2.5f;
    float jitter = 0.3f;
    float gravity = 1500.0f;
    float shrinkTo = 0.6f;
    float kickX = 40.0f;
};

struct NoiseDissolveParams {
    float dissolveSeconds = 3.0f;
    float edge = 0.08f;
    float cellFreq = 0.15f;
    float shrink = 0.4f;
};

// ---- Background effect-specific parameters (§6.2) -----------------------

struct RippleParams {
    float ampPx = 12.0f;
    float wavelengthPx = 120.0f;
    float hz = 1.2f;
    float decayPx = 600.0f;
    float decaySec = 2.5f;
    float spawnMinSec = 0.8f;
    float spawnMaxSec = 2.0f;
    int maxDrops = 6;
};

struct FadeOutInParams {
    float holdRatio = 0.2f;
    float floor = 0.15f; // U-2 (§17): a fully black floor would be indistinguishable from STATE_BLACK
};

struct ZoomShakeParams {
    float zoomAmp = 0.04f;
    float zoomHz = 1.3f;
    float jitterAmp = 0.015f;
    float jitterHz = 9.0f;
    float shakePx = 6.0f;
    float shakeHz = 11.0f;
};

struct TiltParams {
    float maxDeg = 4.0f;
    float periodSec = 5.0f;
};

struct LensDistortParams {
    float kMax = 0.15f;
    float periodSec = 6.0f;
};

struct BackgroundKaleidoscopeParams {
    float rotHz = 0.03f;
    float zoomAmp = 0.15f;
    float zoomHz = 0.1f;
};

struct NoiseRippleParams {
    float ampPx = 8.0f;
    float wavelengthPx = 160.0f;
    float hz = 0.8f;
    float noiseScalePx = 400.0f;
    float noiseTime = 0.4f;
    float phaseNoise = 2.0f;
};

struct HueShiftParams {
    float cycleSec = 12.0f;
    int steps = 6;
    uint64_t maxRingBytes = 64ull * 1024 * 1024;
};

struct GlitchShiftParams {
    float burstGapMin = 0.3f;
    float burstGapMax = 1.5f;
    float burstLenMin = 0.06f;
    float burstLenMax = 0.2f;
    int bandsMin = 3;
    int bandsMax = 8;
    float shiftRatio = 0.06f;
    float rgbSplitPx = 8.0f;
    float rebandEvery = 0.05f;
};

struct ParallaxTiltParams {
    float driftRatio = 0.03f;
    float driftHz = 0.3f;
    float rotDeg = 0.5f;
};

struct WaveZoomParams {
    float zoomAmp = 0.06f;
    float wavesAcross = 1.5f;
    float hz = 0.5f;
};

// ---- Per-layer / engine configuration (§9) ------------------------------

struct LayerEffectConfig {
    std::map<EffectId, EffectParams> perEffect;
    float defaultMinSeconds = 5.0f;
    float defaultMaxSeconds = 10.0f;
};

struct EngineConfig {
    bool enabled = true;
    float transitionSeconds = 0.5f;
    float foregroundShowcaseSeconds = 40.0f;
    float terminalMaxSeconds = 20.0f;
    LayerEffectConfig foreground;
    LayerEffectConfig background;
    std::vector<EffectId> scriptedForeground;
    std::vector<EffectId> scriptedBackground;
};

// Builds a LayerEffectConfig with every catalog effect present (enabled,
// default params) and the given layer default durations -- used both as the
// factory-default EngineConfig and as a starting point when parsing config.ini.
LayerEffectConfig MakeDefaultForegroundEffectConfig();
LayerEffectConfig MakeDefaultBackgroundEffectConfig();
EngineConfig MakeDefaultEngineConfig();

} // namespace core::fx
