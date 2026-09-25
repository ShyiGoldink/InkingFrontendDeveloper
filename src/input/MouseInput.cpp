#include <input/MouseInput.h>

#include <SDL3/SDL.h>

namespace ink {

const MouseState& MouseInput::GetState() const noexcept {
    return state_;
}

void MouseInput::SetDesignPosition(float x, float y) noexcept {
    state_.design.x = x;
    state_.design.y = y;
}

bool MouseInput::IsLeftDown() const noexcept {
    return state_.leftButtonDown;
}

bool MouseInput::IsRightDown() const noexcept {
    return state_.rightButtonDown;
}

bool MouseInput::IsMiddleDown() const noexcept {
    return state_.middleButtonDown;
}

bool MouseInput::IsMoved() const noexcept {
    return state_.moved;
}

void MouseInput::ResetFrameFlags() noexcept {
    state_.moved = false;
}

void MouseInput::UpdateFromSDL(const union SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION: {
        // 运动事件中，坐标直接更新为最新位置。
        state_.position.x = static_cast<int>(event.motion.x);
        state_.position.y = static_cast<int>(event.motion.y);
        state_.moved = true;
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_DOWN: {
        switch (event.button.button) {
        case SDL_BUTTON_LEFT:
            state_.leftButtonDown = true;
            break;
        case SDL_BUTTON_RIGHT:
            state_.rightButtonDown = true;
            break;
        case SDL_BUTTON_MIDDLE:
            state_.middleButtonDown = true;
            break;
        default:
            break;
        }
        break;
    }

    case SDL_EVENT_MOUSE_BUTTON_UP: {
        switch (event.button.button) {
        case SDL_BUTTON_LEFT:
            state_.leftButtonDown = false;
            break;
        case SDL_BUTTON_RIGHT:
            state_.rightButtonDown = false;
            break;
        case SDL_BUTTON_MIDDLE:
            state_.middleButtonDown = false;
            break;
        default:
            break;
        }
        break;
    }

    case SDL_EVENT_MOUSE_WHEEL: {
        // 当前轮子事件不改变按键状态，但保留扩展点，后续可以给出滚轮增量。
        // 例如：如果要支持滚动条与缩放，这里可以继续扩展。
        break;
    }

    default:
        break;
    }
}

}  // namespace ink
