#pragma once
// Minimal, sink-injectable logger (design doc: ロギング方針).
// Kept platform-independent: production code supplies a file-writing sink
// (platform/win32/FileLogSink.*), tests supply an in-memory sink.

#include <functional>
#include <string>

namespace core {

enum class LogLevel { Info, Warn, Error };

using LogSink = std::function<void(LogLevel level, const std::string& message)>;

class Logger {
public:
    // Installs the sink that receives all subsequent log calls. Passing an
    // empty std::function disables logging (the default).
    static void SetSink(LogSink sink) { Instance().sink_ = std::move(sink); }

    static void Info(const std::string& message) { Log(LogLevel::Info, message); }
    static void Warn(const std::string& message) { Log(LogLevel::Warn, message); }
    static void Error(const std::string& message) { Log(LogLevel::Error, message); }

    static void Log(LogLevel level, const std::string& message) {
        auto& self = Instance();
        if (self.sink_) {
            self.sink_(level, message);
        }
    }

private:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    LogSink sink_;
};

inline const char* LogLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::Info: return "INFO";
        case LogLevel::Warn: return "WARN";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

} // namespace core
