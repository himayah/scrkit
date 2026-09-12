#pragma once
// Best-effort, READ-ONLY queries of the real desktop's icon and open-window
// layout, so the saver's simulated icon/window boxes can start from where
// things really are instead of a random position (user feedback: random
// placement "didn't look like" real icons/windows being sucked in).
//
// These only ever read already-public window/desktop state via documented
// Win32 mechanisms (EnumWindows, GetWindowRect, and the well-known
// cross-process technique apps use to read the desktop's icon ListView).
// Nothing here moves, resizes, closes, or otherwise changes anything --
// 要件.txt §10 (実際のデスクトップを操作してはならない) is about such
// modifications, not read-only queries. Every function degrades gracefully
// (returns false / leaves `out` empty) if anything about this fails --
// different shell replacement, security software blocking cross-process
// memory access, API changes in a future Windows version, etc. -- so the
// caller can fall back to the fully-simulated layout.

#include <vector>

#include "../../core/DesktopElements.h"

namespace platform {

// Enumerates real, visible, titled top-level windows (skips tool windows,
// minimized windows, and windows fully outside the primary monitor).
// Returns false if none could be found.
bool QueryRealOpenWindows(int screenWidth, int screenHeight, std::vector<core::WindowElement>& out);

// Reads the real desktop icons' positions/sizes/labels from the desktop's
// icon ListView (Progman/WorkerW -> SHELLDLL_DefView -> SysListView32).
// Returns false if the ListView could not be found or read.
bool QueryRealDesktopIcons(int screenWidth, int screenHeight, std::vector<core::IconElement>& out);

} // namespace platform
