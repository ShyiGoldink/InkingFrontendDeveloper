#pragma once

#include <cstdint>

namespace ink {

class InkingScene;

/// 指针按键。
enum class PointerButton : std::uint8_t {
    Left = 0,
    Middle = 1,
    Right = 2,
    Unknown = 3
};

/// 指针动作。Enter / Leave 也算动作，因为按钮的悬停外观全靠它们。
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
 * 命中三态（docs/InputDesign.md §6）。
 *
 * Block       事件到此为止（半透明遮罩也属于这一类）
 * PassThrough 有东西但让过
 * Miss        这里没东西
 */
enum class HitState : std::uint8_t {
    Miss = 0,
    PassThrough = 1,
    Block = 2
};

/// 命中策略：静态表只需要知道「占这块地方的东西挡不挡」。
enum class HitPolicy : std::uint8_t {
    Block = 0,
    PassThrough = 1
};

/**
 * 一次 query 的答案：三态 + 目标（docs/InputDesign.md §6「投递还需要目标」）。
 *
 * 没有拆成两个返回值，也没有硬塞进一个 enum——三态在 state 里，目标在 target 里，
 * z 留着给「静态层与动态层谁在上面」的比较用。
 */
struct HitTarget {
    HitState state = HitState::Miss;
    InkingScene* target = nullptr;
    int z = 0;

    constexpr bool Blocked() const noexcept { return state == HitState::Block; }
    constexpr bool Found() const noexcept { return state != HitState::Miss; }
    explicit constexpr operator bool() const noexcept { return Blocked(); }
};

/// 枚举 → 中文短名，给日志用。
inline const char* toString(HitState state) noexcept {
    switch (state) {
        case HitState::Block:       return "Block";
        case HitState::PassThrough: return "PassThrough";
        case HitState::Miss:        return "Miss";
    }
    return "?";
}

inline const char* toString(PointerAction action) noexcept {
    switch (action) {
        case PointerAction::Down:   return "按下";
        case PointerAction::Up:     return "抬起";
        case PointerAction::Enter:  return "进入";
        case PointerAction::Leave:  return "离开";
        case PointerAction::Cancel: return "取消";
    }
    return "?";
}

}  // namespace ink
