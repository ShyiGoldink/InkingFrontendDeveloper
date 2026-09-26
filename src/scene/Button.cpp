#include <scene/Button.h>

#include <ink/basic/InkLog.h>

namespace ink {

namespace {

constexpr const char* kModuleName = "Button";

// 默认配色：深色背景上用的一档中低对比度灰蓝。
constexpr Color kFill{44, 47, 60, 255};
constexpr Color kHoverFill{72, 78, 100, 255};
constexpr Color kPressFill{30, 32, 42, 255};
constexpr Color kBorder{80, 86, 108, 255};
constexpr Color kLabel{226, 230, 240, 255};

}  // namespace

Button::Button(const std::string& name, const std::string& label)
    : InkingScene(name),
      _label(label),
      _fill(kFill),
      _hoverFill(kHoverFill),
      _pressFill(kPressFill),
      _border(kBorder),
      _labelColor(kLabel) {}

// ---------------------------------------------------------------------------
// 属性：只影响重绘的写入口，明确调 MakeDirty()
// ---------------------------------------------------------------------------

const std::string& Button::GetLabel() const noexcept {
    return _label;
}

void Button::SetLabel(const std::string& label) {
    if (_label == label) {
        return;
    }
    _label = label;
    MakeDirty("文字变化");  // 文字不参与命中，只要求重绘
}

Color Button::GetFill() const noexcept {
    return _fill;
}

void Button::SetFill(const Color& color) {
    _fill = color;
    MakeDirty("常态底色变化");
}

Color Button::GetHoverFill() const noexcept {
    return _hoverFill;
}

void Button::SetHoverFill(const Color& color) {
    _hoverFill = color;
    MakeDirty("悬停底色变化");
}

Color Button::GetPressFill() const noexcept {
    return _pressFill;
}

void Button::SetPressFill(const Color& color) {
    _pressFill = color;
    MakeDirty("按下底色变化");
}

Color Button::GetBorder() const noexcept {
    return _border;
}

void Button::SetBorder(const Color& color) {
    _border = color;
    MakeDirty("描边变化");
}

Color Button::GetLabelColor() const noexcept {
    return _labelColor;
}

void Button::SetLabelColor(const Color& color) {
    _labelColor = color;
    MakeDirty("文字颜色变化");
}

bool Button::IsHovered() const noexcept {
    return _hovered;
}

bool Button::IsPressed() const noexcept {
    return _pressed;
}

std::uint64_t Button::AppearanceChanges() const noexcept {
    return _appearanceChanges;
}

bool Button::SetHovered(bool hovered) {
    if (_hovered == hovered) {
        return false;
    }
    _hovered = hovered;
    ++_appearanceChanges;
    MakeDirty("悬停状态变化");  // 三档外观只改颜色，几何一点不动
    return true;
}

bool Button::SetPressed(bool pressed) {
    if (_pressed == pressed) {
        return false;
    }
    _pressed = pressed;
    ++_appearanceChanges;
    MakeDirty("按下状态变化");
    return true;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

void Button::onDraw(Canvas& canvas) const {
    const Rect box = GetRect();
    if (!box.valid()) {
        return;
    }

    const Color fill = _pressed ? _pressFill : (_hovered ? _hoverFill : _fill);
    if (fill.a > 0) {
        canvas.fillRect(box, fill);
    }
    if (_border.a > 0) {
        canvas.strokeRect(box, _border, 1.0f);
    }

    if (!_label.empty()) {
        // 字形系统还没落地，drawText 目前是空操作——文本只占位不落笔。
        canvas.drawText(box, _label, _labelColor);
    }
}

// ---------------------------------------------------------------------------
// 输入：三档外观只改颜色，几何一点不动，所以静态表一直有效
// ---------------------------------------------------------------------------

bool Button::onPointerDown(PointerEvent& event) {
    SetPressed(true);
    event.handled = true;
    return true;
}

bool Button::onPointerUp(PointerEvent& event) {
    SetPressed(false);
    event.handled = true;
    return true;
}

void Button::onPointerEnter(PointerEvent&) {
    SetHovered(true);
}

void Button::onPointerLeave(PointerEvent&) {
    SetHovered(false);
    SetPressed(false);
}

void Button::onPointerCancel(PointerEvent&) {
    SetPressed(false);
}

bool Button::onClick(PointerEvent& event) {
    event.handled = true;
    INK_LOG_DEBUG(kModuleName, "点击：" + GetName());
    if (action) {
        action(*this);
    }
    return true;
}

}  // namespace ink
