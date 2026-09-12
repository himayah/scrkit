#include "AppController.h"

#include <algorithm>
#include <random>

#include "../../core/Logger.h"
#include "OpenGLContext.h"
#include "Renderer.h"

namespace platform {

namespace {

core::Vec2 IconCenter(const core::IconElement& icon) {
    return {icon.x + icon.width * 0.5f, icon.y + icon.height * 0.5f};
}

core::Vec2 WindowCenter(const core::WindowElement& win) {
    const float totalHeight = win.titleBarHeight + win.height;
    return {win.x + win.width * 0.5f, win.y + totalHeight * 0.5f};
}

bool AllDead(const std::vector<core::SpiralState>& states) {
    return std::all_of(states.begin(), states.end(), [](const core::SpiralState& s) { return !s.alive; });
}

} // namespace

AppController::~AppController() { Shutdown(); }

bool AppController::Initialize(HDC hdc, int screenWidthPx, int screenHeightPx,
                                const core::ConfigModel& config, const std::wstring& wallpaperPath) {
    screenWidth_ = screenWidthPx;
    screenHeight_ = screenHeightPx;

    // 1. Background image (falls back to a flat color rather than failing --
    //    design doc: エラーハンドリング方針).
    DecodedImage image;
    if (wallpaperPath.empty() || !DecodeImageFile(wallpaperPath, image)) {
        if (!wallpaperPath.empty()) {
            core::Logger::Warn("AppController: falling back to placeholder background image");
        }
        image = MakeFallbackImage(30, 40, 60);
    }
    backgroundTexture_ = CreateTextureFromImage(image);
    if (backgroundTexture_ == 0) {
        core::Logger::Error("AppController: failed to create background texture");
        return false;
    }

    // 2. Text labels (best-effort: a failure here just means no labels).
    textRenderer_.Init(hdc);

    // 3. Desktop layout -- generated once per run, reused every suction loop
    //    (要件.txt §4 step 6).
    core::DesktopLayoutConfig layoutConfig;
    layoutConfig.screenWidth = static_cast<float>(screenWidth_);
    layoutConfig.screenHeight = static_cast<float>(screenHeight_);
    layoutConfig.seed = std::random_device{}();
    layout_ = core::GenerateDesktopLayout(layoutConfig);

    // 4. Particle count: resolve Auto/Custom/preset into a concrete count,
    //    then lay out the NxN grid (要件.txt §5, §6).
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
    const float cellWidth = static_cast<float>(screenWidth_) / static_cast<float>(std::max(1, gridConfig.gridN));
    const float cellHeight = static_cast<float>(screenHeight_) / static_cast<float>(std::max(1, gridConfig.gridN));
    particleHalfSizePx_ = std::max(cellWidth, cellHeight) * 0.55f;

    // 5. Suction center random walk (要件.txt §4: 速度は一定, 1〜3px/frame).
    rng_ = std::make_unique<core::Mt19937RandomSource>(std::random_device{}());
    core::WalkerBounds bounds{0.0f, 0.0f, static_cast<float>(screenWidth_), static_cast<float>(screenHeight_)};
    core::Vec2 startCenter{screenWidth_ * 0.5f, screenHeight_ * 0.5f};
    center_ = std::make_unique<core::SuctionCenterWalker>(startCenter, bounds, 2.0f);

    stateMachine_ = core::SaverStateMachine(core::SaverState::STATE_ICONS);
    iconsInitialized_ = windowsInitialized_ = particlesInitialized_ = false;
    blackHoldTimer_ = resetHoldTimer_ = 0.0f;
    fade_.Reset();

    core::Logger::Info("AppController: initialized (" + std::to_string(resolvedParticleCount_) +
                        " particles, " + std::to_string(layout_.icons.size()) + " icons, " +
                        std::to_string(layout_.windows.size()) + " windows)");
    return true;
}

void AppController::Shutdown() {
    if (backgroundTexture_ != 0) {
        glDeleteTextures(1, &backgroundTexture_);
        backgroundTexture_ = 0;
    }
    textRenderer_.Shutdown();
}

void AppController::EnsureIconSpiralsInit(core::Vec2 centerPos) {
    if (iconsInitialized_) return;
    iconSpirals_.resize(layout_.icons.size());
    iconCurrentPos_.resize(layout_.icons.size());
    for (size_t i = 0; i < layout_.icons.size(); ++i) {
        const core::Vec2 p = IconCenter(layout_.icons[i]);
        iconSpirals_[i] = core::MakeSpiralState(p.x, p.y, centerPos.x, centerPos.y);
        iconCurrentPos_[i] = p;
    }
    iconsInitialized_ = true;
}

void AppController::EnsureWindowSpiralsInit(core::Vec2 centerPos) {
    if (windowsInitialized_) return;
    windowSpirals_.resize(layout_.windows.size());
    windowCurrentPos_.resize(layout_.windows.size());
    for (size_t i = 0; i < layout_.windows.size(); ++i) {
        const core::Vec2 p = WindowCenter(layout_.windows[i]);
        windowSpirals_[i] = core::MakeSpiralState(p.x, p.y, centerPos.x, centerPos.y);
        windowCurrentPos_[i] = p;
    }
    windowsInitialized_ = true;
}

void AppController::EnsureParticleSpiralsInit(core::Vec2 centerPos) {
    if (particlesInitialized_) return;
    particleSpirals_.resize(particles_.size());
    particleCurrentPos_.resize(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i) {
        particleSpirals_[i] = core::MakeSpiralState(particles_[i].x, particles_[i].y, centerPos.x, centerPos.y);
        particleCurrentPos_[i] = {particles_[i].x, particles_[i].y};
    }
    particlesInitialized_ = true;
}

void AppController::StepIconSpirals(core::Vec2 centerPos) {
    const auto params = core::NormalSpiralParams();
    for (size_t i = 0; i < iconSpirals_.size(); ++i) {
        if (iconSpirals_[i].alive) {
            iconCurrentPos_[i] = core::StepSpiral(iconSpirals_[i], params, centerPos.x, centerPos.y);
        }
    }
}

void AppController::StepWindowSpirals(core::Vec2 centerPos) {
    const auto params = core::NormalSpiralParams();
    for (size_t i = 0; i < windowSpirals_.size(); ++i) {
        if (windowSpirals_[i].alive) {
            windowCurrentPos_[i] = core::StepSpiral(windowSpirals_[i], params, centerPos.x, centerPos.y);
        }
    }
}

void AppController::StepParticleSpirals(core::Vec2 centerPos) {
    // 要件.txt §7: 粒子が多いときは軽量なパラメータでらせん計算する。
    const auto params = resolvedParticleCount_ > kLightweightParticleThreshold
                             ? core::LightweightSpiralParams()
                             : core::NormalSpiralParams();
    for (size_t i = 0; i < particleSpirals_.size(); ++i) {
        if (particleSpirals_[i].alive) {
            particleCurrentPos_[i] = core::StepSpiral(particleSpirals_[i], params, centerPos.x, centerPos.y);
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
        case core::SaverState::STATE_ICONS:
            // Loop restart (要件.txt §4 step 7): everything re-spirals in from
            // its original position next time each phase is entered.
            iconsInitialized_ = false;
            windowsInitialized_ = false;
            particlesInitialized_ = false;
            EnsureIconSpiralsInit(centerPos);
            break;
        case core::SaverState::STATE_WINDOWS:
            EnsureWindowSpiralsInit(centerPos);
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
        case core::SaverState::STATE_ICONS:
            EnsureIconSpiralsInit(centerPos);
            StepIconSpirals(centerPos);
            inputs.allIconsConsumed = AllDead(iconSpirals_);
            break;
        case core::SaverState::STATE_WINDOWS:
            EnsureWindowSpiralsInit(centerPos);
            StepWindowSpirals(centerPos);
            inputs.allWindowsConsumed = AllDead(windowSpirals_);
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

void AppController::DrawIconsPhase() const {
    DrawFullscreenTexturedQuad(backgroundTexture_, screenWidth_, screenHeight_, 1.0f);

    // Windows are untouched during the icon phase; show them at rest.
    std::vector<DrawRect> clientAreas, titleBars;
    std::vector<DrawLabeledRect> windowLabels;
    clientAreas.reserve(layout_.windows.size());
    titleBars.reserve(layout_.windows.size());
    for (const auto& win : layout_.windows) {
        titleBars.push_back({win.x, win.y, win.width, win.titleBarHeight});
        clientAreas.push_back({win.x, win.y + win.titleBarHeight, win.width, win.height});
        windowLabels.push_back({{win.x + 4.0f, win.y + 4.0f, win.width, win.titleBarHeight}, win.title});
    }
    DrawWindowBodiesBatched(clientAreas, titleBars);
    DrawLabels(textRenderer_, windowLabels, 0.0f, 12.0f);

    std::vector<DrawRect> iconRects;
    std::vector<DrawLabeledRect> iconLabels;
    iconRects.reserve(layout_.icons.size());
    for (size_t i = 0; i < layout_.icons.size(); ++i) {
        if (i >= iconSpirals_.size() || !iconSpirals_[i].alive) continue;
        const auto& icon = layout_.icons[i];
        const auto& pos = iconCurrentPos_[i];
        DrawRect rect{pos.x - icon.width * 0.5f, pos.y - icon.height * 0.5f, icon.width, icon.height};
        iconRects.push_back(rect);
        iconLabels.push_back({rect, icon.label});
    }
    DrawIconBodiesBatched(iconRects);
    DrawLabels(textRenderer_, iconLabels, 0.0f, iconRects.empty() ? 0.0f : 46.0f);
}

void AppController::DrawWindowsPhase() const {
    DrawFullscreenTexturedQuad(backgroundTexture_, screenWidth_, screenHeight_, 1.0f);

    std::vector<DrawRect> clientAreas, titleBars;
    std::vector<DrawLabeledRect> windowLabels;
    for (size_t i = 0; i < layout_.windows.size(); ++i) {
        if (i >= windowSpirals_.size() || !windowSpirals_[i].alive) continue;
        const auto& win = layout_.windows[i];
        const auto& pos = windowCurrentPos_[i];
        const float totalHeight = win.titleBarHeight + win.height;
        const float left = pos.x - win.width * 0.5f;
        const float top = pos.y - totalHeight * 0.5f;
        DrawRect titleBar{left, top, win.width, win.titleBarHeight};
        titleBars.push_back(titleBar);
        clientAreas.push_back({left, top + win.titleBarHeight, win.width, win.height});
        windowLabels.push_back({{left + 4.0f, top + 4.0f, win.width, win.titleBarHeight}, win.title});
    }
    DrawWindowBodiesBatched(clientAreas, titleBars);
    DrawLabels(textRenderer_, windowLabels, 0.0f, 12.0f);
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
    DrawParticlesBatched(backgroundTexture_, drawParticles, particleHalfSizePx_);
}

void AppController::DrawResetPhase() const {
    DrawFullscreenTexturedQuad(backgroundTexture_, screenWidth_, screenHeight_, 1.0f);

    std::vector<DrawRect> clientAreas, titleBars, iconRects;
    std::vector<DrawLabeledRect> windowLabels, iconLabels;
    for (const auto& win : layout_.windows) {
        titleBars.push_back({win.x, win.y, win.width, win.titleBarHeight});
        clientAreas.push_back({win.x, win.y + win.titleBarHeight, win.width, win.height});
        windowLabels.push_back({{win.x + 4.0f, win.y + 4.0f, win.width, win.titleBarHeight}, win.title});
    }
    for (const auto& icon : layout_.icons) {
        DrawRect rect{icon.x, icon.y, icon.width, icon.height};
        iconRects.push_back(rect);
        iconLabels.push_back({rect, icon.label});
    }
    DrawWindowBodiesBatched(clientAreas, titleBars);
    DrawLabels(textRenderer_, windowLabels, 0.0f, 12.0f);
    DrawIconBodiesBatched(iconRects);
    DrawLabels(textRenderer_, iconLabels, 0.0f, 46.0f);
}

void AppController::Draw() const {
    switch (stateMachine_.Current()) {
        case core::SaverState::STATE_ICONS:
            DrawIconsPhase();
            break;
        case core::SaverState::STATE_WINDOWS:
            DrawWindowsPhase();
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
