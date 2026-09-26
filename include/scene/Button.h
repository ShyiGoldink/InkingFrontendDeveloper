#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <core/InkColor.h>
#include <scene/InkingScene.h>

namespace ink {

/**
 * 按钮 = 一块矩形场景。
 *
 * 命中、嵌套、冒泡全部沿用场景那一套，按钮只多三件事：
 *   1. 常态 / 悬停 / 按下三档配色；
 *   2. 点击回调；
 *   3. 该标脏的地方明确标脏。
 *
 * 第 3 条是这次原型要验的重点：悬停、按下、换配色**只影响重绘**，不影响命中，
 * 所以它们的写入口自己调 MakeDirty()——几何一动不动，静态表一次都不用重烘。
 */
class Button : public InkingScene {
public:
    explicit Button(const std::string& name, const std::string& label = "");

    // ---- 属性 ----
    const std::string& GetLabel() const noexcept;
    /// 文字：只影响重绘 → 明确 makeDirty()
    void SetLabel(const std::string& label);

    Color GetFill() const noexcept;
    /// 常态底色：只影响重绘 → 明确 makeDirty()
    void SetFill(const Color& color);
    Color GetHoverFill() const noexcept;
    void SetHoverFill(const Color& color);
    Color GetPressFill() const noexcept;
    void SetPressFill(const Color& color);
    Color GetBorder() const noexcept;
    void SetBorder(const Color& color);
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

    /// 自检用：这个按钮因为悬停 / 按下改过几次外观。
    std::uint64_t AppearanceChanges() const noexcept;

private:
    /// 悬停 / 按下切换时统一走这里：改状态 → 标重绘。
    bool SetHovered(bool hovered);
    bool SetPressed(bool pressed);

    std::string _label;
    bool _hovered = false;
    bool _pressed = false;
    std::uint64_t _appearanceChanges = 0;

    Color _fill;
    Color _hoverFill;
    Color _pressFill;
    Color _border;
    Color _labelColor;
};

}  // namespace ink
