#pragma once
// Owns the whole animation: content/background particle grids, suction
// center, spiral states, state machine, and fade -- and turns them into
// per-frame draw calls via Renderer (要件.txt §4, §8, Step 10 統合).

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
#include "ImageLoader.h"

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
    // wallpaper right now -- become "content" particles that spiral into the
    // suction center; cells with no difference are never drawn at all, so
    // the wallpaper (already the base layer) simply shows through there.
    // When absent, the content phase has nothing to show and is skipped.
    bool Initialize(HDC hdc, int screenWidthPx, int screenHeightPx, const core::ConfigModel& config,
                    const std::wstring& wallpaperPath, const DecodedImage* desktopCapture = nullptr);

    void Update(float dtSeconds);
    void Draw() const;

    void Shutdown();

private:
    // --- fixed configuration resolved at Initialize() time ---
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    int resolvedParticleCount_ = 3000;
    GLuint backgroundTexture_ = 0;
    GLuint captureTexture_ = 0; // real-desktop capture; 0 when unavailable
    std::vector<core::Particle> particles_; // full grid, generated once (background phase; 要件4-6)
    // Subset of particles_ that core::ContentMask flagged as differing from
    // the wallpaper -- i.e. the "content" to suck away first. Empty when no
    // desktop capture was supplied or nothing was flagged.
    std::vector<core::Particle> contentParticles_;

    // --- live simulation state ---
    std::unique_ptr<core::Mt19937RandomSource> rng_;
    std::unique_ptr<core::SuctionCenterWalker> center_;
    core::SaverStateMachine stateMachine_;
    core::FadeController fade_{2.0f}; // 2s black->image fade (要件4 step5)

    std::vector<core::SpiralState> contentSpirals_;
    std::vector<core::Vec2> contentCurrentPos_;
    std::vector<core::SpiralState> particleSpirals_;
    std::vector<core::Vec2> particleCurrentPos_;
    // Per-particle spiral params for both phases above: each particle gets
    // its own dTheta, derived from its own starting radius so it completes
    // roughly the same number of revolutions regardless of how far it
    // happens to start from the (randomly wandering) suction center -- see
    // core::MakeParamsForRevolutions. (particleSpiralParams_ additionally
    // has kParticleCenterAccelFactor layered on top of that, for the
    // separately-requested near-center vortex tightening.)
    std::vector<core::SpiralParams> contentSpiralParams_;
    std::vector<core::SpiralParams> particleSpiralParams_;

    // Half-width/height of every particle quad in pixels, computed once in
    // Initialize() from the grid cell size (screen size / gridN) so
    // particles tile the screen with no visible gaps regardless of the
    // configured particle count, then used unchanged for every particle for
    // the rest of the run -- still "fixed" per 要件.txt §7 (it never varies
    // per-particle or per-frame), just resolution/config-dependent rather
    // than a hardcoded literal. Shared by the content and background
    // phases, since both draw from the same grid. Tracked separately per
    // axis (not a single square half-size) because a grid cell itself isn't
    // square on a non-square screen (e.g. 1920x1080 with a square NxN grid
    // gives cells ~1.78x wider than tall) -- forcing a square quad stretched
    // whatever was sampled into it, most visibly as taskbar clock text and
    // icons looking vertically stretched in the content phase (user
    // feedback; the same distortion was silently present in the background
    // phase's wallpaper shatter too, just less noticeable there).
    float particleHalfWidthPx_ = 4.0f;
    float particleHalfHeightPx_ = 4.0f;

    bool contentInitialized_ = false;
    bool particlesInitialized_ = false;

    float blackHoldTimer_ = 0.0f;
    float resetHoldTimer_ = 0.0f;
    static constexpr float kBlackHoldSeconds = 1.0f;
    static constexpr float kResetHoldSeconds = 1.5f;
    // Above this particle count, use the lighter spiral params from 要件.txt §7.
    static constexpr int kLightweightParticleThreshold = 3000;
    // core::SpiralParams::centerAccelFactor for the background particle
    // phase -- makes the image visibly warp into a tighter spiral as it
    // nears the suction center (追加要望: 中心に近づくほど角速度を上げる).
    // Halved along with kSpiralMinRevolutions/kSpiralMaxRevolutions below
    // (追加要望: 回転速度をさらに半分程度に) so this near-center flourish
    // scales down with the rest of the rotation instead of staying fixed.
    static constexpr float kParticleCenterAccelFactor = 4.0f;
    // Radial shrink rate for content particles (要件.txt §4: r -= 吸い込み速度).
    // Left unchanged by the revolution-count halving below -- it alone
    // determines how long a particle takes to reach the center, and that
    // duration should stay the same while only the rotation slows down
    // (追加要望: 吸い込まれるまでの時間はそのままに回転速度を半分程度にして
    // ほしい). See core::MakeParamsForRevolutions: dTheta is derived from
    // (revolutions, r0, suctionSpeed) so halving revolutions alone halves
    // the angular speed without changing the frame count to consumption.
    static constexpr float kContentSuctionSpeed = 2.0f;
    // Every particle (content and background phases alike) completes a
    // randomized number of revolutions in this range before reaching the
    // center -- see core::MakeParamsForRevolutions. Originally 3-5, then
    // halved to 1.5-2.5 per further 追加要望 (回転速度をさらに半分程度に,
    // 吸い込まれるまでの時間は変えない).
    static constexpr float kSpiralMinRevolutions = 1.5f;
    static constexpr float kSpiralMaxRevolutions = 2.5f;

    void EnsureContentSpiralsInit(core::Vec2 centerPos);
    void EnsureParticleSpiralsInit(core::Vec2 centerPos);

    void StepContentSpirals(core::Vec2 centerPos);
    void StepParticleSpirals(core::Vec2 centerPos);

    void OnStateEntered(core::SaverState newState, core::Vec2 centerPos);

    void DrawContentPhase() const;
    void DrawBackgroundPhase() const;
    void DrawResetPhase() const;
};

} // namespace platform
