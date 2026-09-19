#pragma once
// Starts a .scr in preview mode inside a host window and tracks the process
// (docs/DESIGN_VIEWER.md §C.2): `<scr> /p <hwnd> /scrapi:<pipe>`.

#include <string>

#include <windows.h>

namespace viewer {

class ScrLauncher {
public:
    ~ScrLauncher() { Terminate(); }
    ScrLauncher() = default;
    ScrLauncher(const ScrLauncher&) = delete;
    ScrLauncher& operator=(const ScrLauncher&) = delete;

    // Launches `scrPath` with `previewParent` as its /p window and `pipeName` as its SCRAPI
    // pipe. Returns false (with *error set) if the process couldn't be created.
    bool Launch(const std::wstring& scrPath, HWND previewParent, const std::wstring& pipeName, std::wstring* error);

    // True while the launched process is still running.
    bool IsRunning() const;

    // Asks the saver to leave (it exits when its parent window goes away or on `bye`), then
    // ends it if it doesn't within `graceMs`. Safe to call repeatedly.
    void Terminate(DWORD graceMs = 1000);

private:
    HANDLE process_ = nullptr;
};

// A fresh, unguessable pipe name: "scrapi-<GUID>".
std::wstring MakePipeName();

} // namespace viewer
