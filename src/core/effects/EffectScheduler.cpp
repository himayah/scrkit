#include "EffectScheduler.h"

#include <algorithm>
#include <set>

#include "EffectCatalog.h"

namespace core::fx {

namespace {

const EffectParams& ParamsFor(const LayerEffectConfig& config, EffectId id) {
    static const EffectParams kDefault;
    auto it = config.perEffect.find(id);
    return it != config.perEffect.end() ? it->second : kDefault;
}

// §9.2 / §5.5 D-12 & 外部レビュー ISSUE-8: 1. either bound is 0 -> layer
// default; 2. otherwise Min > Max -> layer default too; evaluated in that
// order so "0 and Min>Max at once" still resolves to a single well-defined
// outcome.
void ResolveDuration(const EffectParams& params, const LayerEffectConfig& layerConfig, float& outMin,
                      float& outMax) {
    if (params.minSeconds == 0.0f || params.maxSeconds == 0.0f) {
        outMin = layerConfig.defaultMinSeconds;
        outMax = layerConfig.defaultMaxSeconds;
    } else if (params.minSeconds > params.maxSeconds) {
        outMin = layerConfig.defaultMinSeconds;
        outMax = layerConfig.defaultMaxSeconds;
    } else {
        outMin = params.minSeconds;
        outMax = params.maxSeconds;
    }
}

const LayerEffectConfig& LayerConfigFor(const EngineConfig& engine, LayerKind layer) {
    return layer == LayerKind::Foreground ? engine.foreground : engine.background;
}

const std::vector<EffectId>& ScriptedFor(const EngineConfig& engine, LayerKind layer) {
    return layer == LayerKind::Foreground ? engine.scriptedForeground : engine.scriptedBackground;
}

bool CandidateAllowed(EffectId id, const LayerEffectConfig& layerConfig, bool hueShiftReady) {
    if (id == EffectId::HueShift && !hueShiftReady) return false;
    return ParamsFor(layerConfig, id).enabled;
}

EffectPick MakePick(EffectId id, const LayerEffectConfig& layerConfig, core::IRandomSource& rng) {
    float minS, maxS;
    ResolveDuration(ParamsFor(layerConfig, id), layerConfig, minS, maxS);
    const float duration = minS + rng.NextFloat01() * (maxS - minS);
    return EffectPick{id, duration, rng.NextUInt32()};
}

// The script cursor isn't stored anywhere explicitly (Pick stays a pure
// function of its arguments): it's inferred from the layer's own history --
// the next scripted entry is the one after whichever scripted id was most
// recently played on this layer (or index 0 if none has been, or the last
// play wasn't from the script). This also makes the cycle naturally resume
// at the right place across STATE_RESET->STATE_CONTENT cycles, since the
// Timeline isn't cleared between them.
std::optional<EffectPick> PickScripted(const std::vector<EffectId>& scripted, EffectKind kind, LayerKind layer,
                                        const LayerEffectConfig& layerConfig,
                                        const std::deque<TimelineEntry>& history, core::IRandomSource& rng) {
    size_t nextIndex = 0;
    if (!history.empty()) {
        const EffectId lastId = history.back().effectId;
        for (size_t i = 0; i < scripted.size(); ++i) {
            if (scripted[i] == lastId) {
                nextIndex = (i + 1) % scripted.size();
                break;
            }
        }
    }
    const EffectId candidate = scripted[nextIndex];
    const auto& kindCatalog = CatalogFor(layer, kind);
    if (std::find(kindCatalog.begin(), kindCatalog.end(), candidate) == kindCatalog.end()) {
        return std::nullopt; // next scripted entry isn't this kind yet -- let NoCandidatePolicy handle it
    }
    return MakePick(candidate, layerConfig, rng);
}

} // namespace

namespace EffectScheduler {

std::optional<EffectPick> Pick(EffectKind kind, LayerKind layer, const EngineConfig& engine,
                                const std::deque<TimelineEntry>& history, core::IRandomSource& rng,
                                bool hueShiftReady) {
    if (!engine.enabled) return std::nullopt;

    const LayerEffectConfig& layerConfig = LayerConfigFor(engine, layer);
    const std::vector<EffectId>& scripted = ScriptedFor(engine, layer);
    if (!scripted.empty()) return PickScripted(scripted, kind, layer, layerConfig, history, rng);

    std::vector<EffectId> candidates;
    for (EffectId id : CatalogFor(layer, kind)) {
        if (CandidateAllowed(id, layerConfig, hueShiftReady)) candidates.push_back(id);
    }
    if (candidates.empty()) return std::nullopt;

    const size_t excludeCount = std::min(static_cast<size_t>(3), candidates.size() - 1);
    std::set<EffectId> excluded;
    for (auto it = history.rbegin(); it != history.rend() && excluded.size() < excludeCount; ++it) {
        excluded.insert(it->effectId);
    }

    std::vector<EffectId> filtered;
    for (EffectId id : candidates) {
        if (excluded.find(id) == excluded.end()) filtered.push_back(id);
    }
    if (filtered.empty()) filtered = candidates; // defensive; the excludeCount formula should prevent this

    float totalWeight = 0.0f;
    for (EffectId id : filtered) totalWeight += ParamsFor(layerConfig, id).weight;
    if (totalWeight <= 0.0f) return MakePick(filtered.front(), layerConfig, rng);

    float r = rng.NextFloat01() * totalWeight;
    for (EffectId id : filtered) {
        r -= ParamsFor(layerConfig, id).weight;
        if (r <= 0.0f) return MakePick(id, layerConfig, rng);
    }
    return MakePick(filtered.back(), layerConfig, rng);
}

} // namespace EffectScheduler

} // namespace core::fx
