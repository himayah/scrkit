#include "ScrApiPipe.h"

#include <vector>

#include "../../core/Logger.h"
#include "../../scrapi/Protocol.h"

namespace platform {

namespace {

// Opens the viewer's pipe, retrying until it exists/is free or we time out / are asked to stop.
HANDLE ConnectWithRetry(const std::wstring& pipeName, DWORD timeoutMs, HANDLE stopEvent) {
    const std::wstring path = L"\\\\.\\pipe\\" + pipeName;
    const DWORD start = GetTickCount();
    for (;;) {
        HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING,
                               FILE_FLAG_OVERLAPPED, nullptr);
        if (h != INVALID_HANDLE_VALUE) return h;
        if (GetTickCount() - start >= timeoutMs) return INVALID_HANDLE_VALUE;
        if (WaitForSingleObject(stopEvent, 50) == WAIT_OBJECT_0) return INVALID_HANDLE_VALUE;
    }
}

} // namespace

void ScrApiPipe::Start(const std::wstring& pipeName, DWORD connectTimeoutMs) {
    if (thread_.joinable()) return;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this, pipeName, connectTimeoutMs] { ThreadMain(pipeName, connectTimeoutMs); });
}

void ScrApiPipe::Stop() {
    if (thread_.joinable()) {
        SetEvent(stopEvent_);
        thread_.join();
    }
    if (serverPipe_ != INVALID_HANDLE_VALUE) {
        CloseHandle(serverPipe_);
        serverPipe_ = INVALID_HANDLE_VALUE;
    }
    if (stopEvent_) {
        CloseHandle(stopEvent_);
        stopEvent_ = nullptr;
    }
    if (wakeEvent_) {
        CloseHandle(wakeEvent_);
        wakeEvent_ = nullptr;
    }
}

bool ScrApiPipe::PopLine(std::string& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (incoming_.empty()) return false;
    out = std::move(incoming_.front());
    incoming_.pop_front();
    return true;
}

void ScrApiPipe::SendLine(const std::string& line) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        outgoing_.push_back(line);
    }
    if (wakeEvent_) SetEvent(wakeEvent_);
}

void ScrApiPipe::ThreadMain(std::wstring pipeName, DWORD connectTimeoutMs) {
    HANDLE pipe = INVALID_HANDLE_VALUE;
    if (serverPipe_ != INVALID_HANDLE_VALUE) {
        // Server mode: wait for the saver to connect to the pipe we already created.
        pipe = serverPipe_;
        serverPipe_ = INVALID_HANDLE_VALUE;
        HANDLE connectEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        OVERLAPPED ov{};
        ov.hEvent = connectEvent;
        bool connected = false;
        if (ConnectNamedPipe(pipe, &ov)) {
            connected = true;
        } else {
            const DWORD error = GetLastError();
            if (error == ERROR_PIPE_CONNECTED) {
                connected = true; // the client got in between CreateNamedPipe and now
            } else if (error == ERROR_IO_PENDING) {
                HANDLE waits[2] = {connectEvent, stopEvent_};
                const DWORD which = WaitForMultipleObjects(2, waits, FALSE, connectTimeoutMs);
                if (which == WAIT_OBJECT_0) {
                    DWORD ignored = 0;
                    connected = GetOverlappedResult(pipe, &ov, &ignored, FALSE) != FALSE;
                } else {
                    CancelIoEx(pipe, &ov);
                    DWORD ignored = 0;
                    GetOverlappedResult(pipe, &ov, &ignored, TRUE);
                }
            }
        }
        CloseHandle(connectEvent);
        if (!connected) {
            CloseHandle(pipe);
            failed_ = true;
            return;
        }
    } else {
        pipe = ConnectWithRetry(pipeName, connectTimeoutMs, stopEvent_);
        if (pipe == INVALID_HANDLE_VALUE) {
            core::Logger::Warn("ScrApiPipe: could not connect to the viewer's pipe; continuing as a plain preview");
            failed_ = true;
            return;
        }
    }
    connected_ = true;
    core::Logger::Info("ScrApiPipe: connected");
    RunIo(pipe);
}

bool ScrApiPipe::CreateServerPipe(const std::wstring& pipeName) {
    const std::wstring path = L"\\\\.\\pipe\\" + pipeName;
    // Default security descriptor: the creating user (plus SYSTEM/Administrators) only.
    serverPipe_ = CreateNamedPipeW(path.c_str(), PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                                   PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS, 1,
                                   64 * 1024, 64 * 1024, 0, nullptr);
    return serverPipe_ != INVALID_HANDLE_VALUE;
}

void ScrApiPipe::StartServing(DWORD connectTimeoutMs) {
    if (thread_.joinable() || serverPipe_ == INVALID_HANDLE_VALUE) return;
    stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    thread_ = std::thread([this, connectTimeoutMs] { ThreadMain(std::wstring(), connectTimeoutMs); });
}

void ScrApiPipe::RunIo(HANDLE pipe) {
    HANDLE readEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    HANDLE writeEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    OVERLAPPED readOv{};
    readOv.hEvent = readEvent;
    char readBuffer[4096];
    std::string partial; // bytes of a line not yet terminated
    bool readPending = false;
    bool lost = false;

    auto queueIncomingLines = [&](const char* data, DWORD size) {
        partial.append(data, size);
        size_t pos;
        while ((pos = partial.find('\n')) != std::string::npos) {
            std::string line = partial.substr(0, pos);
            partial.erase(0, pos + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                incoming_.push_back(std::move(line));
            }
            if (onIncoming_) onIncoming_();
        }
        if (partial.size() > scrapi::kMaxMessageBytes) {
            core::Logger::Warn("ScrApiPipe: oversized message from viewer; dropping the connection");
            lost = true;
        }
    };

    while (!lost) {
        if (!readPending) {
            ResetEvent(readEvent);
            DWORD got = 0;
            if (ReadFile(pipe, readBuffer, sizeof(readBuffer), &got, &readOv)) {
                queueIncomingLines(readBuffer, got); // completed immediately
                continue;
            }
            const DWORD error = GetLastError();
            if (error == ERROR_MORE_DATA) {
                queueIncomingLines(readBuffer, got);
                continue;
            }
            if (error != ERROR_IO_PENDING) {
                lost = true;
                break;
            }
            readPending = true;
        }

        HANDLE waits[3] = {readEvent, wakeEvent_, stopEvent_};
        const DWORD which = WaitForMultipleObjects(3, waits, FALSE, INFINITE);
        if (which == WAIT_OBJECT_0 + 2) break; // stop requested
        if (which == WAIT_OBJECT_0) {
            DWORD got = 0;
            if (GetOverlappedResult(pipe, &readOv, &got, FALSE)) {
                readPending = false;
                queueIncomingLines(readBuffer, got);
            } else if (GetLastError() == ERROR_MORE_DATA) {
                readPending = false;
                queueIncomingLines(readBuffer, got);
            } else {
                lost = true; // broken pipe: the viewer went away
            }
        } else if (which == WAIT_OBJECT_0 + 1) {
            std::deque<std::string> batch;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                batch.swap(outgoing_);
            }
            for (const std::string& line : batch) {
                const std::string framed = line + "\n";
                OVERLAPPED writeOv{};
                writeOv.hEvent = writeEvent;
                ResetEvent(writeEvent);
                DWORD written = 0;
                if (!WriteFile(pipe, framed.data(), static_cast<DWORD>(framed.size()), &written, &writeOv)) {
                    if (GetLastError() != ERROR_IO_PENDING ||
                        WaitForSingleObject(writeEvent, 2000) != WAIT_OBJECT_0 ||
                        !GetOverlappedResult(pipe, &writeOv, &written, FALSE)) {
                        CancelIoEx(pipe, &writeOv);
                        lost = true;
                        break;
                    }
                }
            }
        }
    }

    if (readPending) {
        CancelIoEx(pipe, &readOv);
        DWORD ignored = 0;
        GetOverlappedResult(pipe, &readOv, &ignored, TRUE); // let the cancelled read finish before its buffers go away
    }
    CloseHandle(readEvent);
    CloseHandle(writeEvent);
    CloseHandle(pipe);
    connected_ = false;
    if (lost) {
        failed_ = true;
        core::Logger::Info("ScrApiPipe: connection to viewer ended");
    }
}

} // namespace platform
