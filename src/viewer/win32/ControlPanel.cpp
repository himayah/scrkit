#include "ControlPanel.h"

#include <commctrl.h>
#include <commdlg.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "../../scrapi/Manifest.h"

namespace viewer {

using scrapi::ControlNode;
using scrapi::ControlType;
using scrapi::JsonValue;

namespace {

constexpr wchar_t kPanelClass[] = L"ScrViewerControlPanel";
constexpr int kMargin = 8;
constexpr int kRowGap = 4;
constexpr int kLabelMaxWidth = 150;
constexpr int kLineHeight = 22;
constexpr int kIndent = 12;
constexpr UINT_PTR kSliderTimerId = 1;
constexpr UINT_PTR kCompositeTimerId = 2;
constexpr UINT kCompositeDelayMs = 250;
constexpr int kSliderSteps = 1000;
constexpr int kComboDropHeight = 220;

LRESULT CALLBACK PanelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* panel = reinterpret_cast<ControlPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (panel) {
        const LRESULT handled = panel->HandleMessage(message, wParam, lParam);
        if (handled != -1) return handled;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool IsSlider(const ControlNode& n) { return n.type == ControlType::Int || n.type == ControlType::Float; }

// Slider position <-> control value. A small integer range maps 1:1; everything else
// uses a fixed 0..kSliderSteps scale over [min,max].
struct SliderScale {
    double min = 0.0;
    double max = 100.0;
    int steps = kSliderSteps;
    bool direct = false; // integer control with a small range: position == value - min
};

SliderScale ScaleFor(const ControlNode& n) {
    SliderScale s;
    if (n.hasMin) s.min = n.min;
    s.max = n.hasMax ? n.max : s.min + 100.0;
    if (s.max <= s.min) s.max = s.min + 1.0;
    if (n.type == ControlType::Int && (s.max - s.min) <= 10000.0) {
        s.direct = true;
        s.steps = static_cast<int>(s.max - s.min);
    }
    return s;
}

double SliderToValue(const ControlNode& n, int pos) {
    const SliderScale s = ScaleFor(n);
    double v = s.direct ? s.min + pos : s.min + (s.max - s.min) * pos / s.steps;
    if (!s.direct && n.hasStep && n.step > 0.0) v = s.min + std::round((v - s.min) / n.step) * n.step;
    return std::clamp(v, s.min, s.max);
}

int ValueToSlider(const ControlNode& n, double v) {
    const SliderScale s = ScaleFor(n);
    const double t = s.direct ? v - s.min : (v - s.min) / (s.max - s.min) * s.steps;
    return std::clamp(static_cast<int>(std::lround(t)), 0, s.steps);
}

std::wstring FormatNumber(const ControlNode& n, double v) {
    wchar_t buf[64];
    if (n.type == ControlType::Int) {
        swprintf(buf, 64, L"%lld", static_cast<long long>(std::llround(v)));
    } else {
        swprintf(buf, 64, L"%.4g", v);
    }
    std::wstring text = buf;
    if (!n.unit.empty()) {
        std::wstring unit(n.unit.begin(), n.unit.end());
        text += L" " + unit;
    }
    return text;
}

COLORREF ParseColor(const std::string& text) {
    unsigned r = 0, g = 0, b = 0;
    if (text.size() >= 7 && text[0] == '#' && std::sscanf(text.c_str() + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
        return RGB(r, g, b);
    }
    return RGB(0, 0, 0);
}

} // namespace

std::wstring ControlPanel::Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string ControlPanel::Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

ControlPanel::~ControlPanel() {
    if (boldFont_) DeleteObject(boldFont_);
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

bool ControlPanel::Create(HWND parent, HINSTANCE instance) {
    instance_ = instance;
    WNDCLASSW wc{};
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    wc.lpszClassName = kPanelClass;
    RegisterClassW(&wc); // already registered on a second call: harmless

    font_ = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    LOGFONTW lf{};
    GetObjectW(font_, sizeof(lf), &lf);
    lf.lfWeight = FW_BOLD;
    boldFont_ = CreateFontIndirectW(&lf);

    // (Double buffering is enabled later, after the first display -- see SetComposited.)
    hwnd_ = CreateWindowExW(WS_EX_CONTROLPARENT, kPanelClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                            0, 0, 100, 100, parent, nullptr, instance, this);
    return hwnd_ != nullptr;
}

HWND ControlPanel::MakeChild(const wchar_t* cls, const std::wstring& text, DWORD style, int id, DWORD exStyle) {
    HWND h = CreateWindowExW(exStyle, cls, text.c_str(), style | WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance_, nullptr);
    if (h) SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    return h;
}

ControlPanel::Row* ControlPanel::RowForControlId(int controlId, int* sub) {
    if (controlId < 1000) return nullptr;
    const size_t index = static_cast<size_t>((controlId - 1000) / 64);
    if (index >= rows_.size()) return nullptr;
    if (sub) *sub = (controlId - 1000) % 64;
    return &rows_[index];
}

void ControlPanel::SetComposited(bool on) {
    const LONG_PTR style = GetWindowLongPtrW(hwnd_, GWL_EXSTYLE);
    const LONG_PTR wanted = on ? (style | WS_EX_COMPOSITED) : (style & ~static_cast<LONG_PTR>(WS_EX_COMPOSITED));
    if (wanted != style) SetWindowLongPtrW(hwnd_, GWL_EXSTYLE, wanted);
}

void ControlPanel::Rebuild(const scrapi::ControlModel* model) {
    KillTimer(hwnd_, kCompositeTimerId);
    SetComposited(false);
    KillTimer(hwnd_, kSliderTimerId);
    pendingSliders_.clear();
    for (Row& row : rows_) {
        if (row.label) DestroyWindow(row.label);
        if (row.widget) DestroyWindow(row.widget);
        if (row.aux) DestroyWindow(row.aux);
        for (HWND h : row.items) DestroyWindow(h);
    }
    rows_.clear();
    rowById_.clear();
    model_ = model;
    if (!model_) {
        scrollPos_ = 0;
        Relayout();
        return;
    }

    struct Walker {
        ControlPanel& panel;
        void Visit(const std::vector<ControlNode>& nodes, int depth) {
            for (const ControlNode& n : nodes) {
                panel.CreateRow(n, depth);
                Visit(n.children, depth + 1);
            }
        }
    } walker{*this};
    walker.Visit(model_->manifest().controls, 0);

    // Build everything with a single layout and a single full repaint at the end.
    batching_ = true;
    for (Row& row : rows_) UpdateWidget(row);
    ApplyVisibility(/*force=*/true);
    batching_ = false;
    RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    SetTimer(hwnd_, kCompositeTimerId, kCompositeDelayMs, nullptr); // double-buffer once it's on screen
}

void ControlPanel::CreateRow(const ControlNode& node, int depth) {
    rows_.emplace_back();
    Row& row = rows_.back();
    row.node = &node;
    row.depth = depth;
    row.visible = false; // not shown until ApplyVisibility says so
    rowById_[node.id] = rows_.size() - 1;
}

void ControlPanel::BuildRowWindows(size_t index) {
    Row& row = rows_[index];
    const ControlNode& node = *row.node;
    row.created = true;
    const std::wstring title = Widen(node.label.empty() ? node.id : node.label);

    if (node.type == ControlType::Group) {
        row.label = MakeChild(L"STATIC", title, SS_LEFT | SS_ENDELLIPSIS, 0);
        SendMessageW(row.label, WM_SETFONT, reinterpret_cast<WPARAM>(boldFont_), TRUE);
        row.height = 20;
        return;
    }
    if (node.type != ControlType::Button) {
        row.label = MakeChild(L"STATIC", title, SS_LEFT | SS_ENDELLIPSIS, 0);
    }
    row.height = kLineHeight;

    switch (node.type) {
        case ControlType::Enum: {
            const bool radio = node.presentation == "radio" && node.options.size() <= 8;
            if (radio) {
                for (size_t i = 0; i < node.options.size(); ++i) {
                    DWORD style = BS_AUTORADIOBUTTON | WS_TABSTOP;
                    if (i == 0) style |= WS_GROUP;
                    row.items.push_back(MakeChild(L"BUTTON", Widen(node.options[i].label), style,
                                                  NextControlId(index, static_cast<int>(i))));
                }
                row.height = std::max<int>(kLineHeight, static_cast<int>(node.options.size()) * kLineHeight);
            } else {
                row.widget = MakeChild(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL | WS_TABSTOP, NextControlId(index, 0));
                for (const auto& o : node.options) {
                    SendMessageW(row.widget, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Widen(o.label).c_str()));
                }
            }
            break;
        }
        case ControlType::Flags: {
            for (size_t i = 0; i < node.options.size() && i < 63; ++i) {
                row.items.push_back(MakeChild(L"BUTTON", Widen(node.options[i].label), BS_AUTOCHECKBOX | WS_TABSTOP,
                                              NextControlId(index, static_cast<int>(i))));
            }
            row.height = std::max<int>(kLineHeight, static_cast<int>(row.items.size()) * kLineHeight);
            break;
        }
        case ControlType::Bool:
            row.widget = MakeChild(L"BUTTON", L"", BS_AUTOCHECKBOX | WS_TABSTOP, NextControlId(index, 0));
            break;
        case ControlType::Int:
        case ControlType::Float: {
            row.widget = MakeChild(TRACKBAR_CLASSW, L"", TBS_HORZ | TBS_NOTICKS | WS_TABSTOP, NextControlId(index, 0));
            SendMessageW(row.widget, TBM_SETRANGE, TRUE, MAKELPARAM(0, ScaleFor(node).steps));
            row.aux = MakeChild(L"STATIC", L"", SS_RIGHT, 0);
            break;
        }
        case ControlType::String: {
            DWORD style = ES_AUTOHSCROLL | WS_TABSTOP;
            if (node.multiline) {
                style = ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | WS_VSCROLL | WS_TABSTOP;
                row.height = 64;
            }
            row.widget = MakeChild(L"EDIT", L"", style, NextControlId(index, 0), WS_EX_CLIENTEDGE);
            break;
        }
        case ControlType::Path:
            row.widget = MakeChild(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, NextControlId(index, 0), WS_EX_CLIENTEDGE);
            row.aux = MakeChild(L"BUTTON", L"...", BS_PUSHBUTTON | WS_TABSTOP, NextControlId(index, 1));
            break;
        case ControlType::Color:
            row.widget = MakeChild(L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, NextControlId(index, 0));
            break;
        case ControlType::Button:
            row.widget = MakeChild(L"BUTTON", title, BS_PUSHBUTTON | WS_TABSTOP, NextControlId(index, 0));
            row.height = 26;
            break;
        case ControlType::Readout:
        case ControlType::Unknown:
        default: {
            DWORD style = SS_LEFT | SS_ENDELLIPSIS;
            if (node.multiline) {
                style = SS_LEFT;
                row.height = 64;
            }
            row.widget = MakeChild(L"STATIC", L"", style, 0);
            break;
        }
    }
}

void ControlPanel::UpdateWidget(Row& row) {
    if (!model_ || !row.node || !row.created) return;
    const ControlNode& n = *row.node;
    const JsonValue* value = model_->Get(n.id);
    if (!value) return;
    updating_ = true;

    switch (n.type) {
        case ControlType::Enum: {
            int selected = -1;
            for (size_t i = 0; i < n.options.size(); ++i) {
                if (n.options[i].value == value->AsString()) selected = static_cast<int>(i);
            }
            if (!row.items.empty()) {
                for (size_t i = 0; i < row.items.size(); ++i) {
                    SendMessageW(row.items[i], BM_SETCHECK, static_cast<int>(i) == selected ? BST_CHECKED : BST_UNCHECKED, 0);
                }
            } else if (row.widget) {
                SendMessageW(row.widget, CB_SETCURSEL, static_cast<WPARAM>(selected), 0);
            }
            break;
        }
        case ControlType::Flags:
            for (size_t i = 0; i < row.items.size(); ++i) {
                bool on = false;
                for (const auto& item : value->items()) on = on || item.AsString() == n.options[i].value;
                SendMessageW(row.items[i], BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
            }
            break;
        case ControlType::Bool:
            SendMessageW(row.widget, BM_SETCHECK, value->AsBool() ? BST_CHECKED : BST_UNCHECKED, 0);
            break;
        case ControlType::Int:
        case ControlType::Float:
            // A slider being dragged right now owns its position; don't fight it.
            if (!pendingSliders_.count(n.id)) SendMessageW(row.widget, TBM_SETPOS, TRUE, ValueToSlider(n, value->AsDouble()));
            SetWindowTextW(row.aux, FormatNumber(n, value->AsDouble()).c_str());
            break;
        case ControlType::String:
        case ControlType::Path:
            if (GetFocus() != row.widget) SetWindowTextW(row.widget, Widen(value->AsString()).c_str());
            break;
        case ControlType::Color:
            row.color = ParseColor(value->AsString());
            InvalidateRect(row.widget, nullptr, TRUE);
            break;
        case ControlType::Readout:
        case ControlType::Unknown: {
            std::wstring text;
            if (value->IsString()) text = Widen(value->AsString());
            else if (value->IsNumber()) text = FormatNumber(n, value->AsDouble());
            else if (value->IsBool()) text = value->AsBool() ? L"true" : L"false";
            else if (value->IsNull()) text = L"-";
            else text = Widen(scrapi::SerializeJson(*value));
            SetWindowTextW(row.widget, text.c_str());
            break;
        }
        default: break;
    }
    updating_ = false;
}

void ControlPanel::Refresh(const std::vector<std::string>& changedIds) {
    if (!model_) return;
    for (const auto& id : changedIds) {
        auto it = rowById_.find(id);
        if (it != rowById_.end()) UpdateWidget(rows_[it->second]);
    }
    ApplyVisibility();
}

void ControlPanel::ApplyVisibility(bool force) {
    if (!model_) {
        Relayout();
        return;
    }
    bool layoutChanged = force;
    for (Row& row : rows_) {
        const std::string& id = row.node->id;
        const bool visible = model_->IsVisible(id);
        const bool enabled = model_->IsEnabled(id) && !(row.node->readOnly && row.node->type != ControlType::Readout);
        if (!force && visible == row.visible && enabled == row.enabled) continue;
        if (visible != row.visible) layoutChanged = true;
        row.visible = visible;
        row.enabled = enabled;
        if (row.visible && !row.created) {
            BuildRowWindows(static_cast<size_t>(&row - rows_.data()));
            UpdateWidget(row);
        }
        const int show = row.visible ? SW_SHOWNA : SW_HIDE;
        auto apply = [&](HWND h) {
            if (!h) return;
            ShowWindow(h, show);
            EnableWindow(h, enabled ? TRUE : FALSE);
        };
        apply(row.label);
        apply(row.widget);
        apply(row.aux);
        for (HWND h : row.items) apply(h);
    }
    if (layoutChanged) Relayout();
}

void ControlPanel::Relayout(bool fullRedraw) {
    if (!hwnd_) return;
    for (int pass = 0; pass < 2; ++pass) {
        RECT client{};
        GetClientRect(hwnd_, &client);
        const int width = std::max<int>(1, client.right - client.left);
        const int pageHeight = std::max<int>(1, client.bottom - client.top);

        int y = kMargin - scrollPos_;
        // One atomic batch move. When repainting everything anyway, don't copy stale bits.
        HDWP batch = BeginDeferWindowPos(static_cast<int>(rows_.size()) * 4 + 16);
        const UINT moveFlags = SWP_NOZORDER | SWP_NOACTIVATE | (fullRedraw ? SWP_NOCOPYBITS : 0);
        auto place = [&](HWND h, int x, int yy, int w, int hgt) {
            if (!h) return;
            if (batch) {
                HDWP next = DeferWindowPos(batch, h, nullptr, x, yy, w, hgt, moveFlags);
                if (next) {
                    batch = next;
                    return;
                }
                batch = nullptr; // the batch is unusable now; fall back to individual moves
            }
            SetWindowPos(h, nullptr, x, yy, w, hgt, moveFlags);
        };
        for (Row& row : rows_) {
            if (!row.visible) continue;
            const int x = kMargin + row.depth * kIndent;
            if (row.node->type == ControlType::Group) {
                place(row.label, x, y + 2, width - x - kMargin, 18);
                y += row.height + kRowGap;
                continue;
            }
            const int labelWidth = std::min(kLabelMaxWidth, width * 2 / 5);
            const int wx = x + labelWidth + 6;
            const int ww = std::max(40, width - wx - kMargin);
            if (row.node->type == ControlType::Button) {
                place(row.widget, x, y, std::min(ww + labelWidth, 220), row.height);
            } else {
                place(row.label, x, y + 3, labelWidth, 18);
                if (!row.items.empty()) {
                    for (size_t i = 0; i < row.items.size(); ++i) {
                        place(row.items[i], wx, y + static_cast<int>(i) * kLineHeight, ww, kLineHeight);
                    }
                } else if (row.node->type == ControlType::Enum) {
                    place(row.widget, wx, y, ww, kComboDropHeight); // height = dropdown extent
                } else if (IsSlider(*row.node)) {
                    place(row.widget, wx, y, std::max(30, ww - 62), row.height);
                    place(row.aux, wx + std::max(30, ww - 62) + 2, y + 3, 58, 18);
                } else if (row.node->type == ControlType::Path) {
                    place(row.widget, wx, y, std::max(30, ww - 28), 22);
                    place(row.aux, wx + std::max(30, ww - 28) + 2, y, 26, 22);
                } else if (row.node->type == ControlType::Color) {
                    place(row.widget, wx, y, 64, 22);
                } else if (row.node->type == ControlType::Bool) {
                    place(row.widget, wx, y, 22, 22);
                } else {
                    place(row.widget, wx, y + (row.node->type == ControlType::String ? 0 : 3), ww, row.node->type == ControlType::String ? row.height : row.height - 4);
                }
            }
            y += row.height + kRowGap;
        }
        if (batch) EndDeferWindowPos(batch);
        contentHeight_ = y + scrollPos_ + kMargin;
        const int maxScroll = std::max(0, contentHeight_ - pageHeight);
        if (scrollPos_ > maxScroll) {
            scrollPos_ = maxScroll;
            continue; // re-run the layout at the clamped scroll position
        }
        SCROLLINFO si{};
        si.cbSize = sizeof(si);
        si.fMask = SIF_RANGE | SIF_PAGE | SIF_POS;
        si.nMin = 0;
        si.nMax = contentHeight_;
        si.nPage = static_cast<UINT>(pageHeight);
        si.nPos = scrollPos_;
        SetScrollInfo(hwnd_, SB_VERT, &si, TRUE);
        break;
    }
    if (fullRedraw && !batching_) {
        // Window resized / rows shown or hidden / rebuilt: repaint the panel and every child
        // from scratch so no ghost of an earlier layout is left behind.
        RedrawWindow(hwnd_, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
}

void ControlPanel::SendSet(const std::string& id, JsonValue value) {
    if (onSet_) onSet_(id, value);
}

void ControlPanel::OnSlider(HWND slider, bool final) {
    if (updating_) return;
    for (Row& row : rows_) {
        if (row.widget != slider || !IsSlider(*row.node)) continue;
        const int pos = static_cast<int>(SendMessageW(slider, TBM_GETPOS, 0, 0));
        const double v = SliderToValue(*row.node, pos);
        SetWindowTextW(row.aux, FormatNumber(*row.node, v).c_str());
        const bool wasIdle = pendingSliders_.empty();
        pendingSliders_[row.node->id] = row.node->type == ControlType::Int ? JsonValue::Int(static_cast<int64_t>(std::llround(v)))
                                                                             : JsonValue::Double(v);
        if (final) FlushPendingSliders();
        else if (wasIdle) SetTimer(hwnd_, kSliderTimerId, 33, nullptr); // coalesce a drag into ~30 updates/s
        return;
    }
}

void ControlPanel::FlushPendingSliders() {
    KillTimer(hwnd_, kSliderTimerId);
    std::map<std::string, JsonValue> pending;
    pending.swap(pendingSliders_); // clear first so replies can update the sliders again
    for (const auto& kv : pending) SendSet(kv.first, kv.second);
}

void ControlPanel::OnCommand(int controlId, int notification, HWND source) {
    if (updating_) return;
    int sub = 0;
    Row* row = RowForControlId(controlId, &sub);
    if (!row || !row->node) return;
    const ControlNode& n = *row->node;

    switch (n.type) {
        case ControlType::Enum:
            if (!row->items.empty()) {
                if (notification == BN_CLICKED && sub < static_cast<int>(n.options.size())) {
                    SendSet(n.id, JsonValue::String(n.options[static_cast<size_t>(sub)].value));
                }
            } else if (notification == CBN_SELCHANGE) {
                const int sel = static_cast<int>(SendMessageW(row->widget, CB_GETCURSEL, 0, 0));
                if (sel >= 0 && sel < static_cast<int>(n.options.size())) {
                    SendSet(n.id, JsonValue::String(n.options[static_cast<size_t>(sel)].value));
                }
            }
            break;
        case ControlType::Flags:
            if (notification == BN_CLICKED) {
                JsonValue chosen = JsonValue::Array();
                for (size_t i = 0; i < row->items.size(); ++i) {
                    if (SendMessageW(row->items[i], BM_GETCHECK, 0, 0) == BST_CHECKED) {
                        chosen.Push(JsonValue::String(n.options[i].value));
                    }
                }
                SendSet(n.id, std::move(chosen));
            }
            break;
        case ControlType::Bool:
            if (notification == BN_CLICKED) {
                SendSet(n.id, JsonValue::Bool(SendMessageW(row->widget, BM_GETCHECK, 0, 0) == BST_CHECKED));
            }
            break;
        case ControlType::String:
            if (notification == EN_KILLFOCUS) {
                wchar_t buf[2048] = {};
                GetWindowTextW(row->widget, buf, 2048);
                SendSet(n.id, JsonValue::String(Narrow(buf)));
            }
            break;
        case ControlType::Path:
            if (source == row->aux && notification == BN_CLICKED) {
                std::wstring filter;
                if (n.filters.empty()) {
                    filter = std::wstring(L"All files") + L'\0' + L"*.*" + L'\0';
                } else {
                    for (const auto& f : n.filters) filter += Widen(f.first) + L'\0' + Widen(f.second) + L'\0';
                }
                wchar_t file[MAX_PATH] = {};
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd_;
                ofn.lpstrFilter = filter.c_str();
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn)) {
                    SetWindowTextW(row->widget, file);
                    SendSet(n.id, JsonValue::String(Narrow(file)));
                }
            } else if (source == row->widget && notification == EN_KILLFOCUS) {
                wchar_t buf[MAX_PATH] = {};
                GetWindowTextW(row->widget, buf, MAX_PATH);
                SendSet(n.id, JsonValue::String(Narrow(buf)));
            }
            break;
        case ControlType::Color:
            if (notification == BN_CLICKED) {
                static COLORREF custom[16] = {};
                CHOOSECOLORW cc{};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd_;
                cc.rgbResult = row->color;
                cc.lpCustColors = custom;
                cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                if (ChooseColorW(&cc)) {
                    char text[16];
                    std::snprintf(text, sizeof(text), "#%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult),
                                  GetBValue(cc.rgbResult));
                    std::string color = text;
                    if (n.alpha) color += "FF";
                    SendSet(n.id, JsonValue::String(color));
                }
            }
            break;
        case ControlType::Button:
            if (notification == BN_CLICKED) {
                if (!n.confirm.empty() &&
                    MessageBoxW(hwnd_, Widen(n.confirm).c_str(), Widen(n.label).c_str(), MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
                    break;
                }
                if (onInvoke_) onInvoke_(n.id);
            }
            break;
        default: break;
    }
}

LRESULT ControlPanel::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
        case WM_SIZE:
            Relayout();
            return 0;
        case WM_VSCROLL: {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int page = client.bottom - client.top;
            int pos = scrollPos_;
            switch (LOWORD(wParam)) {
                case SB_LINEUP: pos -= 24; break;
                case SB_LINEDOWN: pos += 24; break;
                case SB_PAGEUP: pos -= page; break;
                case SB_PAGEDOWN: pos += page; break;
                case SB_TOP: pos = 0; break;
                case SB_BOTTOM: pos = contentHeight_; break;
                case SB_THUMBTRACK:
                case SB_THUMBPOSITION: {
                    SCROLLINFO si{};
                    si.cbSize = sizeof(si);
                    si.fMask = SIF_TRACKPOS;
                    GetScrollInfo(hwnd_, SB_VERT, &si);
                    pos = si.nTrackPos;
                    break;
                }
                default: break;
            }
            scrollPos_ = std::clamp(pos, 0, std::max(0, contentHeight_ - page));
            Relayout(/*fullRedraw=*/false);
            return 0;
        }
        case WM_MOUSEWHEEL: {
            RECT client{};
            GetClientRect(hwnd_, &client);
            const int page = client.bottom - client.top;
            const int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            scrollPos_ = std::clamp(scrollPos_ - delta / WHEEL_DELTA * 48, 0, std::max(0, contentHeight_ - page));
            Relayout(/*fullRedraw=*/false);
            return 0;
        }
        case WM_HSCROLL:
            OnSlider(reinterpret_cast<HWND>(lParam), LOWORD(wParam) == TB_ENDTRACK);
            return 0;
        case WM_COMMAND:
            OnCommand(LOWORD(wParam), HIWORD(wParam), reinterpret_cast<HWND>(lParam));
            return 0;
        case WM_TIMER:
            if (wParam == kSliderTimerId) {
                FlushPendingSliders();
                return 0;
            }
            if (wParam == kCompositeTimerId) {
                KillTimer(hwnd_, kCompositeTimerId);
                SetComposited(true);
                return 0;
            }
            return -1;
        case WM_DRAWITEM: {
            const auto* dis = reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);
            for (const Row& row : rows_) {
                if (row.widget != dis->hwndItem || row.node->type != ControlType::Color) continue;
                HBRUSH brush = CreateSolidBrush(row.color);
                FillRect(dis->hDC, &dis->rcItem, brush);
                DeleteObject(brush);
                FrameRect(dis->hDC, &dis->rcItem, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
                return TRUE;
            }
            return -1;
        }
        default:
            return -1; // not handled: let DefWindowProc run
    }
}

} // namespace viewer
