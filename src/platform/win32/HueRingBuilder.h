#pragma once
// Generates HueShift's (K-1) hue-rotated background copies on a worker
// thread (DESIGN_EFFECTS.md §6.2.8.1). The only std::thread this project
// creates; owned and driven entirely by AppController.

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace platform {

class HueRingBuilder {
public:
    HueRingBuilder() = default;
    ~HueRingBuilder(); // Stop(); Join(); before members are destroyed

    HueRingBuilder(const HueRingBuilder&) = delete;
    HueRingBuilder& operator=(const HueRingBuilder&) = delete;

    // Starts generating rings 1..ringCount-1 (ring 0 is the background
    // itself, never generated -- §6.2.8/A.6.2.8.1) from a private copy of
    // backgroundRgbaCopy (w x h RGBA8), so it stays valid even if the
    // caller's own buffer changes concurrently. Downsamples once by 2x if
    // the estimated total size would exceed maxRingBytes. Any thread still
    // running from a previous Start() is stopped and joined first.
    void Start(std::vector<uint8_t> backgroundRgbaCopy, int w, int h, int ringCount, uint64_t maxRingBytes);

    // Non-blocking: requests the worker stop generating further rings.
    void Stop();
    // Blocks until the worker thread has exited (call after Stop()).
    void Join();
    bool IsRunning() const { return running_; }

    // Pops one completed ring, in generation order, if any is ready.
    // outWidth/outHeight may differ from the original w/h if downsampled.
    bool TryTakeNextRing(std::vector<uint8_t>& outRgba, int& outIndex, int& outWidth, int& outHeight);

private:
    void Run(std::vector<uint8_t> background, int w, int h, int ringCount, uint64_t maxRingBytes);

    std::thread thread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};

    struct CompletedRing {
        std::vector<uint8_t> rgba;
        int index = 0;
        int width = 0, height = 0;
    };
    std::mutex mutex_;
    std::deque<CompletedRing> completed_;
};

} // namespace platform
