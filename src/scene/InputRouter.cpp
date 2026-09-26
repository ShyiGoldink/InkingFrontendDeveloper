#include <scene/InputRouter.h>

#include <ink/basic/InkLog.h>
#include <scene/InkingScene.h>

namespace ink {

namespace {

constexpr const char* kModuleName = "InputRouter";

}  // namespace

void InputRouter::Attach(InkingScene& rootScene) {
    Detach();
    _root = &rootScene;
    INK_LOG_DEBUG(kModuleName, "接管输入：" + rootScene.GetName());
}

void InputRouter::Detach() {
    _root = nullptr;
    _hovered = nullptr;
    _pressed = nullptr;
    _mouseDirty = false;
    _tailFrames = 0;
}

void InputRouter::MarkMouseMoved() noexcept {
    _mouseDirty = true;
}

bool InputRouter::Dirty() const noexcept {
    if (_mouseDirty) {
        return true;
    }
    return _root != nullptr && _root->IsTreeChanged();
}

bool InputRouter::BeginFrame(float mouseX, float mouseY) {
    if (_root == nullptr) {
        return false;
    }

    // 本帧有没有新的标脏。有 → 帧计数补满；没有 → 尾巴递减。
    const bool fresh = _mouseDirty || _root->IsTreeChanged();
    if (!fresh && _tailFrames <= 0) {
        ++_idleFrames;  // 干净：这一帧 0 次 query
        return false;
    }

    const HitTarget target = _root->Query(mouseX, mouseY);
    ++_hoverQueries;
    SwitchHover(target.target, mouseX, mouseY);

    _mouseDirty = false;
    _root->ClearTreeChanged();  // 场景侧的「可能过期」到这里就算消费掉了
    if (fresh) {
        _tailFrames = kTailFrames;
    } else {
        --_tailFrames;
    }
    return true;
}

bool InputRouter::PointerDown(float x, float y, PointerButton button) {
    if (_root == nullptr) {
        return false;
    }

    _button = button;
    const HitTarget target = _root->Query(x, y);  // 点击单独查一次
    ++_clickQueries;
    _pressed = target.target;
    SwitchHover(target.target, x, y);

    if (_pressed == nullptr) {
        return false;
    }

    PointerEvent event;
    event.x = x;
    event.y = y;
    event.action = PointerAction::Down;
    event.button = button;
    return Deliver(_pressed, event, &InkingScene::onPointerDown);
}

bool InputRouter::PointerUp(float x, float y, PointerButton button) {
    if (_root == nullptr) {
        return false;
    }

    _button = button;
    const HitTarget target = _root->Query(x, y);  // 点击单独查一次
    ++_clickQueries;
    SwitchHover(target.target, x, y);

    InkingScene* pressed = _pressed;
    _pressed = nullptr;
    if (pressed == nullptr) {
        return false;
    }

    PointerEvent event;
    event.x = x;
    event.y = y;
    event.button = button;

    if (target.target != pressed) {
        // 在别处松的手：这是一次取消，不是点击。取消只通知按下者本人，不冒泡。
        event.action = PointerAction::Cancel;
        pressed->onPointerCancel(event);
        return false;
    }

    event.action = PointerAction::Up;
    Deliver(pressed, event, &InkingScene::onPointerUp);

    event.action = PointerAction::Up;
    event.handled = false;
    return Deliver(pressed, event, &InkingScene::onClick);
}

InkingScene* InputRouter::GetHovered() const noexcept {
    return _hovered;
}

InkingScene* InputRouter::GetPressed() const noexcept {
    return _pressed;
}

std::uint64_t InputRouter::HoverQueries() const noexcept {
    return _hoverQueries;
}

std::uint64_t InputRouter::ClickQueries() const noexcept {
    return _clickQueries;
}

std::uint64_t InputRouter::IdleFrames() const noexcept {
    return _idleFrames;
}

bool InputRouter::Deliver(InkingScene* target, PointerEvent& event, Handler handler) {
    // 命中目标先处理，返回 false 就交给父场景——一路到输入面为止（docs/InputDesign.md §6）。
    for (InkingScene* node = target; node != nullptr; node = node->GetParentScene()) {
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

void InputRouter::SwitchHover(InkingScene* target, float x, float y) {
    if (target == _hovered) {
        return;
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
}

}  // namespace ink
