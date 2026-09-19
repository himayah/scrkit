#pragma once
// The scrolling controller panel: builds standard Win32 widgets from a SCRAPI manifest
// (docs/DESIGN_VIEWER.md §C.3) and keeps them in sync with a scrapi::ControlModel.
// Knows nothing about any particular saver -- only the widget vocabulary of
// docs/SCRAPI_SPEC.md §5.2.

#include <functional>
#include <map>
#include <string>
#include <vector>

#include <windows.h>

#include "../../scrapi/ControlModel.h"

namespace viewer {

class ControlPanel {
public:
    using SetFn = std::function<void(const std::string& id, const scrapi::JsonValue& value)>;
    using InvokeFn = std::function<void(const std::string& id)>;

    ControlPanel() = default;
    ~ControlPanel();
    ControlPanel(const ControlPanel&) = delete;
    ControlPanel& operator=(const ControlPanel&) = delete;

    bool Create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }
    void SetCallbacks(SetFn onSet, InvokeFn onInvoke) {
        onSet_ = std::move(onSet);
        onInvoke_ = std::move(onInvoke);
    }

    // Destroys every widget and creates them again from `model`'s manifest (null = empty
    // panel). `model` must outlive the widgets (i.e. until the next Rebuild/Clear).
    void Rebuild(const scrapi::ControlModel* model);
    void Clear() { Rebuild(nullptr); }
    // The values of `changedIds` changed in the model: update their widgets, then
    // re-evaluate every control's visibility/enabled state.
    void Refresh(const std::vector<std::string>& changedIds);

    // Called by the window procedure.
    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    struct Row {
        const scrapi::ControlNode* node = nullptr;
        int depth = 0;
        HWND label = nullptr;
        HWND widget = nullptr; // combobox / checkbox / trackbar / edit / static / button
        HWND aux = nullptr;    // slider value text, browse button
        std::vector<HWND> items; // radio buttons / flag checkboxes, one per option
        COLORREF color = 0;    // color controls
        bool visible = true;
        bool enabled = true;
        bool created = false; // windows are made lazily, the first time the row becomes visible
        int height = 0;
    };

    // Registers a row for `node` without creating any window: the manifest has hundreds of
    // controls, most hidden behind visibleWhen at any moment, and creating (and, under
    // double buffering, painting) all of them up front made the panel slow to appear.
    void CreateRow(const scrapi::ControlNode& node, int depth);
    void BuildRowWindows(size_t index);
    void UpdateWidget(Row& row);
    // Re-evaluates every control's visibleWhen/enabledWhen. Only what actually changed is
    // touched, and the (expensive) relayout+repaint only happens if some row was shown or
    // hidden -- this runs on every value update, including the readouts the saver streams.
    // `force` (after a rebuild) applies the state to every row unconditionally.
    void ApplyVisibility(bool force = false);
    // fullRedraw=false is for scrolling: children are moved in one batch with their pixels
    // copied along, and nothing is invalidated wholesale (a per-step erase+repaint starves
    // WM_PAINT during a drag and leaves the panel blank). fullRedraw=true (resize, rows
    // shown/hidden, rebuild) repaints everything so no ghost of the old layout survives.
    void Relayout(bool fullRedraw = true);
    void UpdateScrollBar();
    void OnCommand(int controlId, int notification, HWND source);
    void OnSlider(HWND slider, bool final);
    void FlushPendingSliders();
    void SendSet(const std::string& id, scrapi::JsonValue value);
    Row* RowForControlId(int controlId, int* sub);
    int NextControlId(size_t rowIndex, int sub) const { return 1000 + static_cast<int>(rowIndex) * 64 + sub; }
    HWND MakeChild(const wchar_t* cls, const std::wstring& text, DWORD style, int id, DWORD exStyle = 0);
    static std::wstring Widen(const std::string& s);
    static std::string Narrow(const std::wstring& s);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    HFONT font_ = nullptr;
    HFONT boldFont_ = nullptr;
    const scrapi::ControlModel* model_ = nullptr;
    std::vector<Row> rows_;
    std::map<std::string, size_t> rowById_;
    SetFn onSet_;
    InvokeFn onInvoke_;
    int scrollPos_ = 0;
    int contentHeight_ = 0;
    bool batching_ = false; // Rebuild in progress: defer the full repaint to the end, once
    bool updating_ = false; // suppress edits echoing back while we set widget values
    std::map<std::string, scrapi::JsonValue> pendingSliders_;
};

} // namespace viewer
