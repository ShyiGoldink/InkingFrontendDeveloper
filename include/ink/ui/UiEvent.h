#pragma once

#include <cstdint>

namespace ink {

/// 指针按键。
enum class PointerButton : std::uint8_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    Unknown = 3
};

/**
 * 指针动作。
 *
 * 把 Enter/Leave 也算动作，是因为按钮的四态外观全靠它们：
 * 悬停、按下都只改颜色，不动几何。
 */
enum class PointerAction : std::uint8_t {
    Down = 0,
    Up = 1,
    Enter = 2,
    Leave = 3,
    Cancel = 4
};

/// 设计坐标系下的指针事件（窗口坐标 → 设计坐标的换算在窗口层做完）。
struct PointerEvent {
    float x = 0.0f;
    float y = 0.0f;
    PointerAction action = PointerAction::Down;
    PointerButton button = PointerButton::Left;
    bool handled = false;  ///< 被谁处理了就置 true，冒泡到此为止
};

/**
 * 场景想对窗口做的事。
 *
 * 场景层不认识 SDL 窗口，也拿不到窗口句柄，所以只能"提要求"：
 * 命令冒泡到根场景排队，由窗口层在下一帧开头统一执行。
 * 头部菜单的最小化/最大化/关闭就是这么落地的。
 */
enum class WindowCommand : std::uint8_t {
    None = 0,
    Minimize,
    Maximize,
    ToggleMaximize,
    Restore,
    BeginDrag,
    Close
};

/// 枚举 → 中文短名，给日志用。
const char* toString(WindowCommand command) noexcept;
const char* toString(PointerAction action) noexcept;

}  // namespace ink
