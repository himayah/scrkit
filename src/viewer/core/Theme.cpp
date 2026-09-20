#include "Theme.h"

namespace viewer {

Theme Theme::WindowsLight() {
    Theme t;
    t.bg = Color::Rgb(0xf3f3f3);
    t.panel = Color::Rgb(0xf3f3f3);
    t.card = Color::Rgb(0xfbfbfb);
    t.cardBorder = Color::Rgb(0xe3e3e3);
    t.text = Color::Rgb(0x1a1a1a);
    t.text2 = Color::Rgb(0x5c5c5c);
    t.line = Color::Rgb(0xe0e0e0);
    t.accent = Color::Rgb(0x005fb8);
    t.accentText = Color::Rgb(0x005fb8);
    t.onAccent = Color::Rgb(0xffffff);
    t.accentSoft = Color::Rgb(0xe2eefa);
    t.ctl = Color::Rgb(0xffffff);
    t.ctlBorder = Color::Rgb(0xd6d6d6);
    t.ctlBorderBottom = Color::Rgb(0xa8a8a8);
    t.segTrack = Color::Rgb(0xe8e8e8);
    t.segSel = Color::Rgb(0xffffff);
    t.segSelText = Color::Rgb(0x1a1a1a);
    t.track = Color::Rgb(0xc4c4c4);
    t.ok = Color::Rgb(0x0f7b0f);
    t.warn = Color::Rgb(0x9d5d00);
    t.err = Color::Rgb(0xc42b1c);
    t.hover = Color::Rgb(0xececec);
    t.pressed = Color::Rgb(0xe0e0e0);
    return t;
}

Theme Theme::WindowsDark() {
    Theme t;
    t.bg = Color::Rgb(0x202020);
    t.panel = Color::Rgb(0x202020);
    t.card = Color::Rgb(0x2b2b2b);
    t.cardBorder = Color::Rgb(0x383838);
    t.text = Color::Rgb(0xffffff);
    t.text2 = Color::Rgb(0xc8c8c8);
    t.line = Color::Rgb(0x3a3a3a);
    t.accent = Color::Rgb(0x60cdff);
    t.accentText = Color::Rgb(0x60cdff);
    t.onAccent = Color::Rgb(0x000000);
    t.accentSoft = Color::Rgb(0x0f3a52);
    t.ctl = Color::Rgb(0x333333);
    t.ctlBorder = Color::Rgb(0x454545);
    t.ctlBorderBottom = Color::Rgb(0x454545);
    t.segTrack = Color::Rgb(0x1c1c1c);
    t.segSel = Color::Rgb(0x3d3d3d);
    t.segSelText = Color::Rgb(0xffffff);
    t.track = Color::Rgb(0x5a5a5a);
    t.ok = Color::Rgb(0x6ccb5f);
    t.warn = Color::Rgb(0xfce100);
    t.err = Color::Rgb(0xff99a4);
    t.hover = Color::Rgb(0x3b3b3b);
    t.pressed = Color::Rgb(0x474747);
    return t;
}

} // namespace viewer
