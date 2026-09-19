#pragma once
// Window creation + the main render loop, for both /s (fullscreen) and /p
// (small preview inside the Display Settings dialog) modes
// (要件.txt §1 Step 1, §2 Step 2).

#include <string>

#include <windows.h>

namespace platform {

// Creates a fullscreen, topmost, cursor-hidden window on the primary
// monitor and runs the saver until the user presses a key, clicks, or moves
// the mouse beyond a small threshold. Blocks until then.
void RunFullScreenSaver(HINSTANCE instance);

// Creates a child window filling `previewParent` (a window owned by the
// Display Settings dialog, possibly in another process) and runs the saver
// there until the parent window is destroyed. Blocks until then.
//
// With a non-empty `scrapiPipeName`, additionally connects to the SCRAPI viewer's named
// pipe (docs/SCRAPI_SPEC.md): the saver then renders at a fixed logical resolution
// (the primary monitor's size) scaled into whatever size the viewer gives the window,
// stops its automatic blackout cycle, and lets the viewer drive it through controls.
// Without it this is the ordinary, unchanged Display Settings preview.
void RunPreview(HINSTANCE instance, HWND previewParent, const std::wstring& scrapiPipeName = std::wstring());

} // namespace platform
