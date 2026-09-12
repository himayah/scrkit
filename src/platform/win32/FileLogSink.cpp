#include "FileLogSink.h"

#include <ctime>
#include <sstream>

#include <windows.h>

#include "../../core/Logger.h"
#include "AppPaths.h"
#include "WinFileIO.h"

namespace platform {

namespace {
constexpr long long kMaxLogBytes = 1024LL * 1024; // 1MB, per design doc
}

void InstallFileLogSink() {
    const std::wstring path = GetLogFilePath();
    if (path.empty()) {
        return; // no APPDATA available; logging is best-effort only
    }

    core::Logger::SetSink([path](core::LogLevel level, const std::string& message) {
        const long long size = GetFileSizeW(path);
        if (size >= kMaxLogBytes) {
            WriteTextFileW(path, ""); // rotate: start a fresh log file
        }

        // NOTE: not thread-safe, but the saver only ever logs from its single
        // main thread, so std::localtime (rather than the MSVC-only
        // localtime_s / POSIX-only localtime_r) keeps this portable across
        // MSVC and MinGW-w64 without extra ifdefs.
        std::time_t now = std::time(nullptr);
        const std::tm* tmPtr = std::localtime(&now);
        char timeBuf[32] = {};
        if (tmPtr) {
            std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d %H:%M:%S", tmPtr);
        }

        std::ostringstream line;
        line << "[" << timeBuf << "] [" << core::LogLevelToString(level) << "] " << message << "\r\n";
        AppendTextFileW(path, line.str());
    });
}

} // namespace platform
