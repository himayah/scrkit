#include "AppController.h"

#include <algorithm>
#include <random>

#include "../../core/ContentMask.h"
#include "../../core/Logger.h"
#include "../../core/WallpaperFit.h"
#include "OpenGLContext.h"
#include "Renderer.h"
#include "WallpaperProvider.h"

namespace platform {

namespace {

bool AllDead(const std::vector<core::SpiralState>& states) {
    return std::all_of(states.begin(), states.end(), [](const core::SpiralState& s) { return !s.alive; });
}

} // namespace

AppController::~AppController() { Shutdown(); }

bool AppController::Initialize(HDC hdc, int screenWidthPx, int screenHeightPx,
                                const core::ConfigModel& config, const std::wstring& wallpaperPath,
                                const DecodedImage* desktopCapture) {
    screenWidth_ = screenWidthPx;
    screenHeight_ = screenHeightPx;

    // 1. Background image (falls back to a flat color rather than failing --
    //    design doc: エラーハンドリング方針). When there's no wallpaper *file*
    //    to decode -- notably, Windows reports an empty path here (not a
    //    decode failure) when the user has chosen a plain solid-color
    //    background instead of a picture -- fall back to that real desktop
    //    color rather than an arbitrary placeholder, so both the rendered
    //    background and the content diff below actually match what's really
    //    on screen (user feedback: a visibly-wrong flat placeholder color was
    //    showing through wherever content particles had been sucked away).
    // Also used below as the letterbox fill color for Center/Fit wallpaper
    // styles, so read it unconditionally.
    uint8_t desktopR = 30, desktopG = 40, desktopB = 60;
    GetSystemDesktopColor(desktopR, desktopG, desktopB);

    DecodedImage image;
    if (wallpaperPath.empty() || !DecodeImageFile(wallpaperPath, image)) {
        if (!wallpaperPath.empty()) {
            core::Logger::Warn("AppController: falling back to placeholder background image");
        }
        image = MakeFallbackImage(desktopR, desktopG, desktopB);
    }
    backgroundTexture_ = CreateTextureFromImage(image);
    if (backgroundTexture_ == 0) {
        core::Logger::Error("AppController: failed to create background texture");
        return false;
    }

    // 2. Particle count: resolve Auto/Custom/preset into a concrete count,
    //    then lay out the NxN grid (要件.txt §5, §6). Computed before the
    //    content diff below, since the content phase re-uses this exact
    //    same grid (only a filtered subset of it).
    const int autoCount = ResolveAutoParticleCountFromCurrentContext();
    resolvedParticleCount_ = config.ResolveParticleCount(autoCount);
    if (resolvedParticleCount_ < 1) resolvedParticleCount_ = 1;

    core::ParticleGridConfig gridConfig;
    gridConfig.screenWidth = static_cast<float>(screenWidth_);
    gridConfig.screenHeight = static_cast<float>(screenHeight_);
    gridConfig.gridN = core::ComputeGridDimensionForParticleCount(resolvedParticleCount_);
    gridConfig.particleCount = resolvedParticleCount_;
    particles_ = core::BuildParticleGrid(gridConfig);

    // Slightly oversize each particle relative to its grid cell so adjacent
    // particles overlap a hair instead of leaving hairline seams, while
    // still tiling the screen with no large gaps for any particle count.
    // Kept per-axis (not forced square via max(cellWidth, cellHeight)) since
    // a grid cell isn't square on a non-square screen -- a square quad would
    // stretch whatever it samples (user feedback: icons/taskbar text looked
    // vertically stretched in the content phase).
    const float cellWidth = static_cast<float>(screenWidth_) / static_cast<float>(std::max(1, gridConfig.gridN));
    const float cellHeight = static_cast<float>(screenHeight_) / static_cast<float>(std::max(1, gridConfig.gridN));
    particleHalfWidthPx_ = cellWidth * 0.55f;
    particleHalfHeightPx_ = cellHeight * 0.55f;

    // 3. Real desktop capture + content diff (optional): a still image of
    //    the real screen, diffed cell-by-cell against the wallpaper (same
    //    grid as particles_ above) so only the cells that actually differ --
    //    real icons, the taskbar, open windows -- become "content" particles
    //    (user feedback: querying/capturing individual windows and icons did
    //    not hold up in practice; a single whole-screen diff replaces that).
    if (captureTexture_ != 0) {
        glDeleteTextures(1, &captureTexture_);
        captureTexture_ = 0;
    }
    contentParticles_.clear();

    if (desktopCapture && desktopCapture->width == screenWidth_ && desktopCapture->height == screenHeight_) {
        captureTexture_ = CreateTextureFromImage(*desktopCapture);
        if (captureTexture_ != 0) {
            // Composite the wallpaper the same way Windows actually
            // positions/scales it (Fill/Fit/Stretch/Center/Tile) -- a plain
            // stretch only matches the "Stretch" style, and mismatches
            // elsewhere (Fill, the Windows 10/11 default, crops instead)
            // made the diff flag large swaths of plain background as
            // "content" (user feedback).
            DecodedImage compositedWallpaper;
            compositedWallpaper.width = screenWidth_;
            compositedWallpaper.height = screenHeight_;
            compositedWallpaper.rgba.assign(static_cast<size_t>(screenWidth_) * screenHeight_ * 4, 0);
            core::CompositeWallpaper(image.rgba.data(), image.width, image.height,
                                      compositedWallpaper.rgba.data(), screenWidth_, screenHeight_,
                                      GetSystemWallpaperFitMode(), desktopR, desktopG, desktopB);

            core::ContentMaskConfig maskConfig;
            maskConfig.screenWidth = screenWidth_;
            maskConfig.screenHeight = screenHeight_;
            maskConfig.gridN = gridConfig.gridN;
            const std::vector<bool> mask = core::ComputeContentMask(
                desktopCapture->rgba.data(), compositedWallpaper.rgba.data(), maskConfig);

            for (size_t i = 0; i < particles_.size() && i < mask.size(); ++i) {
                if (mask[i]) contentParticles_.push_back(particles_[i]);
            }
            core::Logger::Info("AppController: " + std::to_string(contentParticles_.size()) + "/" +
                                std::to_string(particles_.size()) +
                                " grid cell(s) flagged as real desktop content");
        } else {
            core::Logger::Warn("AppController: failed to create desktop capture texture; content phase will be skipped");
        }
    }

    // 4. Suction center random walk (要件.txt §4: 速度は一定, 1〜3px/frame).
    rng_ = std::make_unique<core::Mt19937RandomSource>(std::random_device{}());
    core::WalkerBounds bounds{0.0f, 0.0f, static_cast<float>(screenWidth_), static_cast<float>(screenHeight_)};
    core::Vec2 startCenter{screenWidth_ * 0.5f, screenHeight_ * 0.5f};
    center_ = std::make_unique<core::SuctionCenterWalker>(startCenter, bounds, 2.0f);

    stateMachine_ = core::SaverStateMachine(core::SaverState::STATE_CONTENT);
    contentInitialized_ = particlesInitialized_ = false;
    blackHoldTimer_ = resetHoldTimer_ = 0.0f;
    fade_.Reset();

    core::Logger::Info("AppController: initialized (" + std::to_string(resolvedParticleCount_) +
                        " particles, " + std::to_string(contentParticles_.size()) + " content cell(s))");
    return true;
}

void AppController::Shutdown() {
    if (backgroundTexture_ != 0) {
        glDeleteTextures(1, &backgroundTexture_);
        backgroundTexture_ = 0;
    }
    if (captureTexture_ != 0) {
        glDeleteTextures(1, &captureTexture_);
        captureTexture_ = 0;
    }
}

void AppController::EnsureContentSpiralsInit(core::Vec2 centerPos) {
    if (contentInitialized_) return;
    contentSpirals_.resize(contentParticles_.size());
    contentCurrentPos_.resize(contentParticles_.size());
    contentSpiralParams_.resize(contentParticles_.size());
    for (size_t i = 0; i < contentParticles_.size(); ++i) {
        const auto& p = contentParticles_[i];
        contentSpirals_[i] = core::MakeSpiralState(p.x, p.y, centerPos.x, centerPos.y);
        contentCurrentPos_[i] = {p.x, p.y};
        // 追加要望: らせん回転をもっと緩やかにし、3〜5周回するくらいで中心に
        // 消えるようにする -- 個体差として範囲内でランダム化する。
        const float targetRevolutions =
            kSpiralMinRevolutions + rng_->NextFloat01() * (kSpiralMaxRevolutions - kSpiralMinRevolutions);
        contentSpiralParams_[i] =
            core::MakeParamsForRevolutions(contentSpirals_[i].r, kContentSuctionSpeed, targetRevolutions);
    }
    contentInitialized_ = true;
}

void AppController::EnsureParticleSpiralsInit(core::Vec2 centerPos) {
    if (particlesInitialized_) return;
    particleSpirals_.resize(particles_.size());
    particleCurrentPos_.resize(particles_.size());
    particleSpiralParams_.resize(particles_.size());
    // 要件.txt §7: 粒子が多いときは軽量な吸い込み速度を使う。
    const float suctionSpeed = resolvedParticleCount_ > kLightweightParticleThreshold
                                    ? core::LightweightSpiralParams().suctionSpeed
                                    : core::NormalSpiralParams().suctionSpeed;
    for (size_t i = 0; i < particles_.size(); ++i) {
        particleSpirals_[i] = core::MakeSpiralState(particles_[i].x, particles_[i].y, centerPos.x, centerPos.y);
        particleCurrentPos_[i] = {particles_[i].x, particles_[i].y};
        // 追加要望: 背景画像側のらせん回転ももっと緩やかに、3〜5周回するくらい
        // にする -- content側と同じ考え方で個体差をランダム化する。
        const float targetRevolutions =
            kSpiralMinRevolutions + rng_->NextFloat01() * (kSpiralMaxRevolutions - kSpiralMinRevolutions);
        particleSpiralParams_[i] = core::MakeParamsForRevolutions(particleSpirals_[i].r, suctionSpeed, targetRevolutions);
        // 追加要望: 背景画像が吸い込まれるとき、中心に近づくほど角速度を上げて
        // らせん状に歪める(この効果は維持する)。
        particleSpiralParams_[i].centerAccelFactor = kParticleCenterAccelFactor;
    }
    particlesInitialized_ = true;
}

void AppController::StepContentSpirals(core::Vec2 centerPos) {
    for (size_t i = 0; i < contentSpirals_.size(); ++i) {
        if (contentSpirals_[i].alive) {
            contentCurrentPos_[i] =
                core::StepSpiral(contentSpirals_[i], contentSpiralParams_[i], centerPos.x, centerPos.y);
        }
    }
}

void AppController::StepParticleSpirals(core::Vec2 centerPos) {
    for (size_t i = 0; i < particleSpirals_.size(); ++i) {
        if (particleSpirals_[i].alive) {
            particleCurrentPos_[i] =
                core::StepSpiral(particleSpirals_[i], particleSpiralParams_[i], centerPos.x, centerPos.y);
        }
    }
}

void AppController::OnStateEntered(core::SaverState newState, core::Vec2 centerPos) {
    // Eagerly (re-)initialize the newly-entered phase's spiral state right
    // away, in the same Update() call that triggered the transition. Without
    // this, the Draw() call immediately following this Update() would see an
    // empty/stale spiral array for one frame (harmless, but an easy-to-avoid
    // blank flash) since the lazy EnsureXxxInit() calls in Update()'s state
    // switch only run for whichever state is *current* at the top of that
    // function, not the one just transitioned into.
    switch (newState) {
        case core::SaverState::STATE_CONTENT:
            // Loop restart (要件.txt §4 step 7): everything re-spirals in from
            // its original position next time each phase is entered.
            contentInitialized_ = false;
            particlesInitialized_ = false;
            EnsureContentSpiralsInit(centerPos);
            break;
        case core::SaverState::STATE_BACKGROUND:
            EnsureParticleSpiralsInit(centerPos);
            break;
        case core::SaverState::STATE_BLACK:
            blackHoldTimer_ = 0.0f;
            break;
        case core::SaverState::STATE_FADE:
            fade_.Reset();
            break;
        case core::SaverState::STATE_RESET:
            resetHoldTimer_ = 0.0f;
            break;
    }
}

void AppController::Update(float dtSeconds) {
    if (!center_ || !rng_) return;

    center_->Step(*rng_);
    const core::Vec2 centerPos = center_->Position();

    core::StateMachineInputs inputs;
    switch (stateMachine_.Current()) {
        case core::SaverState::STATE_CONTENT:
            EnsureContentSpiralsInit(centerPos);
            StepContentSpirals(centerPos);
            inputs.allContentConsumed = AllDead(contentSpirals_);
            break;
        case core::SaverState::STATE_BACKGROUND:
            EnsureParticleSpiralsInit(centerPos);
            StepParticleSpirals(centerPos);
            inputs.allParticlesConsumed = AllDead(particleSpirals_);
            break;
        case core::SaverState::STATE_BLACK:
            blackHoldTimer_ += dtSeconds;
            inputs.blackHoldElapsed = blackHoldTimer_ >= kBlackHoldSeconds;
            break;
        case core::SaverState::STATE_FADE:
            fade_.Step(dtSeconds);
            inputs.fadeComplete = fade_.IsComplete();
            break;
        case core::SaverState::STATE_RESET:
            resetHoldTimer_ += dtSeconds;
            inputs.resetHoldElapsed = resetHoldTimer_ >= kResetHoldSeconds;
            break;
    }

    if (stateMachine_.Advance(inputs)) {
        OnStateEntered(stateMachine_.Current(), centerPos);
    }
}

void AppController::DrawContentPhase() const {
    DrawFullscreenTexturedQuad(backgroundTexture_, screenWidth_, screenHeight_, 1.0f);

    std::vector<DrawParticle> drawParticles;
    drawParticles.reserve(contentParticles_.size());
    for (size_t i = 0; i < contentParticles_.size(); ++i) {
        if (i < contentSpirals_.size() && !contentSpirals_[i].alive) continue;
        const auto& p = contentParticles_[i];
        const auto& pos = i < contentCurrentPos_.size() ? contentCurrentPos_[i] : core::Vec2{p.x, p.y};
        drawParticles.push_back({pos.x, pos.y, p.u0, p.v0, p.u1, p.v1});
    }
    DrawParticlesBatched(captureTexture_, drawParticles, particleHalfWidthPx_, particleHalfHeightPx_);
}

void AppController::DrawBackgroundPhase() const {
    ClearBlack(); // consumed particles reveal black underneath (要件.txt §4)
    std::vector<DrawParticle> drawParticles;
    drawParticles.reserve(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i) {
        if (i >= particleSpirals_.size() || !particleSpirals_[i].alive) continue;
        const auto& p = particles_[i];
        const auto& pos = particleCurrentPos_[i];
        drawParticles.push_back({pos.x, pos.y, p.u0, p.v0, p.u1, p.v1});
    }
    DrawParticlesBatched(backgroundTexture_, drawParticles, particleHalfWidthPx_, particleHalfHeightPx_);
}

void AppController::DrawResetPhase() const {
    DrawFullscreenTexturedQuad(backgroundTexture_, screenWidth_, screenHeight_, 1.0f);

    std::vector<DrawParticle> drawParticles;
    drawParticles.reserve(contentParticles_.size());
    for (const auto& p : contentParticles_) {
        drawParticles.push_back({p.x, p.y, p.u0, p.v0, p.u1, p.v1}); // original position, at rest
    }
    DrawParticlesBatched(captureTexture_, drawParticles, particleHalfWidthPx_, particleHalfHeightPx_);
}

void AppController::Draw() const {
    switch (stateMachine_.Current()) {
        case core::SaverState::STATE_CONTENT:
            DrawContentPhase();
            break;
        case core::SaverState::STATE_BACKGROUND:
            DrawBackgroundPhase();
            break;
        case core::SaverState::STATE_BLACK:
            ClearBlack();
            break;
        case core::SaverState::STATE_FADE:
            ClearBlack();
            DrawFullscreenTexturedQuad(backgroundTexture_, screenWidth_, screenHeight_, fade_.Alpha());
            break;
        case core::SaverState::STATE_RESET:
            DrawResetPhase();
            break;
    }
}

} // namespace platform
