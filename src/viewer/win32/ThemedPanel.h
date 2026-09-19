#pragma once
// The controller panel as ONE custom-drawn window (docs/DESIGN_VIEWER_UI.md): Direct2D +
// DirectWrite execute the display list viewer::PanelController produces, and mouse/keyboard
// input goes straight back into it. Replaces the earlier panel built from ~500 native child
// windows, which was slow to build and flickered when scrolled. Text editing, file and color
// choosing and confirmations use the standard Windows dialogs/edit control.

#include <windows.h>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "../../scrapi/ControlModel.h"
#include "../core/PanelController.h"
#include "../core/Theme.h"

struct ID2D1Factory;
struct ID2D1HwndRenderTarget;
struct ID2D1SolidColorBrush;
struct ID2D1StrokeStyle;
struct IDWriteFactory;

namespace viewer {

class ThemedPanel {
public:
    using SetFn = std::function<void(const std::string& id, const scrapi::JsonValue& value)>;
    using InvokeFn = std::function<void(const std::string& id)>;

    ThemedPanel();
    ~ThemedPanel();
    ThemedPanel(const ThemedPanel&) = delete;
    ThemedPanel& operator=(const ThemedPanel&) = delete;

    bool Create(HWND parent, HINSTANCE instance);
    HWND hwnd() const { return hwnd_; }
    void SetCallbacks(SetFn onSet, InvokeFn onInvoke) {
        onSet_ = std::move(onSet);
        onInvoke_ = std::move(onInvoke);
    }

    // `model` must outlive its use here (until the next Rebuild/Clear). Null clears.
    void Rebuild(const scrapi::ControlModel* model);
    void Clear() { Rebuild(nullptr); }
    // Values in the model changed (visibility may have too).
    void Refresh(const std::vector<std::string>& changedIds);

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam);

private:
    struct Engine; // Direct2D / DirectWrite state (defined in the .cpp)

    void Render();
    void Handle(const InputResult& result);
    void FlushPendingSets();
    void BeginEdit(const PanelAction& action);
    void EndEdit(bool commit);
    void Invalidate() { InvalidateRect(hwnd_, nullptr, FALSE); }
    static LRESULT CALLBACK PanelProc(HWND, UINT, WPARAM, LPARAM);

    HWND hwnd_ = nullptr;
    HINSTANCE instance_ = nullptr;
    std::unique_ptr<Engine> engine_;
    std::unique_ptr<PanelController> controller_;
    SetFn onSet_;
    InvokeFn onInvoke_;
    std::map<std::string, scrapi::JsonValue> pendingSets_;
    bool trackingMouse_ = false;
    HWND edit_ = nullptr;
    HFONT editFont_ = nullptr;
    std::string editId_;
};

} // namespace viewer
