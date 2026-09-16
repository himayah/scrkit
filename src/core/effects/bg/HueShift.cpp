#include "HueShift.h"

#include <cmath>

namespace core::fx {

HueRingSelection SelectHueRings(float hueDegrees, int steps) {
    const float stepDeg = 360.0f / static_cast<float>(steps);
    float h = std::fmod(hueDegrees, 360.0f);
    if (h < 0.0f) h += 360.0f;

    const int k = static_cast<int>(h / stepDeg) % steps;
    const float f = (h - static_cast<float>(k) * stepDeg) / stepDeg;
    return {k, (k + 1) % steps, f};
}

void HueShiftEffect::Begin(const EffectContext&) {}

void HueShiftEffect::Step(EffectFrame& frame) {
    const float h = std::fmod(frame.intensity * 360.0f * (frame.t / params_.cycleSec), 360.0f);
    const HueRingSelection sel = SelectHueRings(h, params_.steps);

    frame.out->transform = Transform2D{}; // identity: this is a pure texture blend, no geometry moves
    frame.out->alpha = 1.0f;
    frame.out->textureIndex = sel.indexA;
    frame.out->textureIndexB = sel.indexB;
    frame.out->alphaB = sel.blendB;
}

} // namespace core::fx
