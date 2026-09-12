#pragma once
// The /c settings dialog: particle-count preset (+ custom + auto-detect) and
// background image override (要件.txt §6, Step 9).

#include <windows.h>

namespace platform {

// Shows the modal settings dialog. `parent` may be nullptr. Returns once the
// user closes it (OK saves to config.ini via core::SaveConfigToFile-style
// serialization; Cancel discards changes).
void ShowConfigDialog(HWND parent, HINSTANCE instance);

} // namespace platform
