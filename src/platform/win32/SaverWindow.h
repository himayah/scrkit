#pragma once
// Window creation + the main render loop, for both /s (fullscreen) and /p
// (small preview inside the Display Settings dialog) modes
// (要件.txt §1 Step 1, §2 Step 2).

#include <windows.h>

namespace platform {

// Creates a fullscreen, topmost, cursor-hidden window on the primary
// monitor and runs the saver until the user presses a key, clicks, or moves
// the mouse beyond a small threshold. Blocks until then.
void RunFullScreenSaver(HINSTANCE instance);

// Creates a child window filling `previewParent` (a window owned by the
// Display Settings dialog, possibly in another process) and runs the saver
// there until the parent window is destroyed. Blocks until then.
void RunPreview(HINSTANCE instance, HWND previewParent);

} // namespace platform
