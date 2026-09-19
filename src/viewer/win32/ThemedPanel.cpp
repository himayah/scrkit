#include "ThemedPanel.h"

#include <commctrl.h>
#include <commdlg.h>
#include <d2d1.h>
#include <dwrite.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace viewer {

namespace {

constexpr wchar_t kPanelClass[] = L"ScrViewerThemedPanel";
constexpr UINT_PTR kThrottleTimerId = 1;
constexpr UINT kThrottleMs = 33;   // coalesce a slider drag into ~30 updates/s
constexpr UINT kMsgEndEdit = WM_APP + 10;
constexpr float kPi = 3.14159265358979f;

template <class T>
void SafeRelease(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

std::wstring Widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string Narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

D2D1_COLOR_F ToD2D(const Color& c) {
    return D2D1::ColorF(c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f);
}

D2D1_POINT_2F Pt(const Point& p) { return D2D1::Point2F(p.x, p.y); }
D2D1_RECT_F RectF(const Rect& r) { return D2D1::RectF(r.x, r.y, r.x + r.w, r.y + r.h); }

} // namespace

// Direct2D / DirectWrite state, plus the text measurer the controller lays out with.
struct ThemedPanel::Engine : ITextMeasurer {
    ID2D1Factory* d2d = nullptr;
    IDWriteFactory* dwrite = nullptr;
    ID2D1HwndRenderTarget* target = nullptr;
    ID2D1SolidColorBrush* brush = nullptr;
    ID2D1StrokeStyle* roundStroke = nullptr;
    std::wstring family;
    std::map<int, IDWriteTextFormat*> formats; // key: size*10 | bold | align

    ~Engine() {
        DiscardTarget();
        for (auto& kv : formats) SafeRelease(kv.second);
        SafeRelease(roundStroke);
        SafeRelease(dwrite);
        SafeRelease(d2d);
    }

    bool Init() {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &d2d))) return false;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&dwrite)))) return false;
        d2d->CreateStrokeStyle(D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
                               nullptr, 0, &roundStroke);
        // Segoe UI Variable is the Windows 11 UI font; older systems fall back to Segoe UI.
        family = L"Segoe UI";
        IDWriteFontCollection* collection = nullptr;
        if (SUCCEEDED(dwrite->GetSystemFontCollection(&collection, FALSE)) && collection) {
            UINT32 index = 0;
            BOOL exists = FALSE;
            if (SUCCEEDED(collection->FindFamilyName(L"Segoe UI Variable Text", &index, &exists)) && exists) family = L"Segoe UI Variable Text";
            collection->Release();
        }
        return true;
    }

    void DiscardTarget() {
        SafeRelease(brush);
        SafeRelease(target);
    }

    bool EnsureTarget(HWND hwnd) {
        if (target) return true;
        RECT rc{};
        GetClientRect(hwnd, &rc);
        if (rc.right <= 0 || rc.bottom <= 0) return false;
        if (FAILED(d2d->CreateHwndRenderTarget(D2D1::RenderTargetProperties(),
                                               D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(rc.right, rc.bottom)), &target))) {
            return false;
        }
        target->SetDpi(96.0f, 96.0f); // the app is DPI-unaware: 1 unit = 1 pixel
        target->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &brush);
        return brush != nullptr;
    }

    IDWriteTextFormat* Format(float size, bool bold, TextAlign align) {
        const int key = static_cast<int>(size * 10.0f) * 16 + (bold ? 8 : 0) + static_cast<int>(align);
        auto it = formats.find(key);
        if (it != formats.end()) return it->second;
        IDWriteTextFormat* fmt = nullptr;
        if (FAILED(dwrite->CreateTextFormat(family.c_str(), nullptr, bold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &fmt))) {
            return nullptr;
        }
        fmt->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
        fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
        fmt->SetTextAlignment(align == TextAlign::Center ? DWRITE_TEXT_ALIGNMENT_CENTER
                              : align == TextAlign::Right ? DWRITE_TEXT_ALIGNMENT_TRAILING
                                                          : DWRITE_TEXT_ALIGNMENT_LEADING);
        IDWriteInlineObject* ellipsis = nullptr;
        if (SUCCEEDED(dwrite->CreateEllipsisTrimmingSign(fmt, &ellipsis))) {
            DWRITE_TRIMMING trimming{DWRITE_TRIMMING_GRANULARITY_CHARACTER, 0, 0};
            fmt->SetTrimming(&trimming, ellipsis);
            ellipsis->Release();
        }
        formats[key] = fmt;
        return fmt;
    }

    float Width(const std::string& utf8, float fontSize, bool bold) override {
        IDWriteTextFormat* fmt = Format(fontSize, bold, TextAlign::Left);
        if (!fmt || utf8.empty()) return 0.0f;
        const std::wstring w = Widen(utf8);
        IDWriteTextLayout* layout = nullptr;
        if (FAILED(dwrite->CreateTextLayout(w.c_str(), static_cast<UINT32>(w.size()), fmt, 10000.0f, 100.0f, &layout))) {
            return static_cast<float>(utf8.size()) * fontSize * 0.55f;
        }
        DWRITE_TEXT_METRICS m{};
        layout->GetMetrics(&m);
        layout->Release();
        return m.widthIncludingTrailingWhitespace;
    }

    void Execute(const DrawList& list) {
        for (const DrawCmd& c : list.cmds) {
            brush->SetColor(ToD2D(c.color));
            switch (c.kind) {
                case DrawCmd::Kind::FillRoundRect:
                    if (c.radius > 0.0f) target->FillRoundedRectangle(D2D1::RoundedRect(RectF(c.rect), c.radius, c.radius), brush);
                    else target->FillRectangle(RectF(c.rect), brush);
                    break;
                case DrawCmd::Kind::StrokeRoundRect: {
                    const Rect in = c.rect.Inset(c.strokeWidth / 2);
                    target->DrawRoundedRectangle(D2D1::RoundedRect(RectF(in), std::max(0.0f, c.radius - c.strokeWidth / 2), std::max(0.0f, c.radius - c.strokeWidth / 2)),
                                                 brush, c.strokeWidth);
                    break;
                }
                case DrawCmd::Kind::FillEllipse: {
                    const Point m = c.rect.Center();
                    target->FillEllipse(D2D1::Ellipse(Pt(m), c.rect.w / 2, c.rect.h / 2), brush);
                    break;
                }
                case DrawCmd::Kind::StrokeEllipse: {
                    const Point m = c.rect.Center();
                    target->DrawEllipse(D2D1::Ellipse(Pt(m), c.rect.w / 2 - c.strokeWidth / 2, c.rect.h / 2 - c.strokeWidth / 2), brush, c.strokeWidth);
                    break;
                }
                case DrawCmd::Kind::Line:
                    if (c.points.size() == 2) target->DrawLine(Pt(c.points[0]), Pt(c.points[1]), brush, c.strokeWidth, roundStroke);
                    break;
                case DrawCmd::Kind::Polyline:
                    if (c.points.size() >= 2) {
                        ID2D1PathGeometry* geo = nullptr;
                        ID2D1GeometrySink* sink = nullptr;
                        if (SUCCEEDED(d2d->CreatePathGeometry(&geo)) && SUCCEEDED(geo->Open(&sink))) {
                            sink->BeginFigure(Pt(c.points[0]), D2D1_FIGURE_BEGIN_HOLLOW);
                            for (size_t i = 1; i < c.points.size(); ++i) sink->AddLine(Pt(c.points[i]));
                            sink->EndFigure(D2D1_FIGURE_END_OPEN);
                            sink->Close();
                            target->DrawGeometry(geo, brush, c.strokeWidth, roundStroke);
                        }
                        SafeRelease(sink);
                        SafeRelease(geo);
                    }
                    break;
                case DrawCmd::Kind::Arc: {
                    auto at = [&](float deg) {
                        const float a = (deg - 90.0f) * kPi / 180.0f;
                        return D2D1::Point2F(c.cx + c.r * std::cos(a), c.cy + c.r * std::sin(a));
                    };
                    ID2D1PathGeometry* geo = nullptr;
                    ID2D1GeometrySink* sink = nullptr;
                    if (SUCCEEDED(d2d->CreatePathGeometry(&geo)) && SUCCEEDED(geo->Open(&sink))) {
                        sink->BeginFigure(at(c.a0), D2D1_FIGURE_BEGIN_HOLLOW);
                        sink->AddArc(D2D1::ArcSegment(at(c.a1), D2D1::SizeF(c.r, c.r), 0.0f, D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                                      (c.a1 - c.a0) > 180.0f ? D2D1_ARC_SIZE_LARGE : D2D1_ARC_SIZE_SMALL));
                        sink->EndFigure(D2D1_FIGURE_END_OPEN);
                        sink->Close();
                        target->DrawGeometry(geo, brush, c.strokeWidth, roundStroke);
                    }
                    SafeRelease(sink);
                    SafeRelease(geo);
                    break;
                }
                case DrawCmd::Kind::Text: {
                    IDWriteTextFormat* fmt = Format(c.fontSize, c.bold, c.align);
                    if (!fmt || c.text.empty()) break;
                    const std::wstring w = Widen(c.text);
                    target->DrawText(w.c_str(), static_cast<UINT32>(w.size()), fmt, RectF(c.rect), brush, D2D1_DRAW_TEXT_OPTIONS_CLIP);
                    break;
                }
                case DrawCmd::Kind::PushClip:
                    target->PushAxisAlignedClip(RectF(c.rect), D2D1_ANTIALIAS_MODE_ALIASED);
                    break;
                case DrawCmd::Kind::PopClip:
                    target->PopAxisAlignedClip();
                    break;
            }
        }
    }
};

ThemedPanel::ThemedPanel() = default;

ThemedPanel::~ThemedPanel() {
    if (editFont_) DeleteObject(editFont_);
    if (hwnd_ && IsWindow(hwnd_)) DestroyWindow(hwnd_);
}

LRESULT CALLBACK ThemedPanel::PanelProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        // Messages (WM_SIZE, ...) arrive while CreateWindowEx is still running, before it returns.
        static_cast<ThemedPanel*>(create->lpCreateParams)->hwnd_ = hwnd;
    }
    auto* panel = reinterpret_cast<ThemedPanel*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (panel) return panel->HandleMessage(message, wParam, lParam);
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

bool ThemedPanel::Create(HWND parent, HINSTANCE instance) {
    instance_ = instance;
    engine_ = std::make_unique<Engine>();
    if (!engine_->Init()) return false;
    controller_ = std::make_unique<PanelController>(*engine_, Theme::WindowsLight());

    WNDCLASSW wc{};
    wc.lpfnWndProc = PanelProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kPanelClass;
    RegisterClassW(&wc);

    hwnd_ = CreateWindowExW(0, kPanelClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, 100, 100, parent, nullptr,
                            instance, this);
    return hwnd_ != nullptr;
}

void ThemedPanel::Rebuild(const scrapi::ControlModel* model) {
    if (!controller_) return;
    pendingSets_.clear();
    KillTimer(hwnd_, kThrottleTimerId);
    controller_->SetModel(model);
    Invalidate();
}

void ThemedPanel::Refresh(const std::vector<std::string>&) {
    if (!controller_) return;
    controller_->ValuesChanged();
    Invalidate();
}

void ThemedPanel::Render() {
    if (!engine_ || !controller_ || !engine_->EnsureTarget(hwnd_)) return;
    DrawList list;
    controller_->Paint(list);
    engine_->target->BeginDraw();
    engine_->target->Clear(ToD2D(Theme::WindowsLight().panel));
    engine_->Execute(list);
    const HRESULT hr = engine_->target->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        engine_->DiscardTarget();
        Invalidate();
    }
}

void ThemedPanel::FlushPendingSets() {
    KillTimer(hwnd_, kThrottleTimerId);
    std::map<std::string, scrapi::JsonValue> pending;
    pending.swap(pendingSets_);
    for (const auto& kv : pending) {
        if (onSet_) onSet_(kv.first, kv.second);
    }
}

void ThemedPanel::Handle(const InputResult& result) {
    for (const PanelAction& a : result.actions) {
        switch (a.kind) {
            case PanelAction::Kind::SetValue:
                if (a.continuous) {
                    const bool idle = pendingSets_.empty();
                    pendingSets_[a.id] = a.value;
                    if (idle) SetTimer(hwnd_, kThrottleTimerId, kThrottleMs, nullptr);
                } else {
                    pendingSets_.erase(a.id);
                    if (onSet_) onSet_(a.id, a.value);
                }
                break;
            case PanelAction::Kind::Invoke:
                if (onInvoke_) onInvoke_(a.id);
                break;
            case PanelAction::Kind::EditText:
                BeginEdit(a);
                break;
            case PanelAction::Kind::BrowsePath: {
                std::wstring filter;
                if (a.filters.empty()) {
                    filter = std::wstring(L"All files") + L'\0' + L"*.*" + L'\0';
                } else {
                    for (const auto& f : a.filters) filter += Widen(f.first) + L'\0' + Widen(f.second) + L'\0';
                }
                wchar_t file[MAX_PATH] = {};
                OPENFILENAMEW ofn{};
                ofn.lStructSize = sizeof(ofn);
                ofn.hwndOwner = hwnd_;
                ofn.lpstrFilter = filter.c_str();
                ofn.lpstrFile = file;
                ofn.nMaxFile = MAX_PATH;
                ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
                if (GetOpenFileNameW(&ofn)) Handle(controller_->ProvideValue(a.id, scrapi::JsonValue::String(Narrow(file))));
                break;
            }
            case PanelAction::Kind::PickColor: {
                unsigned r = 0, g = 0, b = 0;
                if (a.text.size() >= 7 && a.text[0] == '#') std::sscanf(a.text.c_str() + 1, "%2x%2x%2x", &r, &g, &b);
                static COLORREF custom[16] = {};
                CHOOSECOLORW cc{};
                cc.lStructSize = sizeof(cc);
                cc.hwndOwner = hwnd_;
                cc.rgbResult = RGB(r, g, b);
                cc.lpCustColors = custom;
                cc.Flags = CC_RGBINIT | CC_FULLOPEN;
                if (ChooseColorW(&cc)) {
                    char text[16];
                    std::snprintf(text, sizeof(text), "#%02X%02X%02X", GetRValue(cc.rgbResult), GetGValue(cc.rgbResult), GetBValue(cc.rgbResult));
                    std::string color = text;
                    if (a.text.size() == 9) color += a.text.substr(7, 2); // keep the existing alpha
                    Handle(controller_->ProvideValue(a.id, scrapi::JsonValue::String(color)));
                }
                break;
            }
            case PanelAction::Kind::Confirm:
                if (MessageBoxW(hwnd_, Widen(a.text).c_str(), L"ScrViewer", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                    Handle(controller_->ConfirmInvoke(a.id));
                }
                break;
        }
    }
    if (result.redraw) Invalidate();
}

// A native EDIT control is laid over the field while it is being edited; Enter or losing focus
// commits, Escape cancels.
namespace {
LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR ref) {
    HWND panel = reinterpret_cast<HWND>(ref);
    switch (message) {
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS | DLGC_HASSETSEL;
        case WM_KEYDOWN:
            if (wParam == VK_RETURN) {
                PostMessageW(panel, kMsgEndEdit, 1, 0);
                return 0;
            }
            if (wParam == VK_ESCAPE) {
                PostMessageW(panel, kMsgEndEdit, 0, 0);
                return 0;
            }
            break;
        case WM_CHAR:
            if (wParam == VK_RETURN || wParam == VK_ESCAPE) return 0; // no beep
            break;
        case WM_KILLFOCUS:
            PostMessageW(panel, kMsgEndEdit, 1, 0);
            break;
        default: break;
    }
    return DefSubclassProc(hwnd, message, wParam, lParam);
}
} // namespace

void ThemedPanel::BeginEdit(const PanelAction& a) {
    if (edit_) EndEdit(true);
    if (!editFont_) editFont_ = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                             CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    const int x = static_cast<int>(a.rect.x) + 2, y = static_cast<int>(a.rect.y) + 6;
    const int w = std::max(20, static_cast<int>(a.rect.w) - 4), h = std::max(16, static_cast<int>(a.rect.h) - 12);
    edit_ = CreateWindowExW(0, L"EDIT", Widen(a.text).c_str(), WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL, x, y, w, h, hwnd_, nullptr,
                            instance_, nullptr);
    if (!edit_) return;
    editId_ = a.id;
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(editFont_), TRUE);
    SetWindowSubclass(edit_, EditSubclassProc, 1, reinterpret_cast<DWORD_PTR>(hwnd_));
    SendMessageW(edit_, EM_SETSEL, 0, -1);
    SetFocus(edit_);
}

void ThemedPanel::EndEdit(bool commit) {
    if (!edit_) return;
    wchar_t buf[2048] = {};
    GetWindowTextW(edit_, buf, 2048);
    const std::string id = editId_;
    HWND edit = edit_;
    edit_ = nullptr; // first: destroying the control sends WM_KILLFOCUS, which must not re-enter
    RemoveWindowSubclass(edit, EditSubclassProc, 1);
    DestroyWindow(edit);
    SetFocus(hwnd_);
    if (commit) Handle(controller_->CommitText(id, Narrow(buf)));
    Invalidate();
}

LRESULT ThemedPanel::HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
    auto xOf = [](LPARAM l) { return static_cast<float>(static_cast<short>(LOWORD(l))); };
    auto yOf = [](LPARAM l) { return static_cast<float>(static_cast<short>(HIWORD(l))); };
    switch (message) {
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hwnd_, &ps);
            Render();
            EndPaint(hwnd_, &ps);
            return 0;
        }
        case WM_SIZE: {
            const UINT w = LOWORD(lParam), h = HIWORD(lParam);
            if (engine_ && engine_->target) engine_->target->Resize(D2D1::SizeU(w, h));
            if (controller_) controller_->SetViewport(static_cast<float>(w), static_cast<float>(h));
            Invalidate();
            return 0;
        }
        case WM_MOUSEMOVE:
            if (!trackingMouse_) {
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hwnd_, 0};
                TrackMouseEvent(&tme);
                trackingMouse_ = true;
            }
            if (controller_) Handle(controller_->MouseMove(xOf(lParam), yOf(lParam)));
            return 0;
        case WM_MOUSELEAVE:
            trackingMouse_ = false;
            if (controller_) Handle(controller_->MouseLeave());
            return 0;
        case WM_LBUTTONDOWN:
            if (edit_) EndEdit(true);
            SetFocus(hwnd_);
            SetCapture(hwnd_);
            if (controller_) Handle(controller_->MouseDown(xOf(lParam), yOf(lParam)));
            return 0;
        case WM_LBUTTONUP:
            if (GetCapture() == hwnd_) ReleaseCapture();
            if (controller_) {
                Handle(controller_->MouseUp(xOf(lParam), yOf(lParam)));
                FlushPendingSets(); // the final value of a drag goes out right away
            }
            return 0;
        case WM_MOUSEWHEEL: {
            POINT p{static_cast<short>(LOWORD(lParam)), static_cast<short>(HIWORD(lParam))};
            ScreenToClient(hwnd_, &p);
            if (controller_) Handle(controller_->MouseWheel(static_cast<float>(p.x), static_cast<float>(p.y), GET_WHEEL_DELTA_WPARAM(wParam) / static_cast<float>(WHEEL_DELTA)));
            return 0;
        }
        case WM_GETDLGCODE:
            return DLGC_WANTALLKEYS;
        case WM_KEYDOWN: {
            if (!controller_) return 0;
            PanelKey key;
            switch (wParam) {
                case VK_TAB: key = (GetKeyState(VK_SHIFT) & 0x8000) ? PanelKey::ShiftTab : PanelKey::Tab; break;
                case VK_SPACE: key = PanelKey::Space; break;
                case VK_RETURN: key = PanelKey::Enter; break;
                case VK_ESCAPE: key = PanelKey::Escape; break;
                case VK_LEFT: key = PanelKey::Left; break;
                case VK_RIGHT: key = PanelKey::Right; break;
                case VK_UP: key = PanelKey::Up; break;
                case VK_DOWN: key = PanelKey::Down; break;
                case VK_HOME: key = PanelKey::Home; break;
                case VK_END: key = PanelKey::End; break;
                default: return 0;
            }
            Handle(controller_->KeyDown(key));
            return 0;
        }
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            Invalidate();
            return 0;
        case WM_TIMER:
            if (wParam == kThrottleTimerId) {
                FlushPendingSets();
                return 0;
            }
            break;
        case kMsgEndEdit:
            EndEdit(wParam != 0);
            return 0;
        default: break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

} // namespace viewer
