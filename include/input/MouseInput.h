#pragma once

// 只做前向声明：这个头文件被 InkingWindow.h 包含，包含它不该被迫拖进
// 整个 SDL。注意 SDL3 的 SDL_Event 是 union，前向声明也必须写 union
// （写成 struct 会和真头文件冲突）。
union SDL_Event;

// MouseInput 负责维护鼠标状态；
// 它不直接负责 hit test，也不负责渲染更新。
// 命中检测和事件投递应由 Scene / InputRouter 在下一帧做，
// 这样才能和设计稿里的 isDirty + query 流程一致。
namespace ink {

struct MousePosition {
    int x = 0;
    int y = 0;
};

/// 设计坐标（letterbox 换算之后）。场景层只认这一套。
struct MousePoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct MouseState {
    /// 窗口像素，SDL 事件给的原样。
    MousePosition position{};
    /// 设计坐标，由窗口层换算后写入（每帧一次）。
    MousePoint design{};
    bool leftButtonDown = false;
    bool rightButtonDown = false;
    bool middleButtonDown = false;
    bool moved = false;
};

class MouseInput {
public:
    MouseInput() = default;

    // 由 SDL3 事件循环直接驱动：移动、按下、抬起都在这里更新状态。
    void UpdateFromSDL(const SDL_Event& event);

    // 窗口坐标 → 设计坐标的换算结果写进来。
    // 换算是渲染器的事（letterbox 有偏移和留边），所以由窗口层算完再写，
    // MouseInput 自己不去碰渲染器。
    void SetDesignPosition(float x, float y) noexcept;

    // 每一帧结束时清理瞬时状态，避免 moved 持续影响后续逻辑。
    void ResetFrameFlags() noexcept;

    const MouseState& GetState() const noexcept;
    bool IsLeftDown() const noexcept;
    bool IsRightDown() const noexcept;
    bool IsMiddleDown() const noexcept;
    bool IsMoved() const noexcept;

private:
    MouseState state_{};
};

}  // namespace ink
