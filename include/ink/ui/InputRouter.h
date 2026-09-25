#pragma once

#include <ink/ui/Scene.h>
#include <ink/ui/UiEvent.h>

namespace ink {

// 输入管理：把设计坐标上的一次指针动作，落成"命中谁 + 事件怎么走"。
//
// 命中本身不在这层：静态场景在 Scene::hitTest 里查映射表，动态场景在
// 同一个入口里遍历子树。这里只管三件事：
//   1. 悬停目标的进出（Leave / Enter）；
//   2. 按下的目标是谁，抬手时还算不算同一个（不算就发 Cancel）；
//   3. 事件冒泡——目标不接就交给父场景，一路到根。
class InputRouter {
public:
    /// 指定输入面，通常是根场景。
    void attach(Scene& rootScene);
    void detach();

    /// 这个坐标上是哪个场景（没命中返回 nullptr）。
    Scene* targetAt(float x, float y);

    bool pointerMove(float x, float y);
    bool pointerDown(float x, float y, PointerButton button = PointerButton::Left);
    bool pointerUp(float x, float y, PointerButton button = PointerButton::Left);
    /// 清空悬停与按下状态，不派发任何事件。
    void reset();

    Scene* hovered() const noexcept;
    Scene* pressed() const noexcept;

private:
    using Handler = bool (Scene::*)(PointerEvent&);

    bool deliver(Scene* target, PointerEvent& event, Handler handler);
    bool switchHover(Scene* target, float x, float y);

    Scene*        _root = nullptr;
    Scene*        _hovered = nullptr;
    Scene*        _pressed = nullptr;
    PointerButton _button = PointerButton::Left;
};

}  // namespace ink
