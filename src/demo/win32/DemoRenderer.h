#pragma once
// Deliberately minimal GDI-only rendering for ScrApiDemo.scr: this demo exists to prove SCRAPI
// control patterns end-to-end (real process, real pipe, real preview embedding), not to be a
// visually polished screensaver, so there is no OpenGL context, no particle system -- just enough
// drawing that every control's effect is visibly, honestly different on screen.

#include <windows.h>

#include "../../core/DemoControls.h"

namespace demo {

// Paints one frame into `hdc` covering `client`. `burstActive` draws a highlight ring around the
// shape while the "Burst" button's one-second effect is running (see DemoControlBinder::Tick).
void PaintDemoFrame(HDC hdc, const RECT& client, const core::DemoState& state, bool burstActive);

} // namespace demo
