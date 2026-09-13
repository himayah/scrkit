#pragma once
// Minimal QueryPerformanceCounter-based stopwatch, used to log where
// startup time actually goes (temporary-feeling diagnostic, but cheap
// enough and generically useful to keep around rather than rip out --
// see the RunFullScreenSaver/RunMessageLoop/AppController::Initialize
// checkpoints that use it, added after user feedback reported a ~2s black
// screen at startup that earlier fixes did not resolve).

#include <string>

#include <windows.h>

namespace platform {

class PerfTimer {
public:
    PerfTimer() { Reset(); }

    void Reset() {
        QueryPerformanceFrequency(&freq_);
        QueryPerformanceCounter(&start_);
    }

    double ElapsedMs() const {
        if (freq_.QuadPart == 0) return 0.0;
        LARGE_INTEGER now{};
        QueryPerformanceCounter(&now);
        return static_cast<double>(now.QuadPart - start_.QuadPart) * 1000.0 /
               static_cast<double>(freq_.QuadPart);
    }

    // Elapsed time as a whole-millisecond string, for embedding in log messages.
    std::string ElapsedMsString() const { return std::to_string(static_cast<long long>(ElapsedMs())); }

private:
    LARGE_INTEGER freq_{};
    LARGE_INTEGER start_{};
};

} // namespace platform
