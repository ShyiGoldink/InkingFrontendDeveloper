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
 * 命中、嵌套、冒泡全部沿用场景那一套，按钮只多四件事：
 *   1. 常态 / 悬停 / 按下三档配色（颜色与透明度都是直通的 RGBA）；
 *   2. 点击回调；
 *   3. 该标脏的地方明确标脏；
 *   4. 悬停时可选按倍率放大——走**尺寸写入口**，所以会连带把上层命中表标脏。
 *
 * 第 3、4 条正好是两条不同的路：
 *
 *   换配色 / 换文字：只影响重绘 → 写入口自己调 MakeDirty()，几何一动不动，
 *                    上层命中表一次都不用重烘；
 *   悬停放大：      真的改了尺寸 → 走 Resize() 这个写入口 → 写入口内部标脏
 *                    → 上层命中表重烘一次（docs/InputDesign.md §3 / §4）。
 *
 * 放大时锚点建议用「自身 Center 对上父级 Center」：那样它从中心长大，
 * 中心点不动，四周对称外扩。
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

    /// 悬停时按倍率放大（1.0 = 不放大，默认）。传小于 1 的值按 1 处理。
    ///
    /// 这条路故意走**尺寸写入口**：几何一变就标脏，上层的命中表跟着重烘——
    /// 和「只改颜色」那一路形成对照，也是这次要验的「变大会导致重新烘焙」。
    /// （docs/InputDesign.md §11 打算用变换通道免掉这次重烘，那是后面的事；
    ///   本原型就是要先把重烘这条走通。）
    ///
    /// 倍率只在悬停状态真的切换时生效，所以运行期改它不会立刻改变尺寸。
    bool SetHoverGrowth(float factor);
    float GetHoverGrowth() const noexcept;
    /// 现在是不是处于「悬停放大」后的尺寸。
    bool IsGrown() const noexcept;

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
    /// 悬停放大的落点：走 Resize()，所以几何标脏由写入口内部完成。
    void ApplyHoverGrowth(bool hovered);

    std::string _label;
    bool _hovered = false;
    bool _pressed = false;
    std::uint64_t _appearanceChanges = 0;

    float _hoverGrowth = 1.0f;
    bool _grown = false;
    int _baseWidth = 0;   ///< 放大前的尺寸，离开悬停时按这个还原
    int _baseHeight = 0;

    Color _fill;
    Color _hoverFill;
    Color _pressFill;
    Color _border;
    Color _labelColor;
};

}  // namespace ink
