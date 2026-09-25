#include <ink/ui/InputRouter.h>

#include <ink/basic/InkLog.h>

namespace ink {

namespace {

constexpr const char* kModuleName = "InputRouter";

}  // namespace

void InputRouter::attach(Scene& rootScene) {
    _root = &rootScene;
    reset();
    INK_LOG_DEBUG(kModuleName, "接管输入：" + rootScene.GetName());
}

void InputRouter::detach() {
    _root = nullptr;
    reset();
}

void InputRouter::reset() {
    _hovered = nullptr;
    _pressed = nullptr;
}

Scene* InputRouter::hovered() const noexcept {
    return _hovered;
}

Scene* InputRouter::pressed() const noexcept {
    return _pressed;
}

Scene* InputRouter::targetAt(float x, float y) {
    if (_root == nullptr) {
        return nullptr;
    }
    return _root->hitTest(x, y).target;
}

bool InputRouter::deliver(Scene* target, PointerEvent& event, Handler handler) {
    // 目标先来，不接就往父场景送，一直送到输入面为止。
    for (Scene* node = target; node != nullptr; node = node->GetParent()) {
        if ((node->*handler)(event)) {
            event.handled = true;
            return true;
        }
        if (node == _root) {
            break;
        }
    }
    return false;
}

bool InputRouter::switchHover(Scene* target, float x, float y) {
    if (target == _hovered) {
        return target != nullptr;
    }

    PointerEvent event;
    event.x = x;
    event.y = y;
    event.button = _button;

    if (_hovered != nullptr) {
        event.action = PointerAction::Leave;
        _hovered->onPointerLeave(event);
    }
    if (target != nullptr) {
        event.action = PointerAction::Enter;
        event.handled = false;
        target->onPointerEnter(event);
    }

    _hovered = target;
    return target != nullptr;
}

bool InputRouter::pointerMove(float x, float y) {
    if (_root == nullptr) {
        return false;
    }
    return switchHover(targetAt(x, y), x, y);
}

bool InputRouter::pointerDown(float x, float y, PointerButton button) {
    if (_root == nullptr) {
        return false;
    }

    _button = button;
    Scene* target = targetAt(x, y);
    _pressed = target;
    switchHover(target, x, y);

    if (target == nullptr) {
        return false;
    }

    PointerEvent event;
    event.x = x;
    event.y = y;
    event.action = PointerAction::Down;
    event.button = button;
    return deliver(target, event, &Scene::onPointerDown);
}

bool InputRouter::pointerUp(float x, float y, PointerButton button) {
    if (_root == nullptr) {
        return false;
    }

    _button = button;
    Scene* target = targetAt(x, y);
    switchHover(target, x, y);

    Scene* pressed = _pressed;
    _pressed = nullptr;
    if (pressed == nullptr) {
        return false;
    }

    PointerEvent event;
    event.x = x;
    event.y = y;
    event.button = button;

    if (target != pressed) {
        // 在别处松的手：这是一次取消，不是点击。
        event.action = PointerAction::Cancel;
        pressed->onPointerCancel(event);  // 取消只通知按下者本人，不冒泡
        return false;
    }

    event.action = PointerAction::Up;
    deliver(pressed, event, &Scene::onPointerUp);

    event.action = PointerAction::Up;
    event.handled = false;
    return deliver(pressed, event, &Scene::onClick);
}

}  // namespace ink
