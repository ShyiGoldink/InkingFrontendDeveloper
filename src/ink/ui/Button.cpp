#include <ink/ui/Button.h>

#include <utility>

namespace ink {

namespace {

// 默认配色：深色头部菜单上用的一档中低对比度灰蓝。
constexpr Color kFill{44, 47, 60, 255};
constexpr Color kHoverFill{72, 78, 100, 255};
constexpr Color kPressFill{30, 32, 42, 255};
constexpr Color kBorder{80, 86, 108, 255};
constexpr Color kGlyph{226, 230, 240, 255};
constexpr Color kLabel{226, 230, 240, 255};

}  // namespace

Button::Button(std::string name, std::string label, Glyph glyph)
    : Scene(std::move(name)),
      _label(std::move(label)),
      _glyph(glyph),
      _fill(kFill),
      _hoverFill(kHoverFill),
      _pressFill(kPressFill),
      _border(kBorder),
      _glyphColor(kGlyph),
      _labelColor(kLabel) {}

// ---------------------------------------------------------------------------
// 属性
// ---------------------------------------------------------------------------

const std::string& Button::GetLabel() const noexcept {
    return _label;
}

void Button::SetLabel(std::string label) {
    _label = std::move(label);
}

Glyph Button::GetGlyph() const noexcept {
    return _glyph;
}

void Button::SetGlyph(Glyph glyph) {
    _glyph = glyph;
}

Color Button::GetFill() const noexcept {
    return _fill;
}

void Button::SetFill(const Color& color) {
    _fill = color;
}

Color Button::GetHoverFill() const noexcept {
    return _hoverFill;
}

void Button::SetHoverFill(const Color& color) {
    _hoverFill = color;
}

Color Button::GetPressFill() const noexcept {
    return _pressFill;
}

void Button::SetPressFill(const Color& color) {
    _pressFill = color;
}

Color Button::GetBorder() const noexcept {
    return _border;
}

void Button::SetBorder(const Color& color) {
    _border = color;
}

Color Button::GetGlyphColor() const noexcept {
    return _glyphColor;
}

void Button::SetGlyphColor(const Color& color) {
    _glyphColor = color;
}

Color Button::GetLabelColor() const noexcept {
    return _labelColor;
}

void Button::SetLabelColor(const Color& color) {
    _labelColor = color;
}

bool Button::IsHovered() const noexcept {
    return _hovered;
}

bool Button::IsPressed() const noexcept {
    return _pressed;
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
    canvas.fillRect(box, fill);
    if (_border.a > 0) {
        canvas.strokeRect(box, _border, 1.0f);
    }

    // 图标：全部用直线/矩形拼，不依赖字形系统。
    switch (_glyph) {
        case Glyph::Minimize: {
            const Rect area = box.inset(box.w * 0.30f, box.h * 0.30f);
            const float y = area.bottom();
            canvas.drawLine(area.left(), y, area.right(), y, _glyphColor);
            break;
        }
        case Glyph::Maximize: {
            canvas.strokeRect(box.inset(box.w * 0.30f, box.h * 0.30f), _glyphColor, 1.0f);
            break;
        }
        case Glyph::Restore: {
            // 两个错开的方框：后面的先画，前面那个用底色盖掉一角。
            const Rect area = box.inset(box.w * 0.28f, box.h * 0.28f);
            const Rect back{area.x + area.w * 0.24f, area.y, area.w * 0.76f, area.h * 0.76f};
            const Rect front{area.x, area.y + area.h * 0.24f, area.w * 0.76f, area.h * 0.76f};
            canvas.strokeRect(back, _glyphColor, 1.0f);
            canvas.fillRect(Rect{front.x, front.y, front.w, front.h + 1.0f}, fill);
            canvas.strokeRect(front, _glyphColor, 1.0f);
            break;
        }
        case Glyph::Close: {
            const Rect area = box.inset(box.w * 0.32f, box.h * 0.32f);
            canvas.drawLine(area.left(), area.top(), area.right(), area.bottom(), _glyphColor);
            canvas.drawLine(area.right(), area.top(), area.left(), area.bottom(), _glyphColor);
            break;
        }
        case Glyph::None:
            break;
    }

    if (!_label.empty()) {
        // 字形系统还没落地，drawText 目前是空操作——文本只占位。
        canvas.drawText(box, _label, _labelColor);
    }
}

// ---------------------------------------------------------------------------
// 输入：三档外观只改颜色，几何一点不动，所以静态命中表一直有效
// ---------------------------------------------------------------------------

bool Button::onPointerDown(PointerEvent& event) {
    _pressed = true;
    event.handled = true;
    return true;
}

bool Button::onPointerUp(PointerEvent& event) {
    _pressed = false;
    event.handled = true;
    return true;
}

void Button::onPointerEnter(PointerEvent&) {
    _hovered = true;
}

void Button::onPointerLeave(PointerEvent&) {
    _hovered = false;
    _pressed = false;
}

void Button::onPointerCancel(PointerEvent&) {
    _pressed = false;
}

bool Button::onClick(PointerEvent& event) {
    event.handled = true;
    if (action) {
        action(*this);
    }
    return true;
}

}  // namespace ink
