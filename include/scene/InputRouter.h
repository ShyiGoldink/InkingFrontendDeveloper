#pragma once

#include <cstdint>

#include <scene/InkEvent.h>

namespace ink {

class InkingScene;

/**
 * 输入：isDirty + 三态 query（docs/InputDesign.md §5 / §6）。
 *
 * 每帧只判断一次：
 *
 *     鼠标移动 / 几何 / 层级 / 可见性变化  → makeDirty()
 *     每帧：if (isDirty) { query(鼠标位置, hover); 帧计数++ }
 *           连续 N 帧没再标脏 → 清脏，之后完全不再 query（静止时每帧 0 次）
 *     点击：不经过脏标记，单独 query 一次
 *
 * 命中本身不在这层：静态层查表、动态层自判都发生在 InkingScene::Query 里。
 * 这里只管「什么时候问、问出来的 Enter/Leave 投给谁、点击怎么冒泡」。
 *
 * 标脏的第二个来源是场景树自己：几何、层级、可见性一变，根场景上的
 * IsTreeChanged() 就为真——那是「命中结果可能过期」，不是「要重烘」。
 */
class InputRouter {
public:
    /// 连续多少帧没人再标脏就停下。取 2~3 即可（§5）。
    static constexpr int kTailFrames = 3;

    /// 指定输入面，通常是根场景。
    void Attach(InkingScene& rootScene);
    void Detach();

    /// 鼠标动了：命中结果可能变，标脏。
    /// 窗口层每帧调一次就够——一帧里多少个移动事件都只算一次。
    void MarkMouseMoved() noexcept;

    /// 现在脏不脏：鼠标动过，或者场景树里有几何 / 层级 / 可见性变化。
    bool Dirty() const noexcept;

    /// 每帧开头调一次：脏就 query 一次（hover），再在帧计数尾巴上多算几帧。
    /// 返回这帧到底有没有 query。
    bool BeginFrame(float mouseX, float mouseY);

    /// 点击：不经过脏标记，单独查一次。
    bool PointerDown(float x, float y, PointerButton button = PointerButton::Left);
    bool PointerUp(float x, float y, PointerButton button = PointerButton::Left);

    InkingScene* GetHovered() const noexcept;
    InkingScene* GetPressed() const noexcept;

    /// 自检与日志用。
    std::uint64_t HoverQueries() const noexcept;
    std::uint64_t ClickQueries() const noexcept;
    std::uint64_t IdleFrames() const noexcept;

private:
    using Handler = bool (InkingScene::*)(PointerEvent&);

    bool Deliver(InkingScene* target, PointerEvent& event, Handler handler);
    void SwitchHover(InkingScene* target, float x, float y);

    InkingScene* _root = nullptr;
    InkingScene* _hovered = nullptr;
    InkingScene* _pressed = nullptr;

    bool _mouseDirty = false;   ///< 鼠标动过：命中的另一个脏源
    int _tailFrames = 0;        ///< 帧计数尾巴
    PointerButton _button = PointerButton::Left;

    std::uint64_t _hoverQueries = 0;
    std::uint64_t _clickQueries = 0;
    std::uint64_t _idleFrames = 0;
};

}  // namespace ink
