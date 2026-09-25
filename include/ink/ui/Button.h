#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <ink/ui/InkColor.h>
#include <ink/ui/Scene.h>

namespace ink {

/// 按钮图标。头部菜单要用的那三个，外加最大化之后的"还原"。
enum class Glyph : std::uint8_t {
    None = 0,
    Minimize,
    Maximize,
    Restore,
    Close
};

// 按钮 = 一块矩形场景。
//
// 命中、嵌套、事件冒泡全部沿用场景那一套，按钮只多三件事：
// 常态/悬停/按下三档配色、能拿直线拼出来的图标、点击回调。
// 悬停和按下只改颜色不改几何，所以按钮再怎么变脸都不会弄脏
// 父场景的命中表——静态场景可以一直静态下去。
class Button : public Scene {
public:
    explicit Button(std::string name, std::string label = "",
                    Glyph glyph = Glyph::None);

    // ---- 属性 ----
    const std::string& GetLabel() const noexcept;
    void SetLabel(std::string label);
    Glyph GetGlyph() const noexcept;
    void SetGlyph(Glyph glyph);

    Color GetFill() const noexcept;
    void SetFill(const Color& color);
    Color GetHoverFill() const noexcept;
    void SetHoverFill(const Color& color);
    Color GetPressFill() const noexcept;
    void SetPressFill(const Color& color);
    Color GetBorder() const noexcept;
    void SetBorder(const Color& color);
    Color GetGlyphColor() const noexcept;
    void SetGlyphColor(const Color& color);
    Color GetLabelColor() const noexcept;
    void SetLabelColor(const Color& color);

    /// [√] 点击回调。直接改写它就能做数据劫持，不必派生新类。
    std::function<void(Button&)> action;

    // ---- 状态 ----
    bool IsHovered() const noexcept;
    bool IsPressed() const noexcept;

    // ---- 场景 ----
    void onDraw(Canvas& canvas) const override;
    bool onPointerDown(PointerEvent& event) override;
    bool onPointerUp(PointerEvent& event) override;
    void onPointerEnter(PointerEvent& event) override;
    void onPointerLeave(PointerEvent& event) override;
    void onPointerCancel(PointerEvent& event) override;
    bool onClick(PointerEvent& event) override;

private:
    std::string _label;
    Glyph       _glyph = Glyph::None;
    bool        _hovered = false;
    bool        _pressed = false;

    Color _fill;
    Color _hoverFill;
    Color _pressFill;
    Color _border;
    Color _glyphColor;
    Color _labelColor;
};

}  // namespace ink
