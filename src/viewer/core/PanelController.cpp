#include "PanelController.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace viewer {

using scrapi::ControlNode;
using scrapi::ControlType;
using scrapi::JsonValue;

namespace {

constexpr float kPad = 12.0f;
constexpr float kCardGap = 8.0f;
constexpr float kHeaderH = 40.0f;
constexpr float kRowH = 32.0f;
constexpr float kRowGap = 6.0f;
constexpr float kLabelW = 100.0f;
constexpr float kColGap = 8.0f;
constexpr float kSubHeaderH = 28.0f;
constexpr float kKnobSize = 64.0f;
constexpr float kChipH = 26.0f;
constexpr float kChipGap = 6.0f;
constexpr float kChipPadX = 10.0f;
constexpr float kChipCheckW = 16.0f;
constexpr float kToggleW = 40.0f;
constexpr float kToggleH = 20.0f;
constexpr float kValueW = 56.0f;
constexpr float kBrowseW = 80.0f;
constexpr float kPopupItemH = 28.0f;
constexpr int kPopupMaxItems = 8;
constexpr float kScrollW = 6.0f;
constexpr float kSwatch = 24.0f;
constexpr float kWheelStep = 48.0f;
constexpr float kPi = 3.14159265358979f;

Point Polar(float cx, float cy, float r, float deg) {
    const float a = (deg - 90.0f) * kPi / 180.0f;
    return {cx + r * std::cos(a), cy + r * std::sin(a)};
}

bool ParseHex(const std::string& text, Color& out) {
    unsigned r = 0, g = 0, b = 0, a = 255;
    if (text.size() == 7 && text[0] == '#' && std::sscanf(text.c_str() + 1, "%2x%2x%2x", &r, &g, &b) == 3) {
        out = Color{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), 255};
        return true;
    }
    if (text.size() == 9 && text[0] == '#' && std::sscanf(text.c_str() + 1, "%2x%2x%2x%2x", &r, &g, &b, &a) == 4) {
        out = Color{static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b), static_cast<uint8_t>(a)};
        return true;
    }
    return false;
}

double MinOf(const ControlNode& n) { return n.hasMin ? n.min : 0.0; }
double MaxOf(const ControlNode& n) {
    const double mn = MinOf(n);
    const double mx = n.hasMax ? n.max : mn + 100.0;
    return mx <= mn ? mn + 1.0 : mx;
}

// Clamp to the control's range, then snap: ints round, floats follow `step` when given.
double Snap(const ControlNode& n, double v) {
    const double mn = MinOf(n), mx = MaxOf(n);
    if (n.type == ControlType::Int) v = std::round(v);
    else if (n.hasStep && n.step > 0.0) v = mn + std::round((v - mn) / n.step) * n.step;
    return std::clamp(v, mn, mx);
}

int OptionIndex(const ControlNode& n, const std::string& value) {
    for (size_t i = 0; i < n.options.size(); ++i) {
        if (n.options[i].value == value) return static_cast<int>(i);
    }
    return -1;
}

} // namespace

double SliderPositionToValue(const ControlNode& node, double t) {
    t = std::clamp(t, 0.0, 1.0);
    const double mn = MinOf(node), mx = MaxOf(node);
    double v;
    if (node.scale == "log" && mn > 0.0) v = mn * std::pow(mx / mn, t);
    else v = mn + (mx - mn) * t;
    return Snap(node, v);
}

double ValueToSliderPosition(const ControlNode& node, double value) {
    const double mn = MinOf(node), mx = MaxOf(node);
    value = std::clamp(value, mn, mx);
    if (node.scale == "log" && mn > 0.0) return std::log(value / mn) / std::log(mx / mn);
    return (value - mn) / (mx - mn);
}

PanelController::PanelController(ITextMeasurer& measurer, const Theme& theme) : measurer_(measurer), theme_(theme) {}

// ---------------------------------------------------------------------------
// Model helpers

const JsonValue* PanelController::ValueOf(const ControlNode& node) const { return model_ ? model_->Get(node.id) : nullptr; }

std::string PanelController::DisplayLabel(const ControlNode& node) const { return node.label.empty() ? node.id : node.label; }

std::string PanelController::FormatValue(const ControlNode& node, const JsonValue& v) const {
    if (v.IsString()) return v.AsString();
    if (v.IsBool()) return v.AsBool() ? "true" : "false";
    if (v.IsNull()) return "-";
    if (v.IsNumber()) {
        char buf[64];
        if (node.type == ControlType::Int) std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(std::llround(v.AsDouble())));
        else std::snprintf(buf, sizeof(buf), "%.4g", v.AsDouble());
        std::string s = buf;
        if (!node.unit.empty()) s += " " + node.unit;
        return s;
    }
    return scrapi::SerializeJson(v);
}

bool PanelController::UsesSegments(const ControlNode& n) {
    return n.type == ControlType::Enum && n.presentation == "radio" && n.options.size() >= 2 && n.options.size() <= 4;
}

bool PanelController::IsKnob(const ControlNode& n) {
    return (n.type == ControlType::Int || n.type == ControlType::Float) && n.presentation == "knob";
}

double PanelController::NumericValue(const Item& item) const {
    if (drag_.kind != Drag::Kind::None && drag_.item >= 0 && &items_[static_cast<size_t>(drag_.item)] == &item) return dragValue_;
    const JsonValue* v = item.node ? ValueOf(*item.node) : nullptr;
    return v ? v->AsDouble(MinOf(*item.node)) : (item.node ? MinOf(*item.node) : 0.0);
}

bool PanelController::IsCollapsed(const std::string& id) const {
    auto it = collapsed_.find(id);
    return it != collapsed_.end() && it->second;
}

// ---------------------------------------------------------------------------
// Lifecycle

void PanelController::SetModel(const scrapi::ControlModel* model) {
    model_ = model;
    popup_ = Popup();
    drag_ = Drag();
    hover_ = Hit();
    focusItem_ = -1;
    focusPart_ = -1;
    Layout();
}

void PanelController::SetViewport(float width, float height) {
    viewW_ = width;
    viewH_ = height;
    Layout();
}

void PanelController::ValuesChanged() { Layout(); }

void PanelController::ClampScroll() {
    scrollY_ = std::clamp(scrollY_, 0.0f, std::max(0.0f, contentHeight_ - viewH_));
}

// ---------------------------------------------------------------------------
// Layout

void PanelController::Layout() {
    // Keyboard focus and drags refer to items by index; remember which control they were on
    // and find it again after the rebuild (values changing must not drop the focus).
    std::string focusedId;
    if (focusItem_ >= 0 && focusItem_ < static_cast<int>(items_.size())) focusedId = items_[static_cast<size_t>(focusItem_)].node->id;
    items_.clear();
    cards_.clear();
    contentHeight_ = 0.0f;
    if (!model_) {
        focusItem_ = -1;
        ClampScroll();
        return;
    }
    const float cardX = kPad;
    const float cardW = std::max(160.0f, viewW_ - 2 * kPad);
    float y = kPad;
    for (const ControlNode& node : model_->manifest().controls) {
        if (!model_->IsVisible(node.id)) continue;
        const float top = y;
        if (node.type == ControlType::Group) {
            Item h;
            h.kind = Item::Kind::CardHeader;
            h.node = &node;
            h.rect = {cardX, y, cardW, kHeaderH};
            h.control = h.rect;
            h.focusable = true;
            items_.push_back(h);
            y += kHeaderH;
            if (!IsCollapsed(node.id)) {
                y = LayoutBody(node.children, y, cardX + kPad, cardX + cardW - kPad);
                y += kPad - kRowGap;
            }
        } else {
            y += kPad - kRowGap;
            y = LayoutControl(node, y, cardX + kPad, cardX + cardW - kPad);
            y += kPad - kRowGap;
        }
        cards_.push_back({{cardX, top, cardW, y - top}});
        y += kCardGap;
    }
    contentHeight_ = cards_.empty() ? 0.0f : y - kCardGap + kPad;
    ClampScroll();

    focusItem_ = -1;
    if (!focusedId.empty()) {
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].node->id == focusedId && items_[i].focusable) focusItem_ = static_cast<int>(i);
        }
    }
    if (focusItem_ < 0) focusPart_ = -1;
    if (drag_.item >= static_cast<int>(items_.size())) drag_ = Drag();

    if (popup_.open) { // keep an open dropdown attached to its control across a relayout
        int found = -1;
        for (size_t i = 0; i < items_.size(); ++i) {
            if (items_[i].kind == Item::Kind::Dropdown && items_[i].node->id == popup_.id) found = static_cast<int>(i);
        }
        if (found < 0) popup_ = Popup();
        else {
            popup_.item = found;
            popup_.rect = PopupRectFor(items_[static_cast<size_t>(found)], static_cast<int>(items_[static_cast<size_t>(found)].node->options.size()));
        }
    }
}

float PanelController::LayoutBody(const std::vector<ControlNode>& nodes, float y, float left, float right) {
    for (const ControlNode& node : nodes) {
        if (!model_->IsVisible(node.id)) continue;
        if (node.type == ControlType::Group) {
            Item h;
            h.kind = Item::Kind::SubHeader;
            h.node = &node;
            h.rect = {left, y, right - left, kSubHeaderH};
            h.label = h.rect;
            items_.push_back(h);
            y += kSubHeaderH;
            y = LayoutBody(node.children, y, left, right);
        } else {
            y = LayoutControl(node, y, left, right);
        }
    }
    return y;
}

float PanelController::LayoutControl(const ControlNode& node, float y, float left, float right) {
    Item it;
    it.node = &node;
    it.enabled = model_->IsEnabled(node.id) && !(node.readOnly && node.type != ControlType::Readout);
    const float controlX = left + kLabelW + kColGap;
    const float controlW = std::max(40.0f, right - controlX);
    float height = kRowH;
    it.rect = {left, y, right - left, kRowH};
    it.label = {left, y, kLabelW, kRowH};
    it.control = {controlX, y, controlW, kRowH};

    switch (node.type) {
        case ControlType::Enum:
            if (UsesSegments(node)) {
                it.kind = Item::Kind::Segmented;
                const Rect inner = it.control.Inset(2.0f);
                const float n = static_cast<float>(node.options.size());
                for (size_t i = 0; i < node.options.size(); ++i) {
                    it.parts.push_back({inner.x + inner.w * static_cast<float>(i) / n, inner.y, inner.w / n, inner.h});
                }
            } else {
                it.kind = Item::Kind::Dropdown;
            }
            it.focusable = true;
            break;

        case ControlType::Flags: {
            it.kind = Item::Kind::Flags;
            const float noneW = measurer_.Width("None", theme_.smallFontSize, false) + 16.0f;
            const float allW = measurer_.Width("All", theme_.smallFontSize, false) + 16.0f;
            it.aux2 = {right - noneW, y + 2.0f, noneW, 24.0f};
            it.aux = {it.aux2.x - allW, y + 2.0f, allW, 24.0f};
            it.label = {left, y, right - left - noneW - allW, kRowH - 4.0f};
            float x = left;
            float cy = y + kRowH - 2.0f;
            for (const auto& opt : node.options) {
                const float w = measurer_.Width(opt.label, theme_.smallFontSize, false) + 2 * kChipPadX + kChipCheckW;
                if (x + w > right && x > left) {
                    x = left;
                    cy += kChipH + kChipGap;
                }
                it.parts.push_back({x, cy, w, kChipH});
                x += w + kChipGap;
            }
            height = std::max(kRowH, cy + kChipH - y);
            it.control = {left, y + kRowH - 2.0f, right - left, height - (kRowH - 2.0f)};
            it.focusable = true;
            break;
        }

        case ControlType::Bool:
            it.kind = Item::Kind::Toggle;
            it.control = {controlX, y + (kRowH - kToggleH) / 2, kToggleW, kToggleH};
            it.focusable = true;
            break;

        case ControlType::Int:
        case ControlType::Float:
            if (IsKnob(node)) {
                it.kind = Item::Kind::Knob;
                it.control = {controlX, y, kKnobSize, kKnobSize};
                it.aux = {controlX + kKnobSize + 12.0f, y + kKnobSize / 2 - 12.0f, std::max(40.0f, right - controlX - kKnobSize - 12.0f), 24.0f};
                it.label = {left, y + (kKnobSize - kRowH) / 2, kLabelW, kRowH};
                height = kKnobSize;
            } else {
                it.kind = Item::Kind::Slider;
                it.control = {controlX, y, std::max(60.0f, controlW - kValueW - kColGap), kRowH};
                it.aux = {right - kValueW, y, kValueW, kRowH};
            }
            it.focusable = true;
            break;

        case ControlType::String:
            it.kind = Item::Kind::TextField;
            it.focusable = true;
            if (node.multiline) {
                height = 64.0f;
                it.control.h = height;
            }
            break;

        case ControlType::Path:
            it.kind = Item::Kind::PathField;
            it.control = {controlX, y, std::max(40.0f, controlW - kBrowseW - kColGap), kRowH};
            it.aux = {right - kBrowseW, y, kBrowseW, kRowH};
            it.focusable = true;
            break;

        case ControlType::Color: {
            it.kind = Item::Kind::ColorField;
            it.control = {controlX, y + 4.0f, kSwatch, kSwatch};
            it.aux = {controlX + kSwatch + 8.0f, y, std::min(120.0f, std::max(40.0f, controlW - kSwatch - 8.0f)), kRowH};
            float x = controlX;
            const float py = y + kRowH + 4.0f;
            for (size_t i = 0; i < node.palette.size(); ++i) {
                it.parts.push_back({x, py, kSwatch, kSwatch});
                x += kSwatch + 8.0f;
            }
            if (!node.palette.empty()) height = kRowH + 4.0f + kSwatch;
            it.focusable = true;
            break;
        }

        case ControlType::Button: {
            it.kind = Item::Kind::Button;
            const float w = std::min(right - left, std::max(96.0f, measurer_.Width(DisplayLabel(node), theme_.fontSize, false) + 32.0f));
            it.control = {left, y, w, kRowH};
            it.label = Rect();
            it.focusable = true;
            break;
        }

        case ControlType::Group: // handled by the callers
        case ControlType::Readout:
        case ControlType::Unknown:
        default:
            it.kind = Item::Kind::Readout;
            if (node.multiline) {
                height = 64.0f;
                it.control.h = height;
            }
            break;
    }
    if (!it.enabled) it.focusable = false;
    it.rect.h = height;
    items_.push_back(it);
    return y + height + kRowGap;
}

// ---------------------------------------------------------------------------
// Hit testing and geometry

Rect PanelController::SliderTrack(const Item& item) const {
    return {item.control.x + 9.0f, item.control.y + kRowH / 2 - 2.0f, std::max(1.0f, item.control.w - 18.0f), 4.0f};
}

PanelController::Hit PanelController::HitTest(float x, float y) const {
    const float cy = y + scrollY_;
    Hit best;
    for (size_t i = 0; i < items_.size(); ++i) {
        const Item& it = items_[i];
        if (!it.rect.Contains(x, cy)) continue;
        best.item = static_cast<int>(i);
        const bool live = it.enabled || it.kind == Item::Kind::CardHeader || it.kind == Item::Kind::SubHeader;
        if (!live) return best;
        switch (it.kind) {
            case Item::Kind::CardHeader:
                best.part = kMain;
                break;
            case Item::Kind::Segmented:
                for (size_t p = 0; p < it.parts.size(); ++p) {
                    if (it.parts[p].Contains(x, cy)) best.part = static_cast<int>(p);
                }
                break;
            case Item::Kind::Flags:
                for (size_t p = 0; p < it.parts.size(); ++p) {
                    if (it.parts[p].Contains(x, cy)) best.part = static_cast<int>(p);
                }
                if (it.aux.Contains(x, cy)) best.part = kAux;
                else if (it.aux2.Contains(x, cy)) best.part = kAux2;
                break;
            case Item::Kind::Toggle:
                if (Rect{it.label.x, it.rect.y, it.control.Right() - it.label.x, it.rect.h}.Contains(x, cy)) best.part = kMain;
                break;
            case Item::Kind::Slider:
                if (Rect{it.control.x, it.rect.y, it.control.w, it.rect.h}.Contains(x, cy)) best.part = kMain;
                break;
            case Item::Kind::PathField:
                if (it.control.Contains(x, cy)) best.part = kMain;
                else if (it.aux.Contains(x, cy)) best.part = kAux;
                break;
            case Item::Kind::ColorField:
                for (size_t p = 0; p < it.parts.size(); ++p) {
                    if (it.parts[p].Contains(x, cy)) best.part = static_cast<int>(p);
                }
                if (it.control.Contains(x, cy)) best.part = kMain;
                else if (it.aux.Contains(x, cy)) best.part = kAux;
                break;
            case Item::Kind::Dropdown:
            case Item::Kind::Knob:
            case Item::Kind::TextField:
            case Item::Kind::Button:
                if (it.control.Contains(x, cy)) best.part = kMain;
                break;
            case Item::Kind::SubHeader:
            case Item::Kind::Readout:
                break;
        }
        return best;
    }
    return best;
}

Rect PanelController::PopupRectFor(const Item& item, int optionCount) const {
    const Rect r = ToViewport(item.control);
    const int visible = std::min(optionCount, kPopupMaxItems);
    const float h = visible * kPopupItemH + 8.0f;
    const float w = std::max(r.w, 160.0f);
    float y = r.Bottom() + 2.0f;
    if (y + h > viewH_ && r.y - h - 2.0f >= 0.0f) y = r.y - h - 2.0f;
    return {r.x, y, w, h};
}

void PanelController::OpenPopup(int itemIndex) {
    const Item& it = items_[static_cast<size_t>(itemIndex)];
    const int count = static_cast<int>(it.node->options.size());
    popup_ = Popup();
    popup_.open = true;
    popup_.item = itemIndex;
    popup_.id = it.node->id;
    const JsonValue* v = ValueOf(*it.node);
    const int current = v ? OptionIndex(*it.node, v->AsString()) : -1;
    popup_.hover = current;
    const int visible = std::min(count, kPopupMaxItems);
    popup_.scroll = std::clamp(current - visible / 2, 0, std::max(0, count - visible));
    popup_.rect = PopupRectFor(it, count);
}

Rect PanelController::ScrollbarThumb() const {
    if (contentHeight_ <= viewH_ || viewH_ <= 0.0f) return Rect();
    const float thumbH = std::max(24.0f, viewH_ * viewH_ / contentHeight_);
    const float range = contentHeight_ - viewH_;
    const float thumbY = range > 0.0f ? scrollY_ / range * (viewH_ - thumbH) : 0.0f;
    return {viewW_ - kScrollW - 2.0f, thumbY, kScrollW, thumbH};
}

bool PanelController::FieldRect(const std::string& id, Rect& out) const {
    for (const Item& it : items_) {
        if (it.node && it.node->id == id && (it.kind == Item::Kind::TextField || it.kind == Item::Kind::PathField || it.kind == Item::Kind::ColorField)) {
            out = ToViewport(it.kind == Item::Kind::ColorField ? it.aux : it.control);
            return true;
        }
    }
    return false;
}

bool PanelController::ControlRect(const std::string& id, Rect& out) const {
    for (const Item& it : items_) {
        if (it.node && it.node->id == id && it.kind != Item::Kind::CardHeader && it.kind != Item::Kind::SubHeader) {
            out = ToViewport(it.control);
            return true;
        }
    }
    return false;
}

bool PanelController::PartRect(const std::string& id, int part, Rect& out) const {
    for (const Item& it : items_) {
        if (it.node && it.node->id == id && part >= 0 && part < static_cast<int>(it.parts.size())) {
            out = ToViewport(it.parts[static_cast<size_t>(part)]);
            return true;
        }
    }
    return false;
}

bool PanelController::FlagsLinkRect(const std::string& id, bool all, Rect& out) const {
    for (const Item& it : items_) {
        if (it.node && it.node->id == id && it.kind == Item::Kind::Flags) {
            out = ToViewport(all ? it.aux : it.aux2);
            return true;
        }
    }
    return false;
}

bool PanelController::HeaderRect(const std::string& id, Rect& out) const {
    for (const Item& it : items_) {
        if (it.node && it.node->id == id && it.kind == Item::Kind::CardHeader) {
            out = ToViewport(it.rect);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Input

PanelAction PanelController::MakeSet(const std::string& id, JsonValue v, bool continuous) const {
    PanelAction a;
    a.kind = PanelAction::Kind::SetValue;
    a.id = id;
    a.value = std::move(v);
    a.continuous = continuous;
    return a;
}

InputResult PanelController::SetNumber(const Item& item, double value, bool continuous) {
    InputResult r;
    const double v = Snap(*item.node, value);
    dragValue_ = v;
    r.actions.push_back(MakeSet(item.node->id,
                                item.node->type == ControlType::Int ? JsonValue::Int(static_cast<int64_t>(std::llround(v))) : JsonValue::Double(v),
                                continuous));
    r.redraw = true;
    return r;
}

InputResult PanelController::MouseMove(float x, float y) {
    InputResult r;
    if (drag_.kind == Drag::Kind::Scrollbar) {
        const float thumbH = ScrollbarThumb().h;
        const float travel = std::max(1.0f, viewH_ - thumbH);
        scrollY_ = drag_.startScroll + (y - drag_.startY) * (contentHeight_ - viewH_) / travel;
        ClampScroll();
        r.redraw = true;
        return r;
    }
    if (drag_.kind == Drag::Kind::Slider && drag_.item >= 0 && drag_.item < static_cast<int>(items_.size())) {
        const Item& it = items_[static_cast<size_t>(drag_.item)];
        const Rect track = SliderTrack(it);
        const double v = SliderPositionToValue(*it.node, (x - track.x) / track.w);
        if (v != dragValue_) return SetNumber(it, v, true);
        return r;
    }
    if (drag_.kind == Drag::Kind::Knob && drag_.item >= 0 && drag_.item < static_cast<int>(items_.size())) {
        const Item& it = items_[static_cast<size_t>(drag_.item)];
        const double range = MaxOf(*it.node) - MinOf(*it.node);
        const double v = Snap(*it.node, drag_.startValue + (drag_.startY - y) / 200.0 * range);
        if (v != dragValue_) return SetNumber(it, v, true);
        return r;
    }
    if (popup_.open) {
        int hover = -1;
        if (popup_.rect.Contains(x, y)) {
            const int row = static_cast<int>((y - popup_.rect.y - 4.0f) / kPopupItemH);
            const int index = popup_.scroll + row;
            if (row >= 0 && index < static_cast<int>(items_[static_cast<size_t>(popup_.item)].node->options.size())) hover = index;
        }
        if (hover != popup_.hover) {
            popup_.hover = hover;
            r.redraw = true;
        }
        return r;
    }
    const Hit h = HitTest(x, y);
    if (h.item != hover_.item || h.part != hover_.part) {
        hover_ = h;
        r.redraw = true;
    }
    return r;
}

InputResult PanelController::MouseLeave() {
    InputResult r;
    if (hover_.item != -1) {
        hover_ = Hit();
        r.redraw = true;
    }
    return r;
}

InputResult PanelController::MouseDown(float x, float y) {
    InputResult r;
    if (popup_.open) {
        const Item& it = items_[static_cast<size_t>(popup_.item)];
        if (popup_.rect.Contains(x, y)) {
            const int row = static_cast<int>((y - popup_.rect.y - 4.0f) / kPopupItemH);
            const int index = popup_.scroll + row;
            if (row >= 0 && index < static_cast<int>(it.node->options.size())) {
                r.actions.push_back(MakeSet(it.node->id, JsonValue::String(it.node->options[static_cast<size_t>(index)].value)));
                popup_ = Popup();
            }
        } else {
            popup_ = Popup(); // a click elsewhere just dismisses the list
        }
        r.redraw = true;
        return r;
    }
    if (contentHeight_ > viewH_) {
        const Rect thumb = ScrollbarThumb();
        if (x >= thumb.x - 6.0f && y >= thumb.y && y <= thumb.Bottom()) {
            drag_ = Drag();
            drag_.kind = Drag::Kind::Scrollbar;
            drag_.startY = y;
            drag_.startScroll = scrollY_;
            return r;
        }
    }
    const Hit h = HitTest(x, y);
    if (h.item < 0) return r;
    const Item& it = items_[static_cast<size_t>(h.item)];
    if (it.focusable) {
        focusItem_ = h.item;
        focusPart_ = (h.part >= 0 && h.part < kMain) ? h.part : -1;
        r.redraw = true;
    }
    if (h.part == kNoPart) return r;
    InputResult a = Activate(h.item, h.part, x, y);
    a.redraw = true;
    return a;
}

InputResult PanelController::Activate(int itemIndex, int part, float x, float y) {
    InputResult r;
    const Item& it = items_[static_cast<size_t>(itemIndex)];
    const ControlNode& n = *it.node;
    const JsonValue* v = ValueOf(n);

    switch (it.kind) {
        case Item::Kind::CardHeader:
            collapsed_[n.id] = !IsCollapsed(n.id);
            Layout();
            break;

        case Item::Kind::Segmented:
            if (part >= 0 && part < static_cast<int>(n.options.size())) {
                r.actions.push_back(MakeSet(n.id, JsonValue::String(n.options[static_cast<size_t>(part)].value)));
            }
            break;

        case Item::Kind::Dropdown:
            if (popup_.open && popup_.item == itemIndex) popup_ = Popup();
            else OpenPopup(itemIndex);
            break;

        case Item::Kind::Flags: {
            std::vector<bool> on(n.options.size(), false);
            if (v) {
                for (const JsonValue& item : v->items()) {
                    const int i = OptionIndex(n, item.AsString());
                    if (i >= 0) on[static_cast<size_t>(i)] = true;
                }
            }
            if (part == kAux) std::fill(on.begin(), on.end(), true);
            else if (part == kAux2) std::fill(on.begin(), on.end(), false);
            else if (part >= 0 && part < static_cast<int>(on.size())) on[static_cast<size_t>(part)] = !on[static_cast<size_t>(part)];
            JsonValue arr = JsonValue::Array();
            for (size_t i = 0; i < on.size(); ++i) {
                if (on[i]) arr.Push(JsonValue::String(n.options[i].value));
            }
            r.actions.push_back(MakeSet(n.id, std::move(arr)));
            break;
        }

        case Item::Kind::Toggle:
            r.actions.push_back(MakeSet(n.id, JsonValue::Bool(!(v && v->AsBool()))));
            break;

        case Item::Kind::Slider: {
            const Rect track = SliderTrack(it);
            drag_ = Drag();
            drag_.kind = Drag::Kind::Slider;
            drag_.item = itemIndex;
            const double value = SliderPositionToValue(n, (x - track.x) / track.w);
            return SetNumber(it, value, true);
        }

        case Item::Kind::Knob: {
            const double base = NumericValue(it); // read before the drag state starts overriding it
            drag_ = Drag();
            drag_.kind = Drag::Kind::Knob;
            drag_.item = itemIndex;
            drag_.startY = y;
            drag_.startValue = base;
            dragValue_ = base;
            break;
        }

        case Item::Kind::TextField:
        case Item::Kind::PathField:
            if (part == kAux && it.kind == Item::Kind::PathField) {
                PanelAction a;
                a.kind = PanelAction::Kind::BrowsePath;
                a.id = n.id;
                a.filters = n.filters;
                r.actions.push_back(std::move(a));
            } else {
                PanelAction a;
                a.kind = PanelAction::Kind::EditText;
                a.id = n.id;
                a.rect = ToViewport(it.control);
                a.text = v ? v->AsString() : std::string();
                r.actions.push_back(std::move(a));
            }
            break;

        case Item::Kind::ColorField:
            if (part >= 0 && part < static_cast<int>(n.palette.size())) {
                r.actions.push_back(MakeSet(n.id, JsonValue::String(n.palette[static_cast<size_t>(part)])));
            } else {
                PanelAction a;
                a.kind = PanelAction::Kind::PickColor;
                a.id = n.id;
                a.text = v ? v->AsString() : std::string();
                r.actions.push_back(std::move(a));
            }
            break;

        case Item::Kind::Button: {
            PanelAction a;
            a.id = n.id;
            if (!n.confirm.empty()) {
                a.kind = PanelAction::Kind::Confirm;
                a.text = n.confirm;
                pendingConfirmId_ = n.id;
            } else {
                a.kind = PanelAction::Kind::Invoke;
            }
            r.actions.push_back(std::move(a));
            break;
        }

        case Item::Kind::SubHeader:
        case Item::Kind::Readout:
            break;
    }
    return r;
}

InputResult PanelController::MouseUp(float /*x*/, float /*y*/) {
    InputResult r;
    if ((drag_.kind == Drag::Kind::Slider || drag_.kind == Drag::Kind::Knob) && drag_.item >= 0 && drag_.item < static_cast<int>(items_.size())) {
        const Item& it = items_[static_cast<size_t>(drag_.item)];
        r = SetNumber(it, dragValue_, false); // final, non-continuous value
    }
    const bool wasDragging = drag_.kind != Drag::Kind::None;
    drag_ = Drag();
    r.redraw = r.redraw || wasDragging;
    return r;
}

InputResult PanelController::MouseWheel(float x, float y, float delta) {
    InputResult r;
    if (popup_.open && popup_.rect.Contains(x, y)) {
        const int count = static_cast<int>(items_[static_cast<size_t>(popup_.item)].node->options.size());
        const int maxScroll = std::max(0, count - kPopupMaxItems);
        popup_.scroll = std::clamp(popup_.scroll - static_cast<int>(std::round(delta)), 0, maxScroll);
        r.redraw = true;
        return r;
    }
    popup_ = Popup();
    const float before = scrollY_;
    scrollY_ -= delta * kWheelStep;
    ClampScroll();
    r.redraw = scrollY_ != before || popup_.open;
    return r;
}

std::vector<int> PanelController::FocusOrder() const {
    std::vector<int> order;
    for (size_t i = 0; i < items_.size(); ++i) {
        if (items_[i].focusable) order.push_back(static_cast<int>(i));
    }
    return order;
}

void PanelController::ScrollIntoView(int itemIndex) {
    const Item& it = items_[static_cast<size_t>(itemIndex)];
    if (it.rect.y < scrollY_) scrollY_ = it.rect.y - kPad;
    else if (it.rect.Bottom() > scrollY_ + viewH_) scrollY_ = it.rect.Bottom() - viewH_ + kPad;
    ClampScroll();
}

InputResult PanelController::AdjustFocused(int dir) {
    InputResult r;
    if (focusItem_ < 0 || focusItem_ >= static_cast<int>(items_.size())) return r;
    const Item& it = items_[static_cast<size_t>(focusItem_)];
    const ControlNode& n = *it.node;
    if (it.kind == Item::Kind::Slider || it.kind == Item::Kind::Knob) {
        const double step = n.type == ControlType::Int ? 1.0 : (n.hasStep && n.step > 0.0 ? n.step : (MaxOf(n) - MinOf(n)) / 100.0);
        return SetNumber(it, NumericValue(it) + dir * step, false);
    }
    if (it.kind == Item::Kind::Segmented) {
        const JsonValue* v = ValueOf(n);
        const int cur = v ? OptionIndex(n, v->AsString()) : 0;
        const int next = std::clamp(cur + dir, 0, static_cast<int>(n.options.size()) - 1);
        if (next != cur) r.actions.push_back(MakeSet(n.id, JsonValue::String(n.options[static_cast<size_t>(next)].value)));
        focusPart_ = next;
        r.redraw = true;
    } else if (it.kind == Item::Kind::Flags) {
        focusPart_ = std::clamp(focusPart_ + dir, 0, static_cast<int>(n.options.size()) - 1);
        r.redraw = true;
    }
    return r;
}

InputResult PanelController::KeyDown(PanelKey key) {
    InputResult r;
    if (popup_.open) {
        const int count = static_cast<int>(items_[static_cast<size_t>(popup_.item)].node->options.size());
        switch (key) {
            case PanelKey::Escape: popup_ = Popup(); break;
            case PanelKey::Down: popup_.hover = std::min(count - 1, popup_.hover + 1); break;
            case PanelKey::Up: popup_.hover = std::max(0, popup_.hover - 1); break;
            case PanelKey::Enter:
            case PanelKey::Space:
                if (popup_.hover >= 0) {
                    const Item& it = items_[static_cast<size_t>(popup_.item)];
                    r.actions.push_back(MakeSet(it.node->id, JsonValue::String(it.node->options[static_cast<size_t>(popup_.hover)].value)));
                }
                popup_ = Popup();
                break;
            default: break;
        }
        if (popup_.open) { // keep the highlighted option in view
            const int visible = std::min(count, kPopupMaxItems);
            if (popup_.hover < popup_.scroll) popup_.scroll = popup_.hover;
            if (popup_.hover >= popup_.scroll + visible) popup_.scroll = popup_.hover - visible + 1;
        }
        r.redraw = true;
        return r;
    }

    const std::vector<int> order = FocusOrder();
    switch (key) {
        case PanelKey::Tab:
        case PanelKey::ShiftTab: {
            if (order.empty()) return r;
            auto pos = std::find(order.begin(), order.end(), focusItem_);
            size_t idx = 0;
            if (pos == order.end()) idx = key == PanelKey::Tab ? 0 : order.size() - 1;
            else if (key == PanelKey::Tab) idx = (static_cast<size_t>(pos - order.begin()) + 1) % order.size();
            else idx = (static_cast<size_t>(pos - order.begin()) + order.size() - 1) % order.size();
            focusItem_ = order[idx];
            focusPart_ = -1;
            ScrollIntoView(focusItem_);
            r.redraw = true;
            return r;
        }
        case PanelKey::Left:
        case PanelKey::Down:
            return AdjustFocused(-1);
        case PanelKey::Right:
        case PanelKey::Up: {
            if (key == PanelKey::Up && focusItem_ >= 0 && items_[static_cast<size_t>(focusItem_)].kind == Item::Kind::Dropdown) {
                r = Activate(focusItem_, kMain, 0, 0);
                r.redraw = true;
                return r;
            }
            return AdjustFocused(+1);
        }
        case PanelKey::Home:
        case PanelKey::End:
            if (focusItem_ >= 0) {
                const Item& it = items_[static_cast<size_t>(focusItem_)];
                if (it.kind == Item::Kind::Slider || it.kind == Item::Kind::Knob) {
                    return SetNumber(it, key == PanelKey::Home ? MinOf(*it.node) : MaxOf(*it.node), false);
                }
            }
            return r;
        case PanelKey::Enter:
        case PanelKey::Space:
            if (focusItem_ >= 0 && focusItem_ < static_cast<int>(items_.size())) {
                const Item& it = items_[static_cast<size_t>(focusItem_)];
                int part = kMain;
                if ((it.kind == Item::Kind::Flags || it.kind == Item::Kind::Segmented) && focusPart_ >= 0) part = focusPart_;
                else if (it.kind == Item::Kind::Flags || it.kind == Item::Kind::Segmented) return r;
                r = Activate(focusItem_, part, it.control.x, it.control.y);
                r.redraw = true;
            }
            return r;
        case PanelKey::Escape:
            return r;
    }
    return r;
}

InputResult PanelController::CommitText(const std::string& id, const std::string& text) {
    InputResult r;
    r.actions.push_back(MakeSet(id, JsonValue::String(text)));
    r.redraw = true;
    return r;
}

InputResult PanelController::ProvideValue(const std::string& id, const JsonValue& value) {
    InputResult r;
    r.actions.push_back(MakeSet(id, value));
    r.redraw = true;
    return r;
}

InputResult PanelController::ConfirmInvoke(const std::string& id) {
    InputResult r;
    if (id != pendingConfirmId_) return r;
    pendingConfirmId_.clear();
    PanelAction a;
    a.kind = PanelAction::Kind::Invoke;
    a.id = id;
    r.actions.push_back(std::move(a));
    return r;
}

// ---------------------------------------------------------------------------
// Painting

void PanelController::Paint(DrawList& dl) const {
    const Rect all{0.0f, 0.0f, viewW_, viewH_};
    dl.FillRoundRect(all, 0.0f, theme_.panel);
    dl.PushClip(all);

    if (!model_ || items_.empty()) {
        dl.Text({kPad, 24.0f, std::max(0.0f, viewW_ - 2 * kPad), 48.0f}, model_ ? "This screensaver declares no controls." : "Open a screensaver to see its controls.",
                theme_.fontSize, false, theme_.text2, TextAlign::Center);
        dl.PopClip();
        return;
    }
    for (const Card& c : cards_) {
        const Rect r = ToViewport(c.rect);
        if (r.Bottom() < 0.0f || r.y > viewH_) continue;
        dl.FillRoundRect(r, theme_.cardRadius, theme_.card);
        dl.StrokeRoundRect(r, theme_.cardRadius, theme_.cardBorder);
    }
    for (size_t i = 0; i < items_.size(); ++i) {
        const Rect r = ToViewport(items_[i].rect);
        if (r.Bottom() < 0.0f || r.y > viewH_) continue;
        PaintItem(dl, i);
    }
    PaintPopup(dl);
    PaintScrollbar(dl);
    dl.PopClip();
}

void PanelController::PaintItem(DrawList& dl, size_t index) const {
    const Item& it = items_[index];
    const ControlNode& n = *it.node;
    const float fade = it.enabled ? 1.0f : theme_.disabledAlpha;
    auto C = [&](Color c) { return c.Faded(fade); };
    const bool hovered = hover_.item == static_cast<int>(index);
    const JsonValue* v = ValueOf(n);
    const float r = theme_.radius;
    const Rect label = ToViewport(it.label);
    const Rect control = ToViewport(it.control);
    const Rect aux = ToViewport(it.aux);

    auto fieldBox = [&](const Rect& box, bool hot) {
        dl.FillRoundRect(box, r, C(hot ? theme_.hover : theme_.ctl));
        dl.StrokeRoundRect(box, r, C(theme_.ctlBorder));
        dl.Line({box.x + r, box.Bottom() - 0.5f}, {box.Right() - r, box.Bottom() - 0.5f}, C(theme_.ctlBorderBottom));
    };
    auto drawLabel = [&]() {
        if (!it.label.w) return;
        dl.Text(label, DisplayLabel(n), theme_.fontSize, false, C(theme_.text2));
    };

    switch (it.kind) {
        case Item::Kind::CardHeader: {
            const Rect h = ToViewport(it.rect);
            if (hovered) dl.FillRoundRect(h, theme_.cardRadius, theme_.hover);
            dl.Icon(IsCollapsed(n.id) ? IconKind::ChevronRight : IconKind::ChevronDown, {h.x + 12.0f, h.y + 12.0f, 16.0f, 16.0f}, theme_.text);
            dl.Text({h.x + 36.0f, h.y, h.w - 48.0f, h.h}, DisplayLabel(n), theme_.fontSize, true, theme_.text);
            break;
        }
        case Item::Kind::SubHeader:
            dl.Text(ToViewport(it.rect), DisplayLabel(n), theme_.fontSize, true, theme_.text);
            break;

        case Item::Kind::Segmented: {
            drawLabel();
            dl.FillRoundRect(control, r, C(theme_.segTrack));
            const int sel = v ? OptionIndex(n, v->AsString()) : -1;
            for (size_t p = 0; p < it.parts.size(); ++p) {
                const Rect pr = ToViewport(it.parts[p]);
                const bool on = static_cast<int>(p) == sel;
                if (on) dl.FillRoundRect(pr, r - 1.0f, C(theme_.segSel));
                dl.Text(pr, n.options[p].label, theme_.fontSize, on, C(on ? theme_.segSelText : theme_.text2), TextAlign::Center);
            }
            break;
        }
        case Item::Kind::Dropdown: {
            drawLabel();
            fieldBox(control, hovered || (popup_.open && popup_.item == static_cast<int>(index)));
            const int sel = v ? OptionIndex(n, v->AsString()) : -1;
            dl.Text({control.x + 10.0f, control.y, control.w - 40.0f, control.h}, sel >= 0 ? n.options[static_cast<size_t>(sel)].label : "", theme_.fontSize, false, C(theme_.text));
            dl.Icon(IconKind::ChevronDown, {control.Right() - 26.0f, control.y + 8.0f, 16.0f, 16.0f}, C(theme_.text2));
            break;
        }
        case Item::Kind::Flags: {
            drawLabel();
            const Rect a = ToViewport(it.aux), b = ToViewport(it.aux2);
            const bool hotAll = hovered && hover_.part == kAux, hotNone = hovered && hover_.part == kAux2;
            if (hotAll) dl.FillRoundRect(a, r, C(theme_.hover));
            if (hotNone) dl.FillRoundRect(b, r, C(theme_.hover));
            dl.Text(a, "All", theme_.smallFontSize, false, C(theme_.accentText), TextAlign::Center);
            dl.Text(b, "None", theme_.smallFontSize, false, C(theme_.accentText), TextAlign::Center);
            std::vector<bool> on(n.options.size(), false);
            if (v) {
                for (const JsonValue& item : v->items()) {
                    const int i = OptionIndex(n, item.AsString());
                    if (i >= 0) on[static_cast<size_t>(i)] = true;
                }
            }
            for (size_t p = 0; p < it.parts.size(); ++p) {
                const Rect pr = ToViewport(it.parts[p]);
                if (on[p]) {
                    dl.FillRoundRect(pr, pr.h / 2, C(theme_.accentSoft));
                    dl.Icon(IconKind::Check, {pr.x + kChipPadX, pr.y + 7.0f, 12.0f, 12.0f}, C(theme_.accentText), 2.0f);
                    dl.Text({pr.x + kChipPadX + kChipCheckW, pr.y, pr.w - kChipPadX - kChipCheckW, pr.h}, n.options[p].label, theme_.smallFontSize, true, C(theme_.accentText));
                } else {
                    if (hovered && hover_.part == static_cast<int>(p)) dl.FillRoundRect(pr, pr.h / 2, C(theme_.hover));
                    dl.StrokeRoundRect(pr, pr.h / 2, C(theme_.ctlBorder));
                    dl.Text(pr, n.options[p].label, theme_.smallFontSize, false, C(theme_.text2), TextAlign::Center);
                }
                if (focusItem_ == static_cast<int>(index) && focusPart_ == static_cast<int>(p)) dl.StrokeRoundRect(pr.Inset(-2.0f), pr.h / 2 + 2.0f, theme_.accent, 2.0f);
            }
            break;
        }
        case Item::Kind::Toggle: {
            drawLabel();
            const bool on = v && v->AsBool();
            dl.FillRoundRect(control, control.h / 2, C(on ? theme_.accent : theme_.track));
            const float d = control.h - 6.0f;
            dl.FillEllipse({control.x + (on ? control.w - d - 3.0f : 3.0f), control.y + 3.0f, d, d}, C(on ? theme_.onAccent : theme_.card));
            break;
        }
        case Item::Kind::Slider: {
            drawLabel();
            const Rect track = ToViewport(SliderTrack(it));
            const double value = NumericValue(it);
            const float t = static_cast<float>(ValueToSliderPosition(n, value));
            dl.FillRoundRect(track, 2.0f, C(theme_.track));
            dl.FillRoundRect({track.x, track.y, track.w * t, track.h}, 2.0f, C(theme_.accent));
            const float cx = track.x + track.w * t, cy = track.y + track.h / 2;
            dl.FillEllipse({cx - 9.0f, cy - 9.0f, 18.0f, 18.0f}, C(theme_.card));
            dl.StrokeEllipse({cx - 9.0f, cy - 9.0f, 18.0f, 18.0f}, C(theme_.track));
            dl.FillEllipse({cx - 6.0f, cy - 6.0f, 12.0f, 12.0f}, C(theme_.accent));
            JsonValue shown = v ? *v : JsonValue::Null();
            if (drag_.kind == Drag::Kind::Slider && drag_.item == static_cast<int>(index)) shown = JsonValue::Double(dragValue_);
            dl.Text(aux, FormatValue(n, shown), theme_.fontSize, false, C(theme_.text), TextAlign::Right);
            break;
        }
        case Item::Kind::Knob: {
            drawLabel();
            const double value = NumericValue(it);
            const float t = static_cast<float>(ValueToSliderPosition(n, value));
            const float cx = control.x + control.w / 2, cy = control.y + control.h / 2;
            const float a = -135.0f + 270.0f * t;
            dl.Arc(cx, cy, 25.0f, -135.0f, 135.0f, C(theme_.track), 5.0f);
            if (t > 0.0f) dl.Arc(cx, cy, 25.0f, -135.0f, a, C(theme_.accent), 5.0f);
            dl.FillEllipse({cx - 17.0f, cy - 17.0f, 34.0f, 34.0f}, C(theme_.ctl));
            dl.StrokeEllipse({cx - 17.0f, cy - 17.0f, 34.0f, 34.0f}, C(theme_.ctlBorder));
            dl.Line(Polar(cx, cy, 7.0f, a), Polar(cx, cy, 14.0f, a), C(theme_.accent), 2.5f);
            JsonValue shown = v ? *v : JsonValue::Null();
            if (drag_.kind == Drag::Kind::Knob && drag_.item == static_cast<int>(index)) shown = JsonValue::Double(dragValue_);
            dl.Text(aux, FormatValue(n, shown), theme_.fontSize, true, C(theme_.text));
            break;
        }
        case Item::Kind::TextField:
        case Item::Kind::PathField: {
            drawLabel();
            fieldBox(control, hovered && hover_.part == kMain);
            dl.Text({control.x + 10.0f, control.y, control.w - 20.0f, control.h}, v ? v->AsString() : "", theme_.fontSize, false, C(theme_.text));
            if (it.kind == Item::Kind::PathField) {
                fieldBox(aux, hovered && hover_.part == kAux);
                dl.Text(aux, "Browse…", theme_.fontSize, false, C(theme_.text), TextAlign::Center);
            }
            break;
        }
        case Item::Kind::ColorField: {
            drawLabel();
            Color current = theme_.track;
            const std::string text = v ? v->AsString() : "";
            ParseHex(text, current);
            dl.FillRoundRect(control, r, C(current));
            dl.StrokeRoundRect(control, r, C(theme_.ctlBorder));
            fieldBox(aux, hovered && hover_.part == kAux);
            dl.Text({aux.x + 10.0f, aux.y, aux.w - 20.0f, aux.h}, text, theme_.fontSize, false, C(theme_.text));
            for (size_t p = 0; p < it.parts.size(); ++p) {
                Color sw;
                if (!ParseHex(n.palette[p], sw)) continue;
                const Rect pr = ToViewport(it.parts[p]);
                dl.FillEllipse(pr, sw);
                dl.StrokeEllipse(pr, C(theme_.ctlBorder));
                std::string up = n.palette[p], cur = text;
                for (auto& ch : up) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                for (auto& ch : cur) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
                if (up == cur) dl.StrokeEllipse(pr.Inset(-3.0f), theme_.accent, 2.0f);
            }
            break;
        }
        case Item::Kind::Button: {
            const bool hot = hovered && hover_.part == kMain;
            fieldBox(control, hot);
            dl.Text(control, DisplayLabel(n), theme_.fontSize, false, C(theme_.text), TextAlign::Center);
            break;
        }
        case Item::Kind::Readout: {
            drawLabel();
            const bool bar = (n.format == "bar" || n.format == "gauge") && v && v->IsNumber();
            if (bar) {
                const double mn = MinOf(n), mx = MaxOf(n);
                const float t = static_cast<float>(std::clamp((v->AsDouble() - mn) / (mx - mn), 0.0, 1.0));
                const Rect track{control.x, control.y + control.h / 2 - 3.0f, std::max(20.0f, control.w - 60.0f), 6.0f};
                dl.FillRoundRect(track, 3.0f, theme_.track);
                dl.FillRoundRect({track.x, track.y, track.w * t, track.h}, 3.0f, theme_.accent);
                dl.Text({track.Right() + 6.0f, control.y, 54.0f, control.h}, FormatValue(n, *v), theme_.smallFontSize, false, theme_.text2, TextAlign::Right);
            } else {
                dl.Text(control, v ? FormatValue(n, *v) : "-", theme_.fontSize, true, C(theme_.text));
            }
            break;
        }
    }

    if (focusItem_ == static_cast<int>(index) && it.enabled && focusPart_ < 0) {
        const Rect ring = it.kind == Item::Kind::CardHeader ? ToViewport(it.rect) : ToViewport(it.control);
        dl.StrokeRoundRect(ring.Inset(-2.0f), (it.kind == Item::Kind::CardHeader ? theme_.cardRadius : r) + 2.0f, theme_.accent, 2.0f);
    }
}

void PanelController::PaintPopup(DrawList& dl) const {
    if (!popup_.open) return;
    const Item& it = items_[static_cast<size_t>(popup_.item)];
    const ControlNode& n = *it.node;
    const Rect box = popup_.rect;
    dl.FillRoundRect(box.Offset(0.0f, 2.0f), theme_.cardRadius, Color{0, 0, 0, 40});
    dl.FillRoundRect(box, theme_.cardRadius, theme_.card);
    dl.StrokeRoundRect(box, theme_.cardRadius, theme_.cardBorder);
    const JsonValue* v = ValueOf(n);
    const int current = v ? OptionIndex(n, v->AsString()) : -1;
    const int count = static_cast<int>(n.options.size());
    const int visible = std::min(count, kPopupMaxItems);
    for (int row = 0; row < visible; ++row) {
        const int index = popup_.scroll + row;
        if (index >= count) break;
        const Rect rr{box.x + 4.0f, box.y + 4.0f + row * kPopupItemH, box.w - 8.0f, kPopupItemH};
        const bool sel = index == current;
        if (sel) dl.FillRoundRect(rr, theme_.radius, theme_.accentSoft);
        else if (index == popup_.hover) dl.FillRoundRect(rr, theme_.radius, theme_.hover);
        if (sel) dl.Icon(IconKind::Check, {rr.x + 8.0f, rr.y + 8.0f, 12.0f, 12.0f}, theme_.accentText, 2.0f);
        dl.Text({rr.x + 28.0f, rr.y, rr.w - 32.0f, rr.h}, n.options[static_cast<size_t>(index)].label, theme_.fontSize, sel, sel ? theme_.accentText : theme_.text);
    }
}

void PanelController::PaintScrollbar(DrawList& dl) const {
    const Rect thumb = ScrollbarThumb();
    if (thumb.h <= 0.0f) return;
    Color c = theme_.text2;
    c.a = drag_.kind == Drag::Kind::Scrollbar ? 160 : 100;
    dl.FillRoundRect(thumb, kScrollW / 2, c);
}

} // namespace viewer
