#pragma once
// Owns the whole animation: content/background particle grids, suction
// center, state machine, fade, and (since docs/DESIGN_EFFECTS.md §16 Step 8)
// the layer-separated effect engine -- and turns them into per-frame draw
// calls via Renderer::ExecuteDrawList (要件.txt §4, §8; DESIGN_EFFECTS.md §2).

#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "../../core/ConfigModel.h"
#include "../../core/FadeController.h"
#include "../../core/ParticleGrid.h"
#include "../../core/RandomSource.h"
#include "../../core/SpiralMath.h"
#include "../../core/StateMachine.h"
#include "../../core/SuctionCenterWalker.h"
#include "../../core/effects/EffectEngine.h"
#include "HueRingBuilder.h"
#include "ImageLoader.h"
#include "Renderer.h"

namespace platform {

class AppController {
public:
    AppController() = default;
    ~AppController();

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    // `hdc` must belong to a window with an already-current GL context.
    // `wallpaperPath` is whichever image should be used as the background
    // (already resolved by the caller: config override, else system
    // wallpaper, possibly empty if neither is available).
    // `desktopCapture`, when non-null, is a still image of the real screen
    // taken just before the saver's own window covered it (caller must have
    // captured it at exactly screenWidthPx x screenHeightPx, i.e. this is
    // only meaningful for the real fullscreen size, not a scaled-down
    // preview). When present, it's diffed cell-by-cell against the wallpaper
    // (see core::ContentMask) and only the cells that actually differ --
    // real icons, the taskbar, open windows, anything drawn on top of the
    // wallpaper right now -- become the foreground layer's content; cells
    // with no difference are transparent there, so the wallpaper (already
    // the base layer) simply shows through.
    // `isPreviewMode` distinguishes *why* desktopCapture might be null: `/p`
    // never captures at all (by design, DESIGN.md §9.1), vs a real `/s`
    // BitBlt failure -- these need different foreground-layer EmptyReason
    // handling (DESIGN_EFFECTS.md §4.1/§5.3/§10, D-15) even though both look
    // identical from here (desktopCapture == nullptr either way).
    bool Initialize(HDC hdc, int screenWidthPx, int screenHeightPx, const core::ConfigModel& config,
                    const std::wstring& wallpaperPath, const DecodedImage* desktopCapture = nullptr,
                    bool isPreviewMode = false);

    void Update(float dtSeconds);
    void Draw() const;

    // External-control access (SCRAPI preview, docs/DESIGN_VIEWER.md §B.3). Null before
    // Initialize() succeeds.
    core::fx::EffectEngine* Engine() { return effectEngine_.get(); }
    // false: hold STATE_CONTENT indefinitely (the blackout/fade cycle never starts), so
    // an effect pinned for inspection isn't interrupted. Default true (normal saver).
    void SetAutoCycle(bool enabled) { autoCycle_ = enabled; }

    // SCRAPI preview only. "sample": the foreground shows a built-in desktop (two windows, icons,
    // a taskbar) run through the real mask pipeline; anything else: an empty foreground.
    void ApplyContentSource(const std::string& source);
    // Tints the cells detected as content and outlines the window rectangles.
    void SetMaskOverlay(bool on) { maskOverlay_ = on; }
    // Reseeds the random source and starts the show over (both layers, from the beginning).
    void Restart(uint32_t seed);
    // One-line description of what the foreground currently contains.
    const std::string& ContentInfo() const { return contentInfo_; }

    void Shutdown();

private:
    // --- fixed configuration resolved at Initialize() time ---
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    int resolvedParticleCount_ = 3000;
    int gridN_ = 1;
    GLuint backgroundTexture_ = 0;
    GLuint foregroundTexture_ = 0; // masked real-desktop capture (§7.4); 0 when unavailable
    std::vector<core::Particle> particles_; // full grid, generated once (background layer; 要件4-6)
    // Subset of particles_ that core::ContentMask flagged as differing from
    // the wallpaper -- the foreground layer's content.
    std::vector<core::Particle> contentParticles_;
    core::fx::EmptyReason foregroundEmptyReason_ = core::fx::EmptyReason::NotEmpty;

    // --- live simulation state ---
    std::unique_ptr<core::Mt19937RandomSource> rng_;
    std::unique_ptr<core::SuctionCenterWalker> center_;
    core::SaverStateMachine stateMachine_;
    core::FadeController fade_{2.0f};    // 2s black->image fade (要件4 step5)
    core::FadeController fadeOut_{2.0f}; // 2s whatever's-on-screen->black fade (STATE_FADEOUT)
    std::unique_ptr<core::fx::EffectEngine> effectEngine_;

    // §6.2.8/§6.2.8.1: HueShift's pre-generated hue-rotated background
    // copies. hueRingTextures_[i] holds ring index i+1 (ring 0 is
    // backgroundTexture_ itself, never separately generated/stored).
    std::unique_ptr<HueRingBuilder> hueRingBuilder_;
    std::vector<GLuint> hueRingTextures_;
    static constexpr int kHueRingSteps = 6; // must match HueShiftParams::steps default

    // Half-width/height of every particle quad in pixels, computed once in
    // Initialize() from the grid cell size (screen size / gridN) so
    // particles tile the screen with no visible gaps regardless of the
    // configured particle count. Tracked separately per axis since a grid
    // cell itself isn't square on a non-square screen.
    float particleHalfWidthPx_ = 4.0f;
    float particleHalfHeightPx_ = 4.0f;

    float blackHoldTimer_ = 0.0f;
    float resetHoldTimer_ = 0.0f;
    static constexpr float kBlackHoldSeconds = 1.0f;
    static constexpr float kResetHoldSeconds = 1.5f;

    // Both layers now cycle their own effects forever, independently --
    // nothing about their own progress signals when to reset. Instead, a
    // randomized "blackout" timer (picked fresh each time STATE_CONTENT is
    // entered) fires the fade-to-black/reset cycle on its own schedule, deaf
    // to whatever either layer happens to be doing at that moment (by
    // design: the two layers' effect switches are meant to stay unsynced).
    float blackoutTimer_ = 0.0f;
    float blackoutTargetSeconds_ = 0.0f;
    bool autoCycle_ = true;

    // Kept so the foreground can be rebuilt later (SCRAPI preview): the wallpaper as composited to
    // the screen size, and what the mask overlay draws.
    std::vector<uint8_t> wallpaperRgba_;
    bool maskOverlay_ = false;
    struct OverlayRect {
        float x, y, w, h;
    };
    std::vector<OverlayRect> overlayCells_;
    std::vector<OverlayRect> overlayWindows_;
    std::string contentInfo_ = "none";
    bool previewMode_ = false;
    // Basis for blackoutTargetSeconds_'s random draw: roughly 10x a single
    // effect's typical duration (the midpoint of fg/bg's own
    // defaultMinSeconds/defaultMaxSeconds, averaged across both layers),
    // computed once in Initialize() from config.effects. Each STATE_CONTENT
    // entry then draws uniformly from [0.7, 1.3] x this, so the interval
    // itself is never a fixed number (要望どおり).
    float blackoutBaseSeconds_ = 75.0f;

    // Rebuilds the foreground layer from `capture` (null = empty): mask, texture, cells, and hands
    // the new layer to the engine. `candidateRects` are window rectangles for the mask pipeline.
    void SetForegroundContent(const DecodedImage* capture, const std::vector<core::PixelRect>& candidateRects);
    core::fx::LayerSource MakeForegroundSource(const std::vector<int>& cellIndices, bool hasContent) const;
    void DrawMaskOverlay() const;

    void PickNewBlackoutTarget();
    void OnStateEntered(core::SaverState newState);

    EffectTextureTable BuildTextureTable() const;
};

} // namespace platform
