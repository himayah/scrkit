#include "HueRingBuilder.h"

#include <algorithm>

#include "../../core/effects/HueRotate.h"

namespace platform {

namespace {
// Simple 2x2 box downsample (RGBA8). A 1px loss on an odd trailing row/col
// is invisible on a hue-cycling background.
void DownsampleHalf(const std::vector<uint8_t>& src, int w, int h, std::vector<uint8_t>& dst, int& outW,
                     int& outH) {
    outW = std::max(1, w / 2);
    outH = std::max(1, h / 2);
    dst.assign(static_cast<size_t>(outW) * outH * 4, 0);
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            const int sx = x * 2, sy = y * 2;
            const int sy2 = std::min(sy + 1, h - 1);
            const int sx2 = std::min(sx + 1, w - 1);
            for (int c = 0; c < 4; ++c) {
                const int sum = src[(static_cast<size_t>(sy) * w + sx) * 4 + c] +
                                src[(static_cast<size_t>(sy) * w + sx2) * 4 + c] +
                                src[(static_cast<size_t>(sy2) * w + sx) * 4 + c] +
                                src[(static_cast<size_t>(sy2) * w + sx2) * 4 + c];
                dst[(static_cast<size_t>(y) * outW + x) * 4 + c] = static_cast<uint8_t>(sum / 4);
            }
        }
    }
}
} // namespace

HueRingBuilder::~HueRingBuilder() {
    Stop();
    Join();
}

void HueRingBuilder::Start(std::vector<uint8_t> backgroundRgbaCopy, int w, int h, int ringCount,
                            uint64_t maxRingBytes) {
    Stop();
    Join();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        completed_.clear();
    }
    stopRequested_ = false;
    running_ = true;
    thread_ = std::thread(&HueRingBuilder::Run, this, std::move(backgroundRgbaCopy), w, h, ringCount, maxRingBytes);
}

void HueRingBuilder::Stop() { stopRequested_ = true; }

void HueRingBuilder::Join() {
    if (thread_.joinable()) thread_.join();
}

bool HueRingBuilder::TryTakeNextRing(std::vector<uint8_t>& outRgba, int& outIndex, int& outWidth, int& outHeight) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (completed_.empty()) return false;
    CompletedRing ring = std::move(completed_.front());
    completed_.pop_front();
    outRgba = std::move(ring.rgba);
    outIndex = ring.index;
    outWidth = ring.width;
    outHeight = ring.height;
    return true;
}

void HueRingBuilder::Run(std::vector<uint8_t> background, int w, int h, int ringCount, uint64_t maxRingBytes) {
    int genW = w, genH = h;
    std::vector<uint8_t> genSource = std::move(background);

    if (ringCount > 1) {
        const uint64_t estimatedBytes = static_cast<uint64_t>(ringCount - 1) * static_cast<uint64_t>(w) *
                                         static_cast<uint64_t>(h) * 4ull;
        if (estimatedBytes > maxRingBytes) {
            std::vector<uint8_t> half;
            int hw = 0, hh = 0;
            DownsampleHalf(genSource, genW, genH, half, hw, hh);
            genSource = std::move(half);
            genW = hw;
            genH = hh;
        }
    }

    for (int k = 1; k < ringCount; ++k) {
        if (stopRequested_) break;
        std::vector<uint8_t> ring(static_cast<size_t>(genW) * genH * 4);
        core::fx::HueRotateRgba(genSource.data(), ring.data(), genW, genH, 360.0f * static_cast<float>(k) / ringCount);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            completed_.push_back({std::move(ring), k, genW, genH});
        }
    }
    running_ = false;
}

} // namespace platform
