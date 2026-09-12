#pragma once
// Owns the whole animation: desktop layout, suction center, spiral states,
// particle grid, state machine, and fade -- and turns them into per-frame
// draw calls via Renderer (要件.txt §4, §8, Step 10 統合).

#include <memory>
#include <string>
#include <vector>

#include <windows.h>

#include "../../core/ConfigModel.h"
#include "../../core/DesktopElements.h"
#include "../../core/FadeController.h"
#include "../../core/ParticleGrid.h"
#include "../../core/RandomSource.h"
#include "../../core/SpiralMath.h"
#include "../../core/StateMachine.h"
#include "../../core/SuctionCenterWalker.h"
#include "ImageLoader.h"
#include "TextRenderer.h"

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
    bool Initialize(HDC hdc, int screenWidthPx, int screenHeightPx, const core::ConfigModel& config,
                    const std::wstring& wallpaperPath);

    void Update(float dtSeconds);
    void Draw() const;

    void Shutdown();

private:
    // --- fixed configuration resolved at Initialize() time ---
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    int resolvedParticleCount_ = 3000;
    GLuint backgroundTexture_ = 0;
    TextRenderer textRenderer_;
    core::DesktopLayout layout_; // generated once; reused every loop (要件4-6)
    std::vector<core::Particle> particles_; // generated once from the grid size

    // --- live simulation state ---
    std::unique_ptr<core::Mt19937RandomSource> rng_;
    std::unique_ptr<core::SuctionCenterWalker> center_;
    core::SaverStateMachine stateMachine_;
    core::FadeController fade_{2.0f}; // 2s black->image fade (要件4 step5)

    std::vector<core::SpiralState> iconSpirals_;
    std::vector<core::Vec2> iconCurrentPos_;
    std::vector<core::SpiralState> windowSpirals_;
    std::vector<core::Vec2> windowCurrentPos_;
    std::vector<core::SpiralState> particleSpirals_;
    std::vector<core::Vec2> particleCurrentPos_;

    // Half-width/height of every particle quad in pixels. Computed once in
    // Initialize() from the grid cell size (screen size / gridN) so
    // particles tile the screen with no visible gaps regardless of the
    // configured particle count, then used unchanged for every particle for
    // the rest of the run -- still "fixed" per 要件.txt §7 (it never varies
    // per-particle or per-frame), just resolution/config-dependent rather
    // than a hardcoded literal.
    float particleHalfSizePx_ = 4.0f;

    bool iconsInitialized_ = false;
    bool windowsInitialized_ = false;
    bool particlesInitialized_ = false;

    float blackHoldTimer_ = 0.0f;
    float resetHoldTimer_ = 0.0f;
    static constexpr float kBlackHoldSeconds = 1.0f;
    static constexpr float kResetHoldSeconds = 1.5f;
    // Above this particle count, use the lighter spiral params from 要件.txt §7.
    static constexpr int kLightweightParticleThreshold = 3000;

    void EnsureIconSpiralsInit(core::Vec2 centerPos);
    void EnsureWindowSpiralsInit(core::Vec2 centerPos);
    void EnsureParticleSpiralsInit(core::Vec2 centerPos);

    void StepIconSpirals(core::Vec2 centerPos);
    void StepWindowSpirals(core::Vec2 centerPos);
    void StepParticleSpirals(core::Vec2 centerPos);

    void OnStateEntered(core::SaverState newState, core::Vec2 centerPos);

    void DrawIconsPhase() const;
    void DrawWindowsPhase() const;
    void DrawBackgroundPhase() const;
    void DrawResetPhase() const;
};

} // namespace platform
