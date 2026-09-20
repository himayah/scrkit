#pragma once
// Installs a core::Logger sink that appends timestamped lines to
// %APPDATA%/ScrKit/saver.log, rotating (truncating) once the
// file exceeds ~1MB (design doc: ロギング方針).

namespace platform {

void InstallFileLogSink();

} // namespace platform
