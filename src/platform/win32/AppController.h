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
    core::FadeController fade_{2.0f}; // 2s black->image fade (要件4 step5)
    std::unique_ptr<core::fx::EffectEngine> effectEngine_;

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

    void OnStateEntered(core::SaverState newState);

    EffectTextureTable BuildTextureTable() const;
};

} // namespace platform
