#include <button/InkingDynamicButton.h>

#include "ButtonPaint.h"

#include <button/ButtonLibrary.h>
#include <ink/basic/InkLog.h>
#include <scene/InkingScene.h>

#include <utility>
#include <string>

namespace ink {

namespace {

/**
 * "按名字构造却查不到配置"发生了多少次。
 *
 * 和静态版**各记各的**：一份计数被两条路共用的话，"静态按钮名字拼错了"
 * 会被"动态按钮名字拼错了"顶掉，断言就指不到具体是哪一个出的问题。
 * 函数内静态量，避开静态初始化顺序问题（AGENTS §6 第 12 条）。
 */
int& unknownNameCount() {
    static int count = 0;
    return count;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingDynamicButton::InkingDynamicButton(InkingAnchor* parent,
                                         const ButtonData& data)
    : InkingDynamicAnchor(parent, data.name) {
    applyData(data);
    finishConstruction();
}

InkingDynamicButton::InkingDynamicButton(InkingAnchor* parent,
                                         const std::string& name)
    : InkingDynamicAnchor(parent, name) {
    const ButtonData* registered = FindData(name);
    if (registered != nullptr) {
        applyData(*registered);
    } else {
        // 和静态版同一条规矩：**故意不退回"一份看起来正常的默认外观"**——
        // 那样按钮长得不对却没有任何报错，只能靠肉眼发现。
        ++unknownNameCount();
        INK_LOG_ERROR(
            std::string(kButtonModuleName),
            "按名字构造动态按钮却查不到配置，给了一个空按钮：" + name
                + "（名字拼错了？或者这里该用生成出来的按钮类——"
                  "生成的类是数据烘在类里，不查表）");

        ButtonData empty;
        empty.name = name;
        applyData(empty);
    }
    finishConstruction();
}

int InkingDynamicButton::UnknownNameCount() noexcept {
    return unknownNameCount();
}

void InkingDynamicButton::finishConstruction() {
    if (_data.width <= 0 || _data.height <= 0) {
        INK_LOG_WARN(std::string(kButtonModuleName),
                     "动态按钮尺寸非法（宽 " + std::to_string(_data.width)
                         + " × 高 " + std::to_string(_data.height)
                         + "），名字：" + _data.name);
    }
}

void InkingDynamicButton::applyData(const ButtonData& data) {
    // 拷一份再收尾：调用方给的 ButtonData 可能是"还没补齐三态"的。
    _data = data;
    _data.Normalize();
    _look.Reset(_data);

    // -----------------------------------------------------------------------
    // 几何：走**动态档的写入口**，而不是像静态版那样直接写基类的受保护成员
    //
    // 这正是两档最本质的差别：写入口自己会 `MarkDirty()`、自己会
    // `NotifySceneStructureChanged()`（绘制列表里存着绝对坐标快照，不通知的话
    // 静态子孙会停在旧值——AGENTS §6 第 19 条），所以"改了几何忘了通知"
    // 这条路在动态档上根本不存在。
    //
    // 静态版相反：它没有写入口（那是有意的类型保证），只能在构造期一次性
    // 把几何写进基类，然后自己补一次通知。
    //
    // 在构造函数体里调它们是安全的：写入口只标脏、只通知场景，不读派生类状态。
    // -----------------------------------------------------------------------
    Resize(_data.width > 0 ? _data.width : 0,
           _data.height > 0 ? _data.height : 0);
    ChangeSelfAnchor(_data.selfAnchor);
    ChangeTraceAnchor(_data.traceAnchor);
    ChangeOffset(_data.offsetX, _data.offsetY);

    // 可见性与层级走现成的写入口：它们会自己夹取、自己判"真的变了没有"。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    syncBaseColor();

    // 构造期这批几何写入已经通知过场景了，没有"等着被消费"的改动。
    // 之后每次 Resize / ChangeOffset 都会自己标脏——那份脏标记是给将来的
    // 命中表 / 重绘机制用的（T-3），本类不消费它。
    ClearDirty();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const ButtonData* InkingDynamicButton::FindData(const std::string& name) {
    return ButtonLibrary::Find(name);
}

bool InkingDynamicButton::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，和静态版共用同一份实现
    // （含"用当前变换取逆"那一步）。动态按钮的几何每帧可能变，所以
    // `GetAbsX/Y` 与 `GetDisplayTransform` 都是**现算**的，正好对。
    return detail::HitTestButton(*this, GetShape(), _look.displayTransform,
                                 worldX, worldY);
}

ButtonState InkingDynamicButton::GetState() const noexcept {
    return _look.state;
}

const ShapeSpec& InkingDynamicButton::GetShape() const noexcept {
    return _data.shape;
}

const ButtonAppearance& InkingDynamicButton::GetAppearance() const noexcept {
    return _look.Current();
}

const ButtonAppearance& InkingDynamicButton::GetAppearance(
    ButtonState state) const noexcept {
    return appearanceFor(state);
}

const ButtonData& InkingDynamicButton::GetData() const noexcept {
    return _data;
}

const std::string& InkingDynamicButton::GetText() const noexcept {
    return _data.label;
}

void InkingDynamicButton::SetText(const std::string& text) {
    // 文字不参与命中，也不影响几何：不标脏。
    _data.label = text;
}

bool InkingDynamicButton::HasOnClicked() const noexcept {
    return static_cast<bool>(_onClicked);
}

void InkingDynamicButton::SetOnClicked(ButtonClickCallback callback) {
    _onClicked = std::move(callback);
}

const ButtonAppearance& InkingDynamicButton::appearanceFor(
    ButtonState state) const noexcept {
    return _look.For(state);
}

ButtonState InkingDynamicButton::ResolveState(bool hover, bool press) noexcept {
    return ButtonLook::Resolve(hover, press);
}

std::uint32_t InkingDynamicButton::GetDisplayColor() const noexcept {
    // 过渡进行中它是插值出来的中间色；没有过渡时等于当前状态那份配置的颜色。
    return _look.displayColor;
}

const TransformSpec& InkingDynamicButton::GetDisplayTransform() const noexcept {
    return _look.displayTransform;
}

ShapeBounds InkingDynamicButton::GetHitEnvelope() const {
    // 只用于登记动态洞（动态组件不进表）。和静态版共用同一份算法。
    return detail::ButtonHitEnvelope(_data, GetShape(), GetWidth(), GetHeight());
}

void InkingDynamicButton::syncBaseColor() noexcept {
    _color = _look.displayColor;
}

bool InkingDynamicButton::IsPointerOver() const noexcept {
    return _pointerOver;
}

bool InkingDynamicButton::IsPressCaptured() const noexcept {
    return _pressedOnButton;
}

bool InkingDynamicButton::TriggerClick() {
    if (!_onClicked) {
        return false;
    }
    // 回调里可能把按钮析构掉（例如"点一下就关掉这个场景"），所以先取出来再调。
    const ButtonClickCallback callback = _onClicked;
    callback();
    return true;
}

// ---------------------------------------------------------------------------
// 输入：自己从场景拉（静态版是场景推过来）
// ---------------------------------------------------------------------------

bool InkingDynamicButton::JudgePointer() noexcept {
    InkingScene* scene = GetScene();
    if (scene == nullptr) {
        // 不在任何场景下就没有指针可读：动态按钮的输入只有这一条来源。
        // 这时把它退回"没被指到"的样子，而不是留着上一次的判定——不然
        // 它被移出场景、过一会儿又登记回来时，会带着一份过期的悬停状态。
        const bool hoverChanged = _look.SetHovered(false);
        const bool pressChanged = _look.SetPressed(false);
        _pointerOver = false;
        _pressedOnButton = false;
        _wasDown = false;
        const bool changed = hoverChanged || pressChanged;
        if (changed) {
            syncBaseColor();
        }
        return changed;
    }

    const bool inside = scene->IsPointerInside();
    // 按下用的是**原始按键状态**，不折进 inside：和静态版一致——按着不放把指针
    // 拖出窗口，仍然是"按着"（`DispatchPointer` 那边也是这么用 _pointerDown 的）。
    const bool down = scene->IsPointerDown();

    // 命中自判：世界坐标 → 本地坐标，问自己的形状。
    // 指针不在窗口里时一律算"不在按钮上"（窗口外不该继续悬停）。
    _pointerOver = inside
                && HitTest(scene->GetPointerX(), scene->GetPointerY());

    const bool justPressed = down && !_wasDown;
    const bool justReleased = !down && _wasDown;
    // 抬起是否算一次点击：按下是在它身上按的，且松手时指针还在它身上。
    // 按着滑出去再松开是**取消**——那是用户唯一能表达"我反悔了"的方式。
    const bool click = justReleased && _pressedOnButton && _pointerOver;
    _wasDown = down;

    if (justPressed) {
        // 按下那一刻指针在不在它身上，决定这次按下算不算它的。
        // 在别处按下、按着挪进来的，不算（那一趟是别的组件抓住的）。
        _pressedOnButton = _pointerOver;
    }
    if (justReleased) {
        _pressedOnButton = false;
    }

    // 三态：按下 > 悬停 > 通常。按下用的是"这次按下算在按钮上"而不是"指针
    // 此刻在按钮上"——所以按着滑出去仍然是按下，和静态版的手感一致。
    const bool hoverChanged = _look.SetHovered(_pointerOver);
    const bool pressChanged = _look.SetPressed(down && _pressedOnButton);
    const bool changed = hoverChanged || pressChanged;
    if (changed) {
        syncBaseColor();
    }

    if (click) {
        TriggerClick();
    }
    return changed;
}

void InkingDynamicButton::onTick(float /*deltaSeconds*/) {
    // 输入在这里发生：动态组件的 Tick 是固定逻辑步（InkingScene::TickLogic
    // 遍历绘制列表里的动态节点逐个调），所以"输入"和"几何推进"同拍——
    // 状态必须和渲染同 t，否则快速动画下会差帧（docs/InputDesign.md §7）。
    JudgePointer();
}

void InkingDynamicButton::onAnimationTick(float deltaSeconds) {
    // 三态之间的颜色过渡与变换走**渲染帧**这条通道（和静态版共用 ButtonLook）。
    // 它和上面的 onTick 各管各的：输入 / 几何在逻辑步，颜色/变换过渡在渲染帧。
    const ButtonLook::AdvanceResult advanced = _look.Advance(deltaSeconds);
    if (!advanced.repaint && !advanced.hit) {
        return;
    }

    if (advanced.repaint) {
        syncBaseColor();
        // 画面变了 → 重绘脏（不是命中脏：颜色/变换不改表）。
        MarkRepaintDirty();
    }
    if (advanced.hit) {
        // 变换变了 → 命中脏：它改变"点在不在形状里"的答案（反变换跟着变）。
        MarkDirty();
    }
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void InkingDynamicButton::onRender(float pixelX, float pixelY) const {
    // 和静态版同一份实现（src/button/ButtonPaint.cpp）：动态按钮差的是
    // 几何与输入，不是"画出来长什么样"。
    // 颜色与变换都取**当前显示**那一份（命中那边对同一个变换取逆）。
    detail::PaintButtonShape(*this, GetShape(), _look.displayTransform,
                             _look.displayColor, pixelX, pixelY);
    if (_data.HasLabel()) {
        detail::PaintButtonText(*this, _data, pixelX, pixelY);
    }
}

}  // namespace ink
