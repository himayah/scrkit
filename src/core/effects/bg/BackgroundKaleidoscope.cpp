#include "BackgroundKaleidoscope.h"

#include <cmath>

#include "../KaleidoscopeFold.h"

namespace core::fx {

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr int kSegmentChoices[] = {4, 6, 8};
constexpr int kRings = 16;
constexpr int kSubdiv = 4;
} // namespace

void BackgroundKaleidoscopeEffect::Begin(const EffectContext& ctx) {
    layer_ = ctx.layer;
    rng_ = core::Mt19937RandomSource(ctx.seed);
    segments_ = kSegmentChoices[static_cast<int>(rng_.NextFloat01() * 3.0f) % 3];
    wedgeOriginRad_ = rng_.NextFloat01() * 2.0f * kPi;
    radialMesh_ = MeshBuilder::BuildRadial(layer_->screenW, layer_->screenH, segments_, kRings, kSubdiv);
}

void BackgroundKaleidoscopeEffect::Step(EffectFrame& frame) {
    const float phi = 2.0f * kPi * params_.rotHz * frame.t * frame.intensity;
    const float zoom = 1.0f + frame.intensity * params_.zoomAmp * std::sin(2.0f * kPi * params_.zoomHz * frame.t);
    const Vec2 center{layer_->screenW / 2.0f, layer_->screenH / 2.0f};

    Mesh& out = frame.out->mesh;
    out.cols = radialMesh_.cols;
    out.rows = radialMesh_.rows;
    out.quads = radialMesh_.quads;
    out.quadActive = radialMesh_.quadActive;
    out.vertices = radialMesh_.vertices;

    for (auto& v : out.vertices) {
        const float dx = v.pos.x - center.x;
        const float dy = v.pos.y - center.y;
        const float r = std::sqrt(dx * dx + dy * dy);
        const float alpha = std::atan2(dy, dx);
        // §6.2.6: clamp inside FoldPoint already guards z(t)<1 sampling
        // outside the screen -- no extra bg-specific clamping needed here.
        const Vec2 src = FoldPoint(r, alpha, center, segments_, phi, zoom, wedgeOriginRad_, layer_->screenW,
                                    layer_->screenH);
        v.uv = {src.x / layer_->screenW, src.y / layer_->screenH};
    }
}

} // namespace core::fx
