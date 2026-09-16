#include "AppController.h"

#include <algorithm>
#include <random>
#include <vector>

#include "../../core/ContentMask.h"
#include "../../core/Logger.h"
#include "../../core/WallpaperFit.h"
#include "AppPaths.h"
#include "DebugDump.h" // TEMPORARY, see its header comment
#include "OpenGLContext.h"
#include "StringConvert.h"
#include "WallpaperProvider.h"

namespace platform {

AppController::~AppController() { Shutdown(); }

bool AppController::Initialize(HDC hdc, int screenWidthPx, int screenHeightPx,
                                const core::ConfigModel& config, const std::wstring& wallpaperPath,
                                const DecodedImage* desktopCapture, bool isPreviewMode) {
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

    // Composite the wallpaper the same way Windows actually positions/
    // scales it (Fill/Fit/Stretch/Center/Tile) *before* building the visible
    // background texture below -- see git history for the full rationale.
    // When a same-size real desktop capture is available, uses the *aligned*
    // variant (core::CompositeWallpaperAligned), which searches the capture
    // for Fill/Span's actual crop position instead of assuming it's centered.
    // This one composited image is then reused below for the content diff
    // too, so the rendered background and the diff always agree on what "the
    // wallpaper" looks like.
    const bool haveMatchingCapture =
        desktopCapture && desktopCapture->width == screenWidth_ && desktopCapture->height == screenHeight_;
    DecodedImage compositedWallpaper;
    compositedWallpaper.width = screenWidth_;
    compositedWallpaper.height = screenHeight_;
    compositedWallpaper.rgba.assign(static_cast<size_t>(screenWidth_) * screenHeight_ * 4, 0);
    core::CompositeWallpaperAligned(image.rgba.data(), image.width, image.height, compositedWallpaper.rgba.data(),
                                     screenWidth_, screenHeight_, GetSystemWallpaperFitMode(), desktopR, desktopG,
                                     desktopB, haveMatchingCapture ? desktopCapture->rgba.data() : nullptr);

    backgroundTexture_ = CreateTextureFromImage(compositedWallpaper);
    if (backgroundTexture_ == 0) {
        core::Logger::Error("AppController: failed to create background texture");
        return false;
    }

    // §6.2.8.1: start generating HueShift's hue-rotated copies now, off the
    // main thread, so they're likely ready well before HueShift could ever
    // be picked (Effects.Enabled=1's first background cycle is seconds away
    // at minimum). maxRingBytes matches HueShiftParams::maxRingBytes.
    for (GLuint tex : hueRingTextures_) {
        if (tex != 0) glDeleteTextures(1, &tex);
    }
    hueRingTextures_.assign(static_cast<size_t>(kHueRingSteps - 1), 0);
    hueRingBuilder_ = std::make_unique<HueRingBuilder>();
    hueRingBuilder_->Start(compositedWallpaper.rgba, screenWidth_, screenHeight_, kHueRingSteps,
                            64ull * 1024 * 1024);

    // 2. Particle count: resolve Auto/Custom/preset into a concrete count,
    //    then lay out the NxN grid (要件.txt §5, §6). Both layers' cells
    //    come from this same grid (DESIGN_EFFECTS.md §4.1's LayerSource).
    const int autoCount = ResolveAutoParticleCountFromCurrentContext();
    resolvedParticleCount_ = config.ResolveParticleCount(autoCount);
    if (resolvedParticleCount_ < 1) resolvedParticleCount_ = 1;

    core::ParticleGridConfig gridConfig;
    gridConfig.screenWidth = static_cast<float>(screenWidth_);
    gridConfig.screenHeight = static_cast<float>(screenHeight_);
    gridConfig.gridN = core::ComputeGridDimensionForParticleCount(resolvedParticleCount_);
    gridConfig.particleCount = resolvedParticleCount_;
    particles_ = core::BuildParticleGrid(gridConfig);
    gridN_ = gridConfig.gridN;

    // Slightly oversize each particle relative to its grid cell so adjacent
    // particles overlap a hair instead of leaving hairline seams. Kept
    // per-axis (not forced square) since a grid cell isn't square on a
    // non-square screen.
    const float cellWidth = static_cast<float>(screenWidth_) / static_cast<float>(std::max(1, gridN_));
    const float cellHeight = static_cast<float>(screenHeight_) / static_cast<float>(std::max(1, gridN_));
    particleHalfWidthPx_ = cellWidth * 0.55f;
    particleHalfHeightPx_ = cellHeight * 0.55f;

    // 3. Real desktop capture + content diff (optional): a still image of
    //    the real screen, diffed cell-by-cell against the wallpaper (same
    //    grid as particles_ above) so only the cells that actually differ --
    //    real icons, the taskbar, open windows -- become foreground content.
    if (foregroundTexture_ != 0) {
        glDeleteTextures(1, &foregroundTexture_);
        foregroundTexture_ = 0;
    }
    contentParticles_.clear();
    std::vector<int> contentCellIndices;

    if (haveMatchingCapture) {
        core::ContentMaskConfig maskConfig;
        maskConfig.screenWidth = screenWidth_;
        maskConfig.screenHeight = screenHeight_;
        maskConfig.gridN = gridN_;
        const std::vector<bool> mask =
            core::ComputeContentMask(desktopCapture->rgba.data(), compositedWallpaper.rgba.data(), maskConfig);

        for (size_t i = 0; i < particles_.size() && i < mask.size(); ++i) {
            if (mask[i]) {
                contentParticles_.push_back(particles_[i]);
                contentCellIndices.push_back(static_cast<int>(i));
            }
        }

        // §7.4: the foreground texture is the capture itself with every
        // non-diff cell's alpha zeroed, not a separate "capture texture" the
        // renderer has to know how to mask at draw time (D-3).
        foregroundTexture_ = CreateMaskedTextureFromImage(*desktopCapture, mask, gridN_);
        if (foregroundTexture_ == 0) {
            core::Logger::Warn("AppController: failed to create foreground texture; content phase will be skipped");
        }
        core::Logger::Info("AppController: " + std::to_string(contentParticles_.size()) + "/" +
                            std::to_string(particles_.size()) + " grid cell(s) flagged as real desktop content");

        // TEMPORARY diagnostic (see DebugDump.h) for the reported "holes
        // inside a captured window/icon rectangle" problem: dump the actual
        // capture, the wallpaper it was compared against, and a magenta-
        // tinted overlay showing exactly which cells ComputeContentMask
        // excluded, directly on top of the real pixels -- the same
        // "look at the actual two images being compared" method that
        // resolved every earlier content-mask issue (DESIGN.md §9.8).
        {
            const std::wstring dir = GetAppDataDirectory();
            if (!dir.empty()) {
                WriteDebugBmp(dir + L"\\debug_capture.bmp", desktopCapture->rgba.data(), screenWidth_,
                              screenHeight_);
                WriteDebugBmp(dir + L"\\debug_wallpaper.bmp", compositedWallpaper.rgba.data(), screenWidth_,
                              screenHeight_);
                const std::vector<uint8_t> overlay = core::BuildDiffOverlayRgba(
                    desktopCapture->rgba.data(), screenWidth_, screenHeight_, mask, gridN_, 255, 0, 255);
                WriteDebugBmp(dir + L"\\debug_mask_overlay.bmp", overlay.data(), screenWidth_, screenHeight_);
                core::Logger::Info("AppController: wrote debug_capture.bmp / debug_wallpaper.bmp / "
                                    "debug_mask_overlay.bmp (magenta = excluded cells) to " +
                                    WideToUtf8(dir));
            }
        }
    } else if (desktopCapture) {
        core::Logger::Warn("AppController: desktop capture size (" + std::to_string(desktopCapture->width) +
                            "x" + std::to_string(desktopCapture->height) + ") does not match screen (" +
                            std::to_string(screenWidth_) + "x" + std::to_string(screenHeight_) +
                            "); content phase will be skipped");
    }

    // Whether the foreground layer has anything to show, and if not, why
    // (DESIGN_EFFECTS.md §4.1/§5.3, D-15): a genuine 0-diff result is treated
    // the same as a capture failure for timing purposes (both should skip
    // fast in fullscreen mode, matching v1's existing behavior of an
    // instantly-AllDead empty spiral array) -- the PreviewMode/CaptureFailed
    // split only matters for /p vs /s, not for "why did content end up empty".
    const bool hasForegroundContent = haveMatchingCapture && foregroundTexture_ != 0 && !contentParticles_.empty();
    foregroundEmptyReason_ = hasForegroundContent
                                  ? core::fx::EmptyReason::NotEmpty
                                  : (isPreviewMode ? core::fx::EmptyReason::PreviewMode
                                                   : core::fx::EmptyReason::CaptureFailed);

    // 4. Suction center random walk (要件.txt §4: 速度は一定, 1〜3px/frame).
    rng_ = std::make_unique<core::Mt19937RandomSource>(std::random_device{}());
    core::WalkerBounds bounds{0.0f, 0.0f, static_cast<float>(screenWidth_), static_cast<float>(screenHeight_)};
    core::Vec2 startCenter{screenWidth_ * 0.5f, screenHeight_ * 0.5f};
    center_ = std::make_unique<core::SuctionCenterWalker>(startCenter, bounds, 2.0f);

    // 5. Effect engine (DESIGN_EFFECTS.md §2, §16 Step 8): built after rng_
    //    since it shares that same RNG for scheduling (D-11 -- distinct from
    //    each individual effect's own private per-instance RNG).
    core::fx::LayerSource foregroundLayer;
    foregroundLayer.kind = core::fx::LayerKind::Foreground;
    foregroundLayer.texture = core::fx::TextureRole::Foreground;
    foregroundLayer.screenW = static_cast<float>(screenWidth_);
    foregroundLayer.screenH = static_cast<float>(screenHeight_);
    foregroundLayer.gridN = gridN_;
    foregroundLayer.cellIndices = contentCellIndices;
    foregroundLayer.cells = contentParticles_;
    foregroundLayer.cellHalfW = particleHalfWidthPx_;
    foregroundLayer.cellHalfH = particleHalfHeightPx_;
    foregroundLayer.empty = !hasForegroundContent;
    foregroundLayer.emptyReason = foregroundEmptyReason_;

    core::fx::LayerSource backgroundLayer;
    backgroundLayer.kind = core::fx::LayerKind::Background;
    backgroundLayer.texture = core::fx::TextureRole::Background;
    backgroundLayer.screenW = static_cast<float>(screenWidth_);
    backgroundLayer.screenH = static_cast<float>(screenHeight_);
    backgroundLayer.gridN = gridN_;
    backgroundLayer.cellIndices.resize(particles_.size());
    for (size_t i = 0; i < particles_.size(); ++i) backgroundLayer.cellIndices[i] = static_cast<int>(i);
    backgroundLayer.cells = particles_;
    backgroundLayer.cellHalfW = particleHalfWidthPx_;
    backgroundLayer.cellHalfH = particleHalfHeightPx_;

    effectEngine_ = std::make_unique<core::fx::EffectEngine>(config.effects, *rng_);
    effectEngine_->SetLayers(std::move(foregroundLayer), std::move(backgroundLayer));

    stateMachine_ = core::SaverStateMachine(core::SaverState::STATE_CONTENT);
    blackHoldTimer_ = resetHoldTimer_ = 0.0f;
    fade_.Reset();
    OnStateEntered(core::SaverState::STATE_CONTENT); // kick off both layers' effect state machines

    core::Logger::Info("AppController: initialized (" + std::to_string(resolvedParticleCount_) +
                        " particles, " + std::to_string(contentParticles_.size()) + " content cell(s))");
    return true;
}

void AppController::Shutdown() {
    effectEngine_.reset();
    // §6.2.8.1: stop/join the worker before tearing down anything it might
    // still be about to hand a buffer to (its destructor does this too, but
    // doing it explicitly here keeps shutdown ordering obvious and lets the
    // ring textures below be deleted only once the thread is truly done).
    hueRingBuilder_.reset();
    for (GLuint& tex : hueRingTextures_) {
        if (tex != 0) {
            glDeleteTextures(1, &tex);
            tex = 0;
        }
    }
    if (backgroundTexture_ != 0) {
        glDeleteTextures(1, &backgroundTexture_);
        backgroundTexture_ = 0;
    }
    if (foregroundTexture_ != 0) {
        glDeleteTextures(1, &foregroundTexture_);
        foregroundTexture_ = 0;
    }
}

void AppController::OnStateEntered(core::SaverState newState) {
    if (effectEngine_) effectEngine_->OnPhaseEntered(newState);
    switch (newState) {
        case core::SaverState::STATE_BLACK:
            blackHoldTimer_ = 0.0f;
            break;
        case core::SaverState::STATE_FADE:
            fade_.Reset();
            break;
        case core::SaverState::STATE_RESET:
            resetHoldTimer_ = 0.0f;
            break;
        default:
            break;
    }
}

void AppController::Update(float dtSeconds) {
    if (!center_ || !rng_ || !effectEngine_) return;

    center_->Step(*rng_);
    const core::Vec2 centerPos = center_->Position();

    // §7.4: upload at most one completed hue ring to a GL texture per frame
    // (GL calls stay main-thread-only; the worker only ever produces RGBA
    // buffers). HueShift is excluded from the scheduler (§5.5) for as long
    // as hueShiftReady below stays false.
    if (hueRingBuilder_) {
        std::vector<uint8_t> ringRgba;
        int ringIndex = 0, ringW = 0, ringH = 0;
        if (hueRingBuilder_->TryTakeNextRing(ringRgba, ringIndex, ringW, ringH)) {
            const size_t slot = static_cast<size_t>(ringIndex - 1);
            if (slot < hueRingTextures_.size()) {
                if (hueRingTextures_[slot] != 0) glDeleteTextures(1, &hueRingTextures_[slot]);
                hueRingTextures_[slot] = CreateTextureFromRgba(ringW, ringH, ringRgba.data());
            }
        }
    }

    core::fx::EffectEngine::Inputs fxIn;
    fxIn.dt = dtSeconds;
    fxIn.suctionCenter = {centerPos.x, centerPos.y};
    fxIn.hueShiftReady = hueRingBuilder_ && !hueRingBuilder_->IsRunning();
    const core::fx::EffectEngine::Outputs fxOut = effectEngine_->Update(fxIn);

    core::StateMachineInputs inputs;
    switch (stateMachine_.Current()) {
        case core::SaverState::STATE_CONTENT:
            inputs.allContentConsumed = fxOut.foregroundConsumed;
            break;
        case core::SaverState::STATE_BACKGROUND:
            inputs.allParticlesConsumed = fxOut.backgroundConsumed;
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
        OnStateEntered(stateMachine_.Current());
    }
}

EffectTextureTable AppController::BuildTextureTable() const {
    EffectTextureTable table;
    table.background = backgroundTexture_;
    table.foreground = foregroundTexture_;
    table.hueRings = hueRingTextures_;
    return table;
}

void AppController::Draw() const {
    switch (stateMachine_.Current()) {
        case core::SaverState::STATE_CONTENT:
        case core::SaverState::STATE_BACKGROUND:
        case core::SaverState::STATE_RESET:
            ClearBlack();
            if (effectEngine_) ExecuteDrawList(effectEngine_->DrawList(), BuildTextureTable());
            break;
        case core::SaverState::STATE_BLACK:
            ClearBlack();
            break;
        case core::SaverState::STATE_FADE:
            ClearBlack();
            DrawFullscreenTexturedQuad(backgroundTexture_, screenWidth_, screenHeight_, fade_.Alpha());
            break;
    }
}

} // namespace platform
