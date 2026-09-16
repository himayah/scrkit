#pragma once
// Shared enums and small value types for the layer-separated effect system
// (docs/DESIGN_EFFECTS.md §4.1-4.3, §4.6). Platform-independent, no GL/Windows
// headers so this is unit-testable on Linux like the rest of src/core/.

#include <cstdint>
#include <string>

#include "../SpiralMath.h"

namespace core::fx {

using Vec2 = core::Vec2;

enum class LayerKind { Background, Foreground };

enum class TextureRole { Background, Foreground, HueRing };

// Why the foreground layer has zero cells this cycle (DESIGN_EFFECTS.md §4.1,
// D-15): distinguishes the always-empty preview mode (worth waiting out the
// showcase for, so background effects are still visible) from a real capture
// failure in fullscreen mode (must skip immediately, matching the existing
// v1 error-tolerance policy in DESIGN.md §9.1).
enum class EmptyReason { NotEmpty, PreviewMode, CaptureFailed };

// How an effect treats its layer's geometry (§4.3). Each effect uses exactly
// one of these for its whole lifetime.
enum class GeometryKind { Transform, Mesh, RadialMesh, Tiles, Bands, Fragments };

enum class EffectKind { Continuous, Terminal };

// How a continuous effect gets out of the way when interrupted (§5.2).
enum class ExitStrategy { Envelope, ReturnToRest, Crossfade };

enum class BlendMode { Normal, Additive };

struct ColorMask {
    bool r = true;
    bool g = true;
    bool b = true;
};

// p' = pivot + R(rotateRad) * (scale * (p - pivot)) + translate (§4.6).
struct Transform2D {
    Vec2 translate{0.0f, 0.0f};
    float rotateRad = 0.0f;
    float scale = 1.0f;
    Vec2 pivot{0.0f, 0.0f};
};

// All effect identifiers: 14 foreground (8 continuous + 6 terminal), 11
// background continuous, and the 1 background terminal (BackgroundSuction) --
// 26 total (§3.1). Order here matches the order effects are introduced in
// §6.1/§6.2 and is not meaningful beyond that.
enum class EffectId {
    // Foreground continuous (§6.1.1-6.1.4, 6.1.7-6.1.8, 6.1.13-6.1.14)
    FlagWave,
    NorenSwing,
    InfiniteScroll,
    InfiniteRotation,
    ClothBend,
    LiquidDistort,
    Kaleidoscope,
    SegmentWave,
    // Foreground terminal (§6.1.5-6.1.6, 6.1.9-6.1.12)
    VortexSuction,
    FragmentFlyAway,
    GlassShatter,
    ConfettiFall,
    MosaicCollapse,
    NoiseDissolve,
    // Background continuous (§6.2.1-6.2.11)
    Ripple,
    FadeOutIn,
    ZoomShake,
    Tilt,
    LensDistort,
    BackgroundKaleidoscope,
    NoiseRipple,
    HueShift,
    GlitchShift,
    ParallaxTilt,
    WaveZoom,
    // Background terminal (§6.2.12)
    BackgroundSuction,
};

constexpr int kEffectIdCount = static_cast<int>(EffectId::BackgroundSuction) + 1;

// Name <-> enum conversion used by config parsing (§9.2) and logging (§11).
// Case-insensitive on the FromString direction.
const char* EffectIdToString(EffectId id);
bool EffectIdFromString(const std::string& name, EffectId& out);

} // namespace core::fx
