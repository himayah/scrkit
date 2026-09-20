#include "test_framework.h"

#include <cmath>

#include "scrapi_fixture.h"
#include "../src/core/SpiralControls.h"
#include "../src/core/effects/EffectParams.h"
#include "../src/viewer/core/PanelController.h"

using namespace viewer;
using scrapi::ControlModel;
using scrapi::JsonValue;

namespace {

struct FixedWidthMeasurer : ITextMeasurer {
    float Width(const std::string& s, float fontSize, bool) override { return static_cast<float>(s.size()) * fontSize * 0.55f; }
};

struct Rig {
    FixedWidthMeasurer measurer;
    Theme theme = Theme::WindowsLight();
    ControlModel model;
    PanelController panel;

    explicit Rig(scrapi::Manifest manifest, float w = 392.0f, float h = 800.0f)
        : model(std::move(manifest)), panel(measurer, theme) {
        panel.SetModel(&model);
        panel.SetViewport(w, h);
    }
    static Rig Spiral(float h = 800.0f) {
        return Rig(core::BuildSpiralManifest(core::fx::MakeDefaultEngineConfig(), "test"), 392.0f, h);
    }
    static Rig Fixture(float h = 800.0f) { return Rig(scrapi_fixture::MakeManifest(), 392.0f, h); }

    // Simulate the host applying an action's value back into the model, as the session would.
    void Apply(const InputResult& r) {
        for (const auto& a : r.actions) {
            if (a.kind == PanelAction::Kind::SetValue) model.Set(a.id, a.value);
        }
        panel.ValuesChanged();
    }
    Rect Control(const std::string& id) {
        Rect r;
        CHECK(panel.ControlRect(id, r));
        return r;
    }
    InputResult Click(const Rect& r) {
        const float x = r.x + r.w / 2, y = r.y + r.h / 2;
        panel.MouseMove(x, y);
        InputResult down = panel.MouseDown(x, y);
        InputResult up = panel.MouseUp(x, y);
        down.actions.insert(down.actions.end(), up.actions.begin(), up.actions.end());
        return down;
    }
    DrawList Paint() {
        DrawList dl;
        panel.Paint(dl);
        return dl;
    }
};

bool HasFill(const DrawList& dl, const Rect& r, Color c) {
    for (const auto& cmd : dl.cmds) {
        if (cmd.kind == DrawCmd::Kind::FillRoundRect && cmd.color == c && std::fabs(cmd.rect.x - r.x) < 0.5f &&
            std::fabs(cmd.rect.y - r.y) < 0.5f && std::fabs(cmd.rect.w - r.w) < 0.5f)
            return true;
    }
    return false;
}

bool HasText(const DrawList& dl, const std::string& text) {
    for (const auto& cmd : dl.cmds) {
        if (cmd.kind == DrawCmd::Kind::Text && cmd.text == text) return true;
    }
    return false;
}

const PanelAction* FirstOf(const InputResult& r, PanelAction::Kind kind) {
    for (const auto& a : r.actions) {
        if (a.kind == kind) return &a;
    }
    return nullptr;
}

} // namespace

TEST_CASE(SliderMapping_LinearStepIntAndLog) {
    scrapi::ControlNode f;
    f.type = scrapi::ControlType::Float;
    f.hasMin = f.hasMax = f.hasStep = true;
    f.min = 0.0;
    f.max = 10.0;
    f.step = 0.5;
    CHECK_NEAR(SliderPositionToValue(f, 0.5), 5.0, 1e-9);
    CHECK_NEAR(SliderPositionToValue(f, 0.26), 2.5, 1e-9);   // snapped to the 0.5 step
    CHECK_NEAR(SliderPositionToValue(f, -3.0), 0.0, 1e-9);   // clamped
    CHECK_NEAR(SliderPositionToValue(f, 9.0), 10.0, 1e-9);
    CHECK_NEAR(ValueToSliderPosition(f, 2.5), 0.25, 1e-9);

    scrapi::ControlNode i;
    i.type = scrapi::ControlType::Int;
    i.hasMin = i.hasMax = true;
    i.min = 1;
    i.max = 100;
    CHECK_NEAR(SliderPositionToValue(i, 0.5), 51.0, 1.0);
    CHECK_NEAR(SliderPositionToValue(i, 0.5), std::round(SliderPositionToValue(i, 0.5)), 1e-12);

    scrapi::ControlNode l;
    l.type = scrapi::ControlType::Float;
    l.hasMin = l.hasMax = true;
    l.min = 1.0;
    l.max = 1000.0;
    l.scale = "log";
    CHECK_NEAR(SliderPositionToValue(l, 0.5), std::sqrt(1000.0), 1e-6);
    CHECK_NEAR(ValueToSliderPosition(l, 10.0), 1.0 / 3.0, 1e-9);
}

TEST_CASE(Panel_EmptyModelShowsAHint) {
    FixedWidthMeasurer m;
    PanelController panel(m, Theme::WindowsLight());
    panel.SetViewport(392, 600);
    DrawList dl;
    panel.Paint(dl);
    CHECK(HasText(dl, "Open a screensaver to see its controls."));
    CHECK_NEAR(panel.ContentHeight(), 0.0f, 1e-6f);
    CHECK(panel.MouseDown(10, 10).actions.empty()); // input on an empty panel is harmless
    CHECK(panel.KeyDown(PanelKey::Tab).actions.empty());
}

TEST_CASE(Panel_SpiralManifestBecomesFourCardsWithHeaders) {
    Rig rig = Rig::Spiral();
    Rect r;
    CHECK(rig.panel.HeaderRect("group.bg", r));
    CHECK(rig.panel.HeaderRect("group.fg", r));
    CHECK(rig.panel.HeaderRect("group.effectParams", r));
    CHECK(rig.panel.HeaderRect("group.simulation", r));
    CHECK(rig.panel.ContentHeight() > 100.0f);
    const DrawList dl = rig.Paint();
    CHECK(HasText(dl, "Background"));
    CHECK(HasText(dl, "Foreground"));
    CHECK(dl.cmds.front().kind == DrawCmd::Kind::FillRoundRect); // panel background first
    CHECK(dl.cmds[1].kind == DrawCmd::Kind::PushClip);
    CHECK(dl.cmds.back().kind == DrawCmd::Kind::PopClip);
}

TEST_CASE(Panel_HeaderClickCollapsesAndExpandsACard) {
    Rig rig = Rig::Spiral();
    Rect header;
    CHECK(rig.panel.HeaderRect("group.bg", header));
    Rect mode;
    CHECK(rig.panel.ControlRect("bg.mode", mode));
    const float before = rig.panel.ContentHeight();
    rig.Click(header);
    CHECK(rig.panel.IsCollapsed("group.bg"));
    CHECK(!rig.panel.ControlRect("bg.mode", mode));            // its rows are gone
    CHECK(rig.panel.ContentHeight() < before);
    rig.Click(header);
    CHECK(!rig.panel.IsCollapsed("group.bg"));
    CHECK(rig.panel.ControlRect("bg.mode", mode));
    CHECK_NEAR(rig.panel.ContentHeight(), before, 1e-3f);
}

TEST_CASE(Panel_SegmentedClickRequestsTheOptionAndTheSelectionIsPainted) {
    Rig rig = Rig::Spiral();
    Rect pin;
    CHECK(rig.panel.PartRect("bg.mode", 2, pin));
    const InputResult r = rig.Click(pin);
    const PanelAction* a = FirstOf(r, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->id == "bg.mode");
    CHECK(a->value == JsonValue::String("pin"));
    CHECK(!a->continuous);

    // Nothing is selected visually until the model actually has the value...
    CHECK(!HasFill(rig.Paint(), pin, rig.theme.segSel));
    rig.Apply(r);
    // ...then the chosen segment is filled with the selection color.
    CHECK(rig.panel.PartRect("bg.mode", 2, pin));
    CHECK(HasFill(rig.Paint(), pin, rig.theme.segSel));
}

TEST_CASE(Panel_ControlsAppearAndDisappearWithVisibleWhen) {
    Rig rig = Rig::Spiral();
    Rect r;
    CHECK(!rig.panel.ControlRect("bg.effect", r));     // Auto: no effect dropdown
    CHECK(rig.panel.ControlRect("bg.pool", r));        // Auto: pool chips
    rig.model.Set("bg.mode", JsonValue::String("pin"));
    rig.model.Set("bg.effect", JsonValue::String("Ripple"));
    rig.panel.ValuesChanged();
    CHECK(rig.panel.ControlRect("bg.effect", r));
    CHECK(!rig.panel.ControlRect("bg.pool", r));
    CHECK(rig.panel.ControlRect("fx.Ripple.intensity", r)); // sub-section for the pinned effect appears
    CHECK(HasText(rig.Paint(), "Ripple"));
}

TEST_CASE(Panel_DropdownOpensSelectsByClickAndByKeyboardAndDismisses) {
    Rig rig = Rig::Spiral();
    rig.model.Set("bg.mode", JsonValue::String("pin"));
    rig.model.Set("bg.effect", JsonValue::String("Tilt"));
    rig.panel.ValuesChanged();
    const Rect dd = rig.Control("bg.effect");

    rig.Click(dd);
    CHECK(rig.panel.PopupOpen());
    CHECK(HasText(rig.Paint(), "LensDistort")); // the option list is drawn

    // Click the first visible option row -> that option is requested and the list closes.
    // Rows are 28px, starting 4px into the popup which sits 2px below the control.
    const float x = dd.x + 20.0f;
    const float firstRowY = dd.Bottom() + 2.0f + 4.0f + 14.0f;
    rig.panel.MouseMove(x, firstRowY);
    const InputResult pick = rig.panel.MouseDown(x, firstRowY);
    const PanelAction* a = FirstOf(pick, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    CHECK(!rig.panel.PopupOpen());

    // A click elsewhere only dismisses.
    rig.Click(dd);
    CHECK(rig.panel.PopupOpen());
    const InputResult away = rig.panel.MouseDown(5.0f, 5.0f);
    CHECK(away.actions.empty());
    CHECK(!rig.panel.PopupOpen());

    // Keyboard: focus by Tab..., open, move, choose, Escape closes.
    rig.Click(dd);
    CHECK(rig.panel.PopupOpen());
    rig.panel.KeyDown(PanelKey::Down);
    const InputResult chosen = rig.panel.KeyDown(PanelKey::Enter);
    CHECK(FirstOf(chosen, PanelAction::Kind::SetValue) != nullptr);
    CHECK(!rig.panel.PopupOpen());
    rig.Click(dd);
    rig.panel.KeyDown(PanelKey::Escape);
    CHECK(!rig.panel.PopupOpen());
}

TEST_CASE(Panel_FlagsChipsToggleAndAllNoneLinksWork) {
    Rig rig = Rig::Spiral();
    // Auto pool of the background starts with everything on.
    Rect first;
    CHECK(rig.panel.PartRect("bg.pool", 0, first));
    InputResult r = rig.Click(first);
    const PanelAction* a = FirstOf(r, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->id == "bg.pool");
    CHECK_EQ(a->value.items().size(), static_cast<size_t>(rig.model.Find("bg.pool")->options.size() - 1)); // one chip turned off
    CHECK(SerializeJson(a->value).find("\"Ripple\"") == std::string::npos);
    rig.Apply(r);

    Rect none, all;
    CHECK(rig.panel.FlagsLinkRect("bg.pool", false, none));
    r = rig.Click(none);
    a = FirstOf(r, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (a) CHECK(a->value.items().empty());
    rig.Apply(r);
    CHECK(rig.panel.FlagsLinkRect("bg.pool", true, all));
    r = rig.Click(all);
    a = FirstOf(r, PanelAction::Kind::SetValue);
    if (a) CHECK_EQ(a->value.items().size(), rig.model.Find("bg.pool")->options.size());
}

TEST_CASE(Panel_SliderDragSendsContinuousValuesThenAFinalOne) {
    Rig rig = Rig::Spiral();
    const Rect s = rig.Control("bg.minSeconds"); // 0.5..120 step 0.5
    const float y = s.y + s.h / 2;
    const float left = s.x + 9.0f, width = s.w - 18.0f;

    InputResult down = rig.panel.MouseDown(left + width * 0.5f, y);
    const PanelAction* a = FirstOf(down, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (!a) return;
    CHECK(a->continuous);
    CHECK_NEAR(a->value.AsDouble(), 60.0, 0.5 + 1e-9);

    InputResult move = rig.panel.MouseMove(left + width * 0.75f, y + 30.0f); // vertical drift doesn't matter
    a = FirstOf(move, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (a) {
        CHECK(a->continuous);
        CHECK_NEAR(a->value.AsDouble(), 90.0, 0.5 + 1e-9);
    }
    CHECK(HasText(rig.Paint(), "90 s")); // the value label follows the drag before the model catches up

    InputResult beyond = rig.panel.MouseMove(left + width * 5.0f, y);  // dragged far past the end: clamped
    a = FirstOf(beyond, PanelAction::Kind::SetValue);
    if (a) CHECK_NEAR(a->value.AsDouble(), 120.0, 1e-9);

    InputResult up = rig.panel.MouseUp(left + width * 5.0f, y);
    a = FirstOf(up, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (a) {
        CHECK(!a->continuous); // the final value is a definite one
        CHECK_NEAR(a->value.AsDouble(), 120.0, 1e-9);
    }
    // After the drag, moving the mouse changes nothing.
    CHECK(rig.panel.MouseMove(left, y).actions.empty());
}

TEST_CASE(Panel_KnobDragsVerticallyAndClamps) {
    Rig rig = Rig::Spiral();
    rig.model.Set("bg.mode", JsonValue::String("pin"));
    rig.model.Set("bg.effect", JsonValue::String("Ripple"));
    rig.panel.ValuesChanged();
    const Rect k = rig.Control("fx.Ripple.intensity"); // 0..1, default 0.6
    const float x = k.x + k.w / 2, y = k.y + k.h / 2;

    rig.panel.MouseDown(x, y);
    InputResult up100 = rig.panel.MouseMove(x, y - 100.0f); // 100px up = +0.5 of the range
    const PanelAction* a = FirstOf(up100, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (a) CHECK_NEAR(a->value.AsDouble(), 1.0, 1e-9); // 0.6 + 0.5 clamped to 1.0
    InputResult down = rig.panel.MouseMove(x, y + 60.0f);
    a = FirstOf(down, PanelAction::Kind::SetValue);
    if (a) CHECK_NEAR(a->value.AsDouble(), 0.6 - 0.3, 0.051); // 60px down = -0.3 (snapped to the 0.05 step)
    rig.panel.MouseUp(x, y);
}

TEST_CASE(Panel_ToggleTextColorPathAndButtonRequests) {
    Rig rig = Rig::Fixture();

    InputResult r = rig.Click(rig.Control("enabled"));
    const PanelAction* a = FirstOf(r, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (a) CHECK(a->value == JsonValue::Bool(false)); // it was true
    rig.Apply(r);

    // A disabled control (file: enabledWhen enabled==true) ignores clicks.
    CHECK(rig.Click(rig.Control("file")).actions.empty());
    rig.model.Set("enabled", JsonValue::Bool(true));
    rig.panel.ValuesChanged();

    r = rig.Click(rig.Control("title"));
    a = FirstOf(r, PanelAction::Kind::EditText);
    CHECK(a != nullptr);
    if (a) {
        CHECK(a->id == "title");
        CHECK(a->rect.w > 50.0f);
        const InputResult commit = rig.panel.CommitText("title", "Kayak");
        CHECK(FirstOf(commit, PanelAction::Kind::SetValue)->value == JsonValue::String("Kayak"));
    }

    r = rig.Click(rig.Control("tint"));
    a = FirstOf(r, PanelAction::Kind::PickColor);
    CHECK(a != nullptr);
    if (a) CHECK(a->text == "#336699");

    r = rig.Click(rig.Control("go"));
    CHECK(FirstOf(r, PanelAction::Kind::Invoke) != nullptr);
}

TEST_CASE(Panel_ButtonWithConfirmAsksFirstAndOnlyInvokesAfterConfirmation) {
    scrapi::Manifest m = scrapi_fixture::MakeManifest();
    for (auto& c : m.controls) {
        if (c.id == "go") c.confirm = "Really?";
    }
    Rig rig(std::move(m));
    const InputResult r = rig.Click(rig.Control("go"));
    const PanelAction* ask = FirstOf(r, PanelAction::Kind::Confirm);
    CHECK(ask != nullptr);
    CHECK(FirstOf(r, PanelAction::Kind::Invoke) == nullptr);
    if (ask) CHECK(ask->text == "Really?");
    CHECK(rig.panel.ConfirmInvoke("other").actions.empty()); // not the pending one
    const InputResult ok = rig.panel.ConfirmInvoke("go");
    CHECK(FirstOf(ok, PanelAction::Kind::Invoke) != nullptr);
    CHECK(rig.panel.ConfirmInvoke("go").actions.empty());    // confirmation is single-use
}

TEST_CASE(Panel_PathFieldBrowseAndProvidedValue) {
    scrapi::Manifest m = scrapi_fixture::MakeManifest();
    for (auto& c : m.controls) {
        if (c.id == "file") c.filters = {{"Images", "*.png"}};
    }
    Rig rig(std::move(m));
    rig.model.Set("enabled", JsonValue::Bool(true));
    rig.panel.ValuesChanged();
    Rect field = rig.Control("file");
    // The Browse button sits to the right of the field.
    const Rect browse{field.Right() + 8.0f + 10.0f, field.y, 20.0f, field.h};
    const InputResult r = rig.Click(browse);
    const PanelAction* a = FirstOf(r, PanelAction::Kind::BrowsePath);
    CHECK(a != nullptr);
    if (a) CHECK_EQ(a->filters.size(), static_cast<size_t>(1));
    const InputResult provided = rig.panel.ProvideValue("file", JsonValue::String("C:\\x.png"));
    CHECK(FirstOf(provided, PanelAction::Kind::SetValue)->value == JsonValue::String("C:\\x.png"));
}

TEST_CASE(Panel_WheelScrollsAndClampsAndScrollbarThumbDrags) {
    Rig rig = Rig::Spiral(300.0f); // much shorter than the content
    CHECK(rig.panel.ContentHeight() > 300.0f);
    CHECK_NEAR(rig.panel.ScrollY(), 0.0f, 1e-6f);
    rig.panel.MouseWheel(100, 100, -1.0f); // one notch down
    CHECK(rig.panel.ScrollY() > 0.0f);
    for (int i = 0; i < 100; ++i) rig.panel.MouseWheel(100, 100, -1.0f);
    CHECK_NEAR(rig.panel.ScrollY(), rig.panel.ContentHeight() - 300.0f, 1e-3f);
    for (int i = 0; i < 100; ++i) rig.panel.MouseWheel(100, 100, +1.0f);
    CHECK_NEAR(rig.panel.ScrollY(), 0.0f, 1e-6f);

    // Dragging the scrollbar thumb: the thumb is at the top; drag it to the bottom.
    rig.panel.MouseDown(392.0f - 6.0f, 10.0f);
    rig.panel.MouseMove(392.0f - 6.0f, 10.0f + 1000.0f);
    CHECK_NEAR(rig.panel.ScrollY(), rig.panel.ContentHeight() - 300.0f, 1e-2f);
    rig.panel.MouseUp(0, 0);
}

TEST_CASE(Panel_ClicksHitTheRightThingAfterScrolling) {
    Rig rig = Rig::Spiral(300.0f);
    for (int i = 0; i < 3; ++i) rig.panel.MouseWheel(100, 100, -1.0f);
    Rect r;
    if (rig.panel.HeaderRect("group.fg", r) && r.y >= 0.0f && r.Bottom() <= 300.0f) {
        rig.Click(r);
        CHECK(rig.panel.IsCollapsed("group.fg"));
    } else {
        // Not on screen at this scroll position: at least the coordinates must be viewport-relative.
        Rect bg;
        CHECK(rig.panel.HeaderRect("group.bg", bg));
        CHECK(bg.y < 0.0f + 1.0f);
    }
}

TEST_CASE(Panel_KeyboardTabFocusSpaceAndArrows) {
    Rig rig = Rig::Spiral();
    // First Tab focuses the first focusable thing: the Background header. Space toggles it.
    rig.panel.KeyDown(PanelKey::Tab);
    rig.panel.KeyDown(PanelKey::Space);
    CHECK(rig.panel.IsCollapsed("group.bg"));
    rig.panel.KeyDown(PanelKey::Space);
    CHECK(!rig.panel.IsCollapsed("group.bg"));

    // Tab again -> the mode segmented control; Right selects the next option.
    rig.panel.KeyDown(PanelKey::Tab);
    const InputResult r = rig.panel.KeyDown(PanelKey::Right);
    const PanelAction* a = FirstOf(r, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (a) {
        CHECK(a->id == "bg.mode");
        CHECK(a->value == JsonValue::String("rest")); // auto -> rest
    }
    // Shift+Tab goes back to the header.
    rig.panel.KeyDown(PanelKey::ShiftTab);
    rig.panel.KeyDown(PanelKey::Space);
    CHECK(rig.panel.IsCollapsed("group.bg"));
}

TEST_CASE(Panel_KeyboardFocusSurvivesValueUpdates) {
    Rig rig = Rig::Spiral();
    rig.panel.KeyDown(PanelKey::Tab);
    rig.panel.KeyDown(PanelKey::Tab); // on bg.mode
    rig.model.SetFromSaver("bg.currentEffect", JsonValue::String("Ripple"));
    rig.panel.ValuesChanged();        // a readout streamed in
    const InputResult r = rig.panel.KeyDown(PanelKey::Right);
    CHECK(FirstOf(r, PanelAction::Kind::SetValue) != nullptr); // still focused on the segmented control
}

TEST_CASE(Panel_SliderKeyboardAdjustsByStep) {
    Rig rig = Rig::Spiral();
    const Rect s = rig.Control("bg.minSeconds");
    rig.Click(Rect{s.x - 2.0f, s.y + 40.0f, 1.0f, 1.0f}); // (miss: just to make sure it does no harm)
    // Focus it by clicking on the slider, then release without moving.
    const float x = s.x + 9.0f, y = s.y + s.h / 2;
    rig.panel.MouseDown(x, y);
    rig.panel.MouseUp(x, y);
    const InputResult r = rig.panel.KeyDown(PanelKey::Right);
    const PanelAction* a = FirstOf(r, PanelAction::Kind::SetValue);
    CHECK(a != nullptr);
    if (a) CHECK(a->id == "bg.minSeconds");
}

TEST_CASE(Panel_DisabledControlsAreDrawnFaded) {
    Rig rig = Rig::Fixture();
    rig.model.Set("enabled", JsonValue::Bool(false));
    rig.panel.ValuesChanged();
    const Rect f = rig.Control("file");
    const DrawList dl = rig.Paint();
    bool fadedField = false;
    for (const auto& cmd : dl.cmds) {
        if (cmd.kind == DrawCmd::Kind::FillRoundRect && std::fabs(cmd.rect.x - f.x) < 0.5f && std::fabs(cmd.rect.y - f.y) < 0.5f &&
            cmd.color.a < 200)
            fadedField = true;
    }
    CHECK(fadedField);
}

TEST_CASE(Panel_ThemesDifferButShareStructure) {
    CHECK(Theme::WindowsLight().card != Theme::WindowsDark().card);
    CHECK(Theme::WindowsLight().accent == Color::Rgb(0x005fb8));
    CHECK(Theme::WindowsDark().accent == Color::Rgb(0x60cdff));
    FixedWidthMeasurer m;
    Rig rig = Rig::Spiral();
    PanelController dark(m, Theme::WindowsDark());
    dark.SetModel(&rig.model);
    dark.SetViewport(392, 800);
    CHECK_NEAR(dark.ContentHeight(), rig.panel.ContentHeight(), 1e-3f);
}

TEST_CASE(Panel_GroupsMarkedCollapsedStartClosedAndOpenOnClick) {
    Rig rig = Rig::Spiral();
    CHECK(rig.panel.IsCollapsed("group.content"));
    Rect r;
    CHECK(!rig.panel.ControlRect("content.source", r));   // inside a closed card
    CHECK(!rig.panel.IsCollapsed("group.bg"));            // the others start open
    Rect header;
    CHECK(rig.panel.HeaderRect("group.content", header));
    rig.Click(header);
    CHECK(!rig.panel.IsCollapsed("group.content"));
    CHECK(rig.panel.ControlRect("content.source", r));
    // and it stays as the user left it across relayouts
    rig.panel.ValuesChanged();
    CHECK(!rig.panel.IsCollapsed("group.content"));
}
