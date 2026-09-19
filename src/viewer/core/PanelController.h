#pragma once
// The brain of the custom-drawn controller panel (docs/DESIGN_VIEWER_UI.md §4-§5): from a
// SCRAPI ControlModel it lays out cards and controls, produces the DrawList for the current
// state, and turns mouse/keyboard input into value changes and requests for the host.
// No platform code: the host feeds it input in viewport coordinates and executes the actions
// and the display list it returns, which is also what makes all of it unit-testable.

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../../scrapi/ControlModel.h"
#include "DrawList.h"
#include "Geometry.h"
#include "TextMeasurer.h"
#include "Theme.h"

namespace viewer {

// Something the host has to do as the result of input.
struct PanelAction {
    enum class Kind {
        SetValue,   // send `value` for control `id` (continuous=true while a drag is still going on)
        Invoke,     // run button `id`
        EditText,   // show a text editor over `rect` (viewport coords) with `text`; later CommitText
        BrowsePath, // open a file dialog (filters); later ProvideValue
        PickColor,  // open a color dialog starting at `text`; later ProvideValue
        Confirm,    // ask the user `text`; if accepted, call ConfirmInvoke(id)
    };
    Kind kind = Kind::SetValue;
    std::string id;
    scrapi::JsonValue value;
    Rect rect;
    std::string text;
    bool continuous = false;
    std::vector<std::pair<std::string, std::string>> filters;
};

struct InputResult {
    std::vector<PanelAction> actions;
    bool redraw = false;
};

enum class PanelKey { Tab, ShiftTab, Space, Enter, Escape, Left, Right, Up, Down, Home, End };

// Slider mapping, exposed for tests: position t in [0,1] <-> control value (linear or log,
// snapped to `step` for floats, rounded for ints).
double SliderPositionToValue(const scrapi::ControlNode& node, double t);
double ValueToSliderPosition(const scrapi::ControlNode& node, double value);

class PanelController {
public:
    PanelController(ITextMeasurer& measurer, const Theme& theme);

    void SetTheme(const Theme& theme) { theme_ = theme; }
    // The model must outlive its use here (until the next SetModel). Null = empty panel.
    void SetModel(const scrapi::ControlModel* model);
    void SetViewport(float width, float height);
    // Some values in the model changed: relayout (visibility may have changed) and repaint.
    void ValuesChanged();

    InputResult MouseMove(float x, float y);
    InputResult MouseDown(float x, float y);
    InputResult MouseUp(float x, float y);
    InputResult MouseLeave();
    // `delta` > 0 scrolls up (like a wheel notch away from the user).
    InputResult MouseWheel(float x, float y, float delta);
    InputResult KeyDown(PanelKey key);

    // Results of the requests above.
    InputResult CommitText(const std::string& id, const std::string& text);
    InputResult ProvideValue(const std::string& id, const scrapi::JsonValue& value);
    InputResult ConfirmInvoke(const std::string& id);

    void Paint(DrawList& out) const;

    float ContentHeight() const { return contentHeight_; }
    float ScrollY() const { return scrollY_; }
    bool IsCollapsed(const std::string& groupId) const;
    bool PopupOpen() const { return popup_.open; }
    // The text-edit field rect for `id` in viewport coordinates, if it is currently laid out.
    bool FieldRect(const std::string& id, Rect& out) const;
    // Where control `id`'s main widget currently is (viewport coordinates); false if it isn't
    // laid out (hidden by visibleWhen, inside a collapsed card, ...). For hosts and tests.
    bool ControlRect(const std::string& id, Rect& out) const;
    // One sub-part of a list-like control (segment / chip / palette swatch `part`).
    bool PartRect(const std::string& id, int part, Rect& out) const;
    // The "All" / "None" links of a flags control.
    bool FlagsLinkRect(const std::string& id, bool all, Rect& out) const;
    // The header row of top-level group `id`.
    bool HeaderRect(const std::string& id, Rect& out) const;

private:
    // Sentinel "parts" for hit results that aren't an index into a list.
    static constexpr int kNoPart = -1;
    static constexpr int kMain = 1000;
    static constexpr int kAux = 1001;
    static constexpr int kAux2 = 1002;

    struct Item {
        enum class Kind {
            CardHeader, SubHeader, Segmented, Dropdown, Flags, Toggle, Slider, Knob,
            TextField, PathField, ColorField, Button, Readout
        };
        Kind kind = Kind::Readout;
        const scrapi::ControlNode* node = nullptr;
        Rect rect;   // the whole row (content coordinates)
        Rect label;
        Rect control;
        Rect aux;    // value text / Browse / hex field / "All"
        Rect aux2;   // "None"
        std::vector<Rect> parts; // segments / chips / palette swatches
        bool enabled = true;
        bool focusable = false;
    };
    struct Card {
        Rect rect;
    };
    struct Hit {
        int item = -1;
        int part = kNoPart;
    };
    struct Drag {
        enum class Kind { None, Slider, Knob, Scrollbar } kind = Kind::None;
        int item = -1;
        float startY = 0.0f;
        double startValue = 0.0;
        float startScroll = 0.0f;
    };
    struct Popup {
        bool open = false;
        int item = -1;
        std::string id;   // the dropdown's control id (survives a relayout)
        int scroll = 0;   // first visible option
        int hover = -1;   // option index under the mouse / keyboard
        Rect rect;        // viewport coordinates
    };

    // layout
    void Layout();
    float LayoutBody(const std::vector<scrapi::ControlNode>& nodes, float y, float left, float right);
    float LayoutControl(const scrapi::ControlNode& node, float y, float left, float right);
    void ClampScroll();

    // model helpers
    const scrapi::JsonValue* ValueOf(const scrapi::ControlNode& node) const;
    std::string DisplayLabel(const scrapi::ControlNode& node) const;
    std::string FormatValue(const scrapi::ControlNode& node, const scrapi::JsonValue& v) const;
    double NumericValue(const Item& item) const;   // includes a drag in progress
    static bool UsesSegments(const scrapi::ControlNode& node);
    static bool IsKnob(const scrapi::ControlNode& node);

    // hit testing / geometry
    Hit HitTest(float x, float y) const;   // viewport coordinates
    Rect ToViewport(const Rect& contentRect) const { return contentRect.Offset(0.0f, -scrollY_); }
    Rect SliderTrack(const Item& item) const;
    Rect ScrollbarThumb() const;
    void OpenPopup(int item);
    void ClosePopup() { popup_.open = false; }
    Rect PopupRectFor(const Item& item, int optionCount) const;

    // behaviour
    InputResult Activate(int item, int part, float x, float y);
    InputResult SetNumber(const Item& item, double value, bool continuous);
    PanelAction MakeSet(const std::string& id, scrapi::JsonValue v, bool continuous = false) const;
    InputResult AdjustFocused(int direction);
    std::vector<int> FocusOrder() const;
    void ScrollIntoView(int item);

    // painting
    void PaintItem(DrawList& dl, size_t index) const;
    void PaintPopup(DrawList& dl) const;
    void PaintScrollbar(DrawList& dl) const;

    ITextMeasurer& measurer_;
    Theme theme_;
    const scrapi::ControlModel* model_ = nullptr;
    float viewW_ = 0.0f, viewH_ = 0.0f;
    float scrollY_ = 0.0f, contentHeight_ = 0.0f;
    std::vector<Item> items_;
    std::vector<Card> cards_;
    std::map<std::string, bool> collapsed_;
    Hit hover_;
    int focusItem_ = -1, focusPart_ = -1;
    Drag drag_;
    double dragValue_ = 0.0;
    std::string pendingConfirmId_;
    Popup popup_;
};

} // namespace viewer
