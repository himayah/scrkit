#include "AppController.h"

#include <algorithm>
#include <random>
#include <vector>

#include "../../core/ContentMask.h"
#include "../../core/Logger.h"
#include "../../core/WallpaperFit.h"
#include "AppPaths.h"
#include "OpenGLContext.h"
#include "Renderer.h"
#include "ScreenCapture.h"
#include "WallpaperProvider.h"

namespace platform {

namespace {

bool AllDead(const std::vector<core::SpiralState>& states) {
    return std::all_of(states.begin(), states.end(), [](const core::SpiralState& s) { return !s.alive; });
}

// Diagnostic helper (temporary -- chasing the compositedWallpaper-too-dark
// bug, see docs/TODO / spiral-saver-open-work memory): sparse-sampled
// average brightness, same technique as the earlier startup-blackout
// diagnostics (see history around 8579506/a539536), reused here to compare
// the *raw decoded* wallpaper image against the *composited* reference and
// the real capture, so a decode-time darkening can be told apart from a
// compositing/scale-math bug instead of guessing from one combined number.
int SampledAvgBrightness(const uint8_t* rgba, size_t pixelCount) {
    if (pixelCount == 0) return 0;
    unsigned long long sum = 0;
    size_t sampled = 0;
    for (size_t i = 0; i < pixelCount; i += 97, ++sampled) {
        const uint8_t* p = rgba + i * 4;
        sum += p[0] + p[1] + p[2];
    }
    return sampled == 0 ? 0 : static_cast<int>(sum / (sampled * 3));
}

// Diagnostic helper (temporary, see SaveDebugImages below): writes `image`
// as an uncompressed 24-bit BMP -- no WIC encoder round-trip needed, and
// viewable with literally anything (Paint, Photos, a browser tab).
bool SaveDebugBmp(const std::wstring& path, const DecodedImage& image) {
    if (image.width <= 0 || image.height <= 0) return false;
    const int width = image.width;
    const int height = image.height;
    const int rowSize = ((width * 3 + 3) / 4) * 4; // rows padded to a 4-byte boundary
    const DWORD pixelDataSize = static_cast<DWORD>(rowSize) * static_cast<DWORD>(height);

    BITMAPFILEHEADER fileHeader{};
    fileHeader.bfType = 0x4D42; // 'BM'
    fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
    fileHeader.bfSize = fileHeader.bfOffBits + pixelDataSize;

    BITMAPINFOHEADER infoHeader{};
    infoHeader.biSize = sizeof(BITMAPINFOHEADER);
    infoHeader.biWidth = width;
    infoHeader.biHeight = -height; // negative = top-down, matches our RGBA row order
    infoHeader.biPlanes = 1;
    infoHeader.biBitCount = 24;
    infoHeader.biCompression = BI_RGB;
    infoHeader.biSizeImage = pixelDataSize;

    std::vector<uint8_t> pixelData(pixelDataSize, 0);
    for (int y = 0; y < height; ++y) {
        const uint8_t* srcRow = image.rgba.data() + static_cast<size_t>(y) * width * 4;
        uint8_t* dstRow = pixelData.data() + static_cast<size_t>(y) * rowSize;
        for (int x = 0; x < width; ++x) {
            dstRow[x * 3 + 0] = srcRow[x * 4 + 2]; // B
            dstRow[x * 3 + 1] = srcRow[x * 4 + 1]; // G
            dstRow[x * 3 + 2] = srcRow[x * 4 + 0]; // R
        }
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, &fileHeader, sizeof(fileHeader), &written, nullptr) && written == sizeof(fileHeader);
    if (ok) {
        ok = WriteFile(file, &infoHeader, sizeof(infoHeader), &written, nullptr) && written == sizeof(infoHeader);
    }
    if (ok) {
        ok = WriteFile(file, pixelData.data(), static_cast<DWORD>(pixelData.size()), &written, nullptr) &&
             written == pixelData.size();
    }
    CloseHandle(file);
    return ok;
}

// Diagnostic (temporary -- chasing the compositedWallpaper-too-dark bug, see
// docs/TODO / spiral-saver-open-work memory): the brightness-gain correction
// in core::ComputeContentMask brought the *average* sampled brightness of
// the capture and compositedWallpaper very close together on the real
// machine, yet ~92% of cells were still flagged as content -- meaning large
// per-pixel differences remain even where the overall averages now match.
// That could be a non-uniform (non-linear) tone curve that a single flat
// gain can't correct, or it could mean the two images don't actually
// correspond to the same picture/crop at all. Saving both side by side lets
// that be seen directly instead of guessing from summary numbers alone.
//
// Saved at full screen resolution, NOT downscaled: an earlier version of
// this function capped the output at 1280px on the long edge for easier
// viewing, but that downscale (via core::ResampleRgba's nearest-neighbor
// sampling) measurably smooths over exactly the fine per-pixel texture
// noise this diagnostic exists to quantify -- a from-the-images fraction-
// flagged calculation came out at 18.9% while the real run's own log line
// (computed by core::ComputeContentMask on the actual full-resolution
// buffers) said 46.5% for the very same frame. Any offline pixel analysis
// of these files needs to match what the real algorithm actually sees.
void SaveDebugImages(int screenWidth, int screenHeight, const DecodedImage& capture,
                      const DecodedImage& wallpaper) {
    const std::wstring dir = GetAppDataDirectory();
    if (dir.empty()) {
        core::Logger::Warn("SaveDebugImages: could not resolve %APPDATA%/SpiralSuctionSaver");
        return;
    }

    const int outW = screenWidth;
    const int outH = screenHeight;

    DecodedImage smallCapture;
    smallCapture.width = outW;
    smallCapture.height = outH;
    smallCapture.rgba.assign(static_cast<size_t>(outW) * outH * 4, 0);
    core::ResampleRgba(capture.rgba.data(), capture.width, capture.height, smallCapture.rgba.data(), outW, outH);

    DecodedImage smallWallpaper;
    smallWallpaper.width = outW;
    smallWallpaper.height = outH;
    smallWallpaper.rgba.assign(static_cast<size_t>(outW) * outH * 4, 0);
    core::ResampleRgba(wallpaper.rgba.data(), wallpaper.width, wallpaper.height, smallWallpaper.rgba.data(), outW,
                        outH);

    const bool capOk = SaveDebugBmp(dir + L"\\debug_capture.bmp", smallCapture);
    const bool wpOk = SaveDebugBmp(dir + L"\\debug_wallpaper.bmp", smallWallpaper);
    core::Logger::Info("SaveDebugImages: wrote debug_capture.bmp=" + std::string(capOk ? "ok" : "FAILED") +
                        ", debug_wallpaper.bmp=" + std::string(wpOk ? "ok" : "FAILED") + " (" +
                        std::to_string(outW) + "x" + std::to_string(outH) + ") to %APPDATA%/SpiralSuctionSaver");
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
    // Diagnostic (temporary, see SampledAvgBrightness above): the raw
    // decoded wallpaper's own brightness, before any compositing math runs
    // on it -- isolates a WIC decode-time darkening (e.g. an embedded color
    // profile or gamma mismatch) from a bug in CompositeWallpaper itself.
    core::Logger::Info(
        "AppController: decoded wallpaper " + std::to_string(image.width) + "x" + std::to_string(image.height) +
        ", sampled avg brightness=" + std::to_string(SampledAvgBrightness(image.rgba.data(), image.rgba.size() / 4)) +
        " (/255)");

    // Composite the wallpaper the same way Windows actually positions/
    // scales it (Fill/Fit/Stretch/Center/Tile) *before* building the visible
    // background texture below -- stretching the raw decoded file across
    // the whole screen (what this code used to do here) only matches the
    // "Stretch" style; every other style (Fill, the Windows 10/11 default)
    // scales and crops instead, so a plain stretch left the rendered
    // background visibly shifted/distorted compared to the real desktop
    // wallpaper shown just before the saver started (user feedback: "起動前
    // の背景表示と起動後の...背景画像は明らかにずれている"). When a same-
    // size real desktop capture is available, uses the *aligned* variant
    // (core::CompositeWallpaperAligned), which searches the capture for
    // Fill/Span's actual crop position instead of assuming it's centered
    // (see its own doc comment). This one composited image is then reused
    // below for the content diff too, so the rendered background and the
    // diff always agree on what "the wallpaper" looks like.
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

    if (haveMatchingCapture) {
        captureTexture_ = CreateTextureFromImage(*desktopCapture);
        if (captureTexture_ != 0) {
            // Reuses the same compositedWallpaper built above for the
            // visible background -- see the comment there.
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
            // Diagnostic (temporary, see SampledAvgBrightness above): compares
            // the real capture against the *composited* wallpaper reference --
            // if this gap is as large as the decoded-image-vs-capture gap
            // logged above, the darkening happens in CompositeWallpaper's
            // scale/crop math (or the fit mode it was given); if it's much
            // smaller, the decode step itself is where the image went dark.
            const size_t pixelCount = compositedWallpaper.rgba.size() / 4;
            core::Logger::Info(
                "AppController::Initialize: sampled avg brightness -- capture=" +
                std::to_string(SampledAvgBrightness(desktopCapture->rgba.data(), pixelCount)) +
                ", compositedWallpaper=" +
                std::to_string(SampledAvgBrightness(compositedWallpaper.rgba.data(), pixelCount)) + " (/255)");
            // Diagnostic (temporary, see ScreenCapture.h): if the capture is
            // far brighter than both the decoded wallpaper and the composited
            // reference above, while advancedColorEnabled below comes back
            // true, that confirms Windows is tone-mapping the SDR desktop
            // brighter for an HDR display -- something a plain file decode
            // can never reproduce -- rather than a bug in this code's own
            // decode or compositing math.
            LogDisplayColorInfo();
            // Diagnostic (temporary, see SaveDebugImages above): a visual
            // side-by-side of what's actually being diffed, to see directly
            // whether the remaining mismatch (after brightness-gain
            // correction) is a tone-curve shape difference or something more
            // structural (wrong crop/image entirely).
            SaveDebugImages(screenWidth_, screenHeight_, *desktopCapture, compositedWallpaper);
        } else {
            core::Logger::Warn("AppController: failed to create desktop capture texture; content phase will be skipped");
        }
    } else if (desktopCapture) {
        core::Logger::Warn("AppController: desktop capture size (" + std::to_string(desktopCapture->width) +
                            "x" + std::to_string(desktopCapture->height) + ") does not match screen (" +
                            std::to_string(screenWidth_) + "x" + std::to_string(screenHeight_) +
                            "); content phase will be skipped");
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
