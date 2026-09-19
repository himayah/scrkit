#pragma once
// Client end of the SCRAPI named-pipe transport (docs/SCRAPI_SPEC.md §3): connects to
// the pipe the viewer created, and moves JSON Lines between it and the render
// thread through two thread-safe queues. All pipe I/O happens on one background
// thread (overlapped, so it can be stopped promptly); the render thread never blocks.

#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

#include <windows.h>

namespace platform {

class ScrApiPipe {
public:
    ScrApiPipe() = default;
    ~ScrApiPipe() { Stop(); }
    ScrApiPipe(const ScrApiPipe&) = delete;
    ScrApiPipe& operator=(const ScrApiPipe&) = delete;

    // `pipeName` is the part after \\.\pipe\ . Returns immediately; the connection
    // (retried for up to `connectTimeoutMs`) is made on the I/O thread.
    void Start(const std::wstring& pipeName, DWORD connectTimeoutMs = 5000);
    void Stop();

    // Render thread: next received line (without the newline), if any.
    bool PopLine(std::string& out);
    // Render thread: queue a line for sending (the newline is added here). Non-blocking.
    void SendLine(const std::string& line);

    bool IsConnected() const { return connected_.load(); }
    // Set once the connection could not be made or was lost.
    bool IsFailed() const { return failed_.load(); }

private:
    void ThreadMain(std::wstring pipeName, DWORD connectTimeoutMs);

    std::thread thread_;
    HANDLE stopEvent_ = nullptr;  // manual-reset: Stop() requested
    HANDLE wakeEvent_ = nullptr;  // auto-reset: outgoing data queued
    std::atomic<bool> connected_{false};
    std::atomic<bool> failed_{false};
    std::mutex mutex_;
    std::deque<std::string> incoming_;
    std::deque<std::string> outgoing_;
};

} // namespace platform
