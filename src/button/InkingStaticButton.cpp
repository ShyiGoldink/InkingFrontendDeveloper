#include <button/InkingStaticButton.h>

#include "ButtonPaint.h"

#include <button/ButtonLibrary.h>
#include <ink/basic/InkLog.h>

#include <utility>
#include <string>

namespace ink {

namespace {

/**
 * "按名字构造却查不到配置"发生了多少次。
 *
 * 函数内静态量而不是文件级静态量：避开静态初始化顺序问题
 * （AGENTS §6 第 12 条）。只在 UI 线程构造按钮，不需要原子。
 */
int& unknownNameCount() {
    static int count = 0;
    return count;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

InkingStaticButton::InkingStaticButton(InkingAnchor* parent,
                                       const ButtonData& data)
    : InkingStaticAnchor(parent, data.name) {
    applyData(data);
    finishConstruction(/*foundInLibrary=*/true);
}

InkingStaticButton::InkingStaticButton(InkingAnchor* parent,
                                       const std::string& name)
    : InkingStaticAnchor(parent, name) {
    const ButtonData* registered = FindData(name);
    if (registered != nullptr) {
        applyData(*registered);
    } else {
        // 没登记过：名字拼错，或者这条用法该换成**生成出来的类**。
        //
        // 这里**故意不退回"一份看起来正常的默认外观"**：那样按钮长得不对，
        // 却没有任何报错，只能靠肉眼发现（第一版就是这样）。现在写一条错误
        // 日志、给一个空按钮、并让计数加一——于是它是**可断言**的。
        ++unknownNameCount();
        INK_LOG_ERROR(
            std::string(kButtonModuleName),
            "按名字构造却查不到配置，给了一个空按钮：" + name
                + "（名字拼错了？或者这里该用生成出来的按钮类——"
                  "生成的类是数据烘在类里，不查表）");

        ButtonData empty;
        empty.name = name;
        applyData(empty);
    }
    finishConstruction(/*foundInLibrary=*/registered != nullptr);
}

int InkingStaticButton::UnknownNameCount() noexcept {
    return unknownNameCount();
}

void InkingStaticButton::finishConstruction(bool /*foundInLibrary*/) {
    // 尺寸为 0 的按钮画不出来也点不到，这几乎总是配置漏了 width/height。
    if (_data.width <= 0 || _data.height <= 0) {
        INK_LOG_WARN(std::string(kButtonModuleName),
                     "按钮尺寸非法（宽 " + std::to_string(_data.width)
                         + " × 高 " + std::to_string(_data.height)
                         + "），名字：" + _data.name);
    }
}

void InkingStaticButton::applyData(const ButtonData& data) {
    // 拷一份再收尾：调用方给的 ButtonData 可能是"还没补齐三态"的，
    // 而本类要求 _data 永远处于补齐状态（渲染与命中都按它来）。
    _data = data;
    _data.Normalize();

    // 三态外观展开存好（ButtonLook 里），渲染时按状态直接取，
    // 不在每帧去判一遍。状态机也一起复位。
    _look.Reset(_data);

    if (!_data.text.path.empty() || _data.text.fontSize > 0.0f) {
        // text.path 明确写了字体时，只记下来：加载字体属于文字渲染那一段，
        // 现在没有字形图集，加载了也无处可用（SDL3_ttf 已接入，见 docs/SETUP.md）。
        INK_LOG_DEBUG(std::string(kButtonModuleName),
                      "按钮配了字体路径，但文字渲染尚未落地（现在是占位块）："
                          + _data.text.path);
    }

    // 基类那份 color 跟着**当前状态**走：本类重写了 onRender，用不到它，
    // 但保持一致可以避免"从基类窗口看颜色"时读到另一个答案。
    syncBaseColor();

    // -----------------------------------------------------------------------
    // 几何：写进基类
    //
    // 这里为什么要直接写基类的受保护成员，而不是走某个写入口：
    //   1. 静态组件的几何是**构造即定型**的，那些写入口（Resize /
    //      ChangeSelfAnchor / …）按设计只存在于动态档，加了就破掉
    //      "拿到 InkingAnchor* 也改不了静态几何"这条类型上的保证；
    //   2. 但"按名字构造"这条路，基类构造函数只拿到名字（它不知道 ButtonData
    //      这个类型，也不该知道），尺寸与锚点是**在这之后**才从库里取到的。
    //      不给它写进去，按钮就是个 0×0——画不出来也点不到。
    //
    // 所以这是"构造期一次性写入"，不是运行期几何变化。写完必须通知场景：
    // 绘制列表里存着绝对坐标快照，几何变了不通知，它的静态子孙会停在旧值
    // （AGENTS §6 第 19 条）。
    //
    // 副作用（有意保留）：从构造函数里调一个虚函数（NotifySceneStructureChanged
    // 底下会走到场景）在构造期是安全的——它只标脏、不读派生类的状态。
    // -----------------------------------------------------------------------
    _width = _data.width > 0 ? _data.width : 0;
    _height = _data.height > 0 ? _data.height : 0;
    _selfAnchor = _data.selfAnchor;
    _traceAnchor = _data.traceAnchor;
    _offsetX = _data.offsetX;
    _offsetY = _data.offsetY;

    // 可见性与层级走现成的写入口：它们会自己夹取、自己判"真的变了没有"，
    // 也就不可能和基类的规则分叉（例如 zindex 的下限）。
    SetVisible(_data.visible);
    ChangeZIndex(_data.zIndex);

    // 几何定下来了 → 这一份"待消费的改动"就没了。静态组件的几何此后不会再变，
    // 所以命中表建好之后，_dirty 应该一直是 false。
    ClearDirty();

    // 几何是构造期写入的，场景列表里的坐标快照要重算一次。
    NotifySceneStructureChanged();
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const ButtonData* InkingStaticButton::FindData(const std::string& name) {
    return ButtonLibrary::Find(name);
}

bool InkingStaticButton::HitTest(float worldX, float worldY) const {
    // 世界坐标 → 本地 → 形状判定，三步和绘制共用同一套定义
    // （见 detail::HitTestButton：它会把当前变换取逆过一次）。
    return detail::HitTestButton(*this, GetShape(), _look.displayTransform,
                                 worldX, worldY);
}

ButtonState InkingStaticButton::GetState() const noexcept {
    return _look.state;
}

const ShapeSpec& InkingStaticButton::GetShape() const noexcept {
    return _data.shape;
}

const ButtonAppearance& InkingStaticButton::GetAppearance() const noexcept {
    return _look.Current();
}

std::uint32_t InkingStaticButton::GetDisplayColor() const noexcept {
    // 过渡进行中它是插值出来的中间色；没有过渡时等于当前状态那份配置的颜色。
    return _look.displayColor;
}

const TransformSpec& InkingStaticButton::GetDisplayTransform() const noexcept {
    // 同样：过渡进行中是插值出来的中间变换，没有过渡时就是当前状态那份配置的。
    return _look.displayTransform;
}

ShapeBounds InkingStaticButton::GetHitEnvelope() const {
    // 静态 / 动态共用同一份算法（src/button/ButtonPaint.cpp）：
    // "三态变换的并集"对表条目和洞是同一件事——它可能盖住哪儿。
    return detail::ButtonHitEnvelope(_data, GetShape(), GetWidth(), GetHeight());
}

const ButtonAppearance& InkingStaticButton::GetAppearance(
    ButtonState state) const noexcept {
    return appearanceFor(state);
}

const ButtonData& InkingStaticButton::GetData() const noexcept {
    return _data;
}

const std::string& InkingStaticButton::GetText() const noexcept {
    return _data.label;
}

void InkingStaticButton::SetText(const std::string& text) {
    // 文字不参与命中，所以这里**不标脏**、也不通知场景：
    // 会标脏的只有几何 / 可见性 / 层级 / 鼠标移动这四件事。
    _data.label = text;
}

bool InkingStaticButton::HasOnClicked() const noexcept {
    return static_cast<bool>(_onClicked);
}

void InkingStaticButton::SetOnClicked(ButtonClickCallback callback) {
    _onClicked = std::move(callback);
}

const ButtonAppearance& InkingStaticButton::appearanceFor(
    ButtonState state) const noexcept {
    // 规则本体在 ButtonLook（静态 / 动态共用），这里只是转发。
    return _look.For(state);
}

ButtonState InkingStaticButton::ResolveState(bool hover, bool press) noexcept {
    // 规则本体在 ButtonLook（静态 / 动态共用），这里只是转发。
    return ButtonLook::Resolve(hover, press);
}

void InkingStaticButton::syncBaseColor() noexcept {
    // 基类的 color 跟着**当前显示**的颜色走（过渡中是插值色），
    // 保持"从基类读到的是同一件事"。
    _color = _look.displayColor;
}

// ---------------------------------------------------------------------------
// 每渲染帧一次：三态之间的颜色过渡与变换（本地几何一动不动）
// ---------------------------------------------------------------------------

void InkingStaticButton::onAnimationTick(float deltaSeconds) {
    // `Advance` 在没有过渡在跑时直接返回两个 false（稳态 = 一个分支 + 什么都不做）。
    const ButtonLook::AdvanceResult advanced = _look.Advance(deltaSeconds);
    if (!advanced.repaint && !advanced.hit) {
        return;
    }

    if (advanced.repaint) {
        syncBaseColor();
        // 画面变了 → **重绘脏**。注意这里**不能**标成命中脏：颜色/变换不改命中表，
        // 标错了就会每帧白重建一次绘制列表（排序 + 重算所有静态坐标快照）。
        MarkRepaintDirty();
    }
    if (advanced.hit) {
        // 变换变了 → **命中脏**：按钮转过去以后，同一个点可能落到别的组件上，
        // 悬停必须重算。但它仍然**不改表**——表按本地形状烘，查询时把点
        // 反变换回本地即可（docs/InputDesign.md §11 的变换通道）。
        MarkDirty();
    }
}

// ---------------------------------------------------------------------------
// [final] 状态写入口：幂等，只换颜色
// ---------------------------------------------------------------------------

bool InkingStaticButton::MouseHover(bool hover) noexcept {
    // 标志的进出与三态的重算都收在 ButtonLook 里：返回 false 有两种情形
    // （标志没变、或标志变了但三态没变——例如拖拽时指针滑出去），
    // 两种都不该换色，所以这里直接返回。
    if (!_look.SetHovered(hover)) {
        return false;
    }
    syncBaseColor();
    return true;
}

bool InkingStaticButton::MousePress(bool press) noexcept {
    if (!_look.SetPressed(press)) {
        return false;
    }
    syncBaseColor();
    return true;
}

bool InkingStaticButton::TriggerClick() {
    if (!_onClicked) {
        return false;
    }
    // 回调里可能把按钮析构掉（例如"点一下就关掉这个场景"），所以先取出来
    // 再调：直接 _onClicked() 的话，回调内部一旦销毁本对象，
    // 返回时就是在已释放的 std::function 上收尾。
    const ButtonClickCallback callback = _onClicked;
    callback();
    return true;
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void InkingStaticButton::onRender(float pixelX, float pixelY) const {
    // 画什么由 src/button/ButtonPaint.cpp 提供，静态 / 动态按钮共用同一份：
    // 两份实现各自演化就会出现"静态按钮和动态按钮画得不一样"。
    // 颜色取**当前显示**那份（过渡中是插值色），变换同样取当前那份
    // （命中那边会对它取逆，两边必须同源）。
    detail::PaintButtonShape(*this, GetShape(), _look.displayTransform,
                             _look.displayColor, pixelX, pixelY);
    if (_data.HasLabel()) {
        renderText(pixelX, pixelY);
    }
}

void InkingStaticButton::renderText(float pixelX, float pixelY) const {
    detail::PaintButtonText(*this, _data, pixelX, pixelY);
}

}  // namespace ink
