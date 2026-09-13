#pragma once
// Best-effort, READ-ONLY queries of the real desktop's icon and open-window
// layout AND appearance, so the saver's simulated icon/window boxes can
// start from where things really are and look like what's really there --
// including the parts normally hidden behind whatever else is on top of
// them right now (user feedback: overlapped windows/icons should still
// clip their real content, not whatever happens to be drawn over them).
//
// These only ever read already-public window/desktop state via documented
// Win32 mechanisms (EnumWindows, GetWindowRect, PrintWindow, and the
// well-known cross-process technique apps use to read the desktop's icon
// ListView). Nothing here moves, resizes, closes, or otherwise changes
// anything -- 要件.txt §10 (実際のデスクトップを操作してはならない) is about
// such modifications, not read-only queries. Every function degrades
// gracefully (returns false / leaves `out` empty, or leaves an individual
// item's capture flagged unavailable) if anything about this fails --
// different shell replacement, security software blocking cross-process
// memory access, a window that renders blank under PrintWindow (protected
// video content, etc.), API changes in a future Windows version -- so the
// caller can fall back at whatever granularity is available.

#include <vector>

#include "../../core/DesktopElements.h"
#include "ImageLoader.h"

namespace platform {

// A real open window's position/size/title plus (when possible) its own
// true appearance, captured via PrintWindow so it's correct even where
// another window currently overlaps it on the real screen.
struct RealWindowInfo {
    core::WindowElement element;
    DecodedImage capture;
    bool hasCapture = false;
};

// Enumerates real, visible, titled top-level windows (skips tool windows,
// minimized windows, shell-owned windows like Progman/the taskbar, and
// windows fully outside the primary monitor). Returns false if none could
// be found.
bool QueryRealOpenWindows(int screenWidth, int screenHeight, std::vector<RealWindowInfo>& out);

// The real desktop icons' positions/sizes/labels, plus (when possible) one
// combined capture of the whole icon layer -- captured via PrintWindow on
// the icon view itself so it shows every icon uncovered, even ones a
// window happens to be sitting on top of on the real screen right now.
// `captureOriginX/Y` locate the capture's (0,0) in the same screen-pixel
// space as `icons`' positions (subtract before dividing by capture
// width/height to get UV coordinates for a given icon).
struct RealIconLayerInfo {
    std::vector<core::IconElement> icons;
    DecodedImage capture;
    bool hasCapture = false;
    float captureOriginX = 0.0f;
    float captureOriginY = 0.0f;
};

// Reads the real desktop icons from the desktop's icon ListView
// (Progman/WorkerW -> SHELLDLL_DefView -> SysListView32). Returns false if
// the ListView could not be found or no icon position could be read (a
// capture failure alone does not cause this to return false -- `out.icons`
// can still be populated with `out.hasCapture == false`).
bool QueryRealDesktopIcons(int screenWidth, int screenHeight, RealIconLayerInfo& out);

} // namespace platform
