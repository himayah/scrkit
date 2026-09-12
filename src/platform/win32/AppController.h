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

// UV rect into a "captured desktop" texture. Public (namespace-scope, not a
// class member) so free helper functions can spell its name too.
struct UvRect {
    float u0 = 0.0f, v0 = 0.0f, u1 = 0.0f, v1 = 0.0f;
};

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
    // taken just before the saver's own window covered it; when provided,
    // icon/window boxes are textured with their corresponding clipping of
    // it instead of a flat color (caller must have captured it at the same
    // pixel dimensions as screenWidthPx x screenHeightPx, i.e. this is only
    // meaningful for the real fullscreen size, not a scaled-down preview).
    // `realIcons`/`realWindows`, when non-null and non-empty, replace the
    // randomly-generated layout with the real desktop icon/window positions
    // (see RealDesktopQuery) so suction starts from where things really are.
    bool Initialize(HDC hdc, int screenWidthPx, int screenHeightPx, const core::ConfigModel& config,
                    const std::wstring& wallpaperPath, const DecodedImage* desktopCapture = nullptr,
                    const std::vector<core::IconElement>* realIcons = nullptr,
                    const std::vector<core::WindowElement>* realWindows = nullptr);

    void Update(float dtSeconds);
    void Draw() const;

    void Shutdown();

private:
    // --- fixed configuration resolved at Initialize() time ---
    int screenWidth_ = 0;
    int screenHeight_ = 0;
    int resolvedParticleCount_ = 3000;
    GLuint backgroundTexture_ = 0;
    GLuint captureTexture_ = 0; // 0 when no desktop capture was supplied
    TextRenderer textRenderer_;
    core::DesktopLayout layout_; // generated once; reused every loop (要件4-6)
    std::vector<core::Particle> particles_; // generated once from the grid size

    // UV rects into captureTexture_ for each icon / window's title bar and
    // client area, computed once from their (fixed, original) layout
    // position -- unused when captureTexture_ is 0.
    std::vector<UvRect> iconUv_;
    std::vector<UvRect> windowTitleUv_;
    std::vector<UvRect> windowClientUv_;

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
    // core::SpiralParams::centerAccelFactor for the background particle
    // phase -- makes the image visibly warp into a tighter spiral as it
    // nears the suction center (追加要望: 中心に近づくほど角速度を上げる).
    static constexpr float kParticleCenterAccelFactor = 40.0f;

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
