#include <button/InkingStaticButton.h>

#include <button/ButtonLibrary.h>
#include <ink/basic/InkLog.h>
#include <ink/basic/InkingDraw.h>

#include <SDL3/SDL.h>

#include <utility>
#include <string>

namespace ink {

namespace {

/**
 * 设计坐标 → 设备像素的倍率。
 *
 * 基类的 SubmitToRenderer 已经把**左上角**换算过了，但尺寸还得自己乘——
 * 这是"展示倍率不参与布局、只在绘制时换算"那条（docs/API.md「[x] 展示倍率」）。
 */
float magnify(const InkingAnchor& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

/**
 * 配置里没写 fontSize 时用的兜底字号（设计坐标）。
 *
 * 文字真正落地（字形图集）时，这里会换成"机器默认字体在当前 DPI 下的
 * 推荐字号"；现在写死一个常量，是为了让"没配字号"不至于画出一个零高度的
 * 方块——那看起来像 bug，其实是没配。
 */
constexpr float kDefaultFontSize = 16.0f;

/// 文字占位块的颜色兜底：文字色纯透明时看不到，给个不透明的白。
std::uint32_t visibleTextColor(std::uint32_t color) {
    return ColorAlpha(color) == 0u ? 0xFFFFFFFFu : color;
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
        // 没登记过：名字拼错、或者登记代码还没接上（生成器没落地）。
        // 数据用一份 normalize 过的兜底值，至少是个看得见的黑按钮。
        ButtonData fallback;
        fallback.name = name;
        applyData(fallback);
    }
    finishConstruction(/*foundInLibrary=*/registered != nullptr);
}

void InkingStaticButton::finishConstruction(bool foundInLibrary) {
    if (!foundInLibrary) {
        INK_LOG_WARN(std::string(kButtonModuleName),
                     "按名字构造时库里没有这份配置，已退回默认外观："
                         + _data.name
                         + "（生成器落地前请先 ButtonLibrary::Register，"
                           "或改用 ButtonData 构造）");
    }

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

    // 三态外观展开存好，渲染时按状态直接取，不在每帧去判一遍。
    _normal = _data.normal;
    _hover = _data.hover;
    _onclicked = _data.onclicked;

    if (!_data.text.path.empty() || _data.text.fontSize > 0.0f) {
        // text.path 明确写了字体时，只记下来：加载字体属于文字渲染那一段，
        // 现在没有字形图集，加载了也无处可用（SDL3_ttf 已接入，见 docs/SETUP.md）。
        INK_LOG_DEBUG(std::string(kButtonModuleName),
                      "按钮配了字体路径，但文字渲染尚未落地（现在是占位块）："
                          + _data.text.path);
    }

    // 基类那份 color 跟着**当前状态**走：本类重写了 onRender，用不到它，
    // 但保持一致可以避免"从基类窗口看颜色"时读到另一个答案。
    _state = ResolveState(_hovered, _pressed);
    _color = appearanceFor(_state).color;

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
    // 父链上任何一层不可见，就不该还能被点到。
    if (!IsVisibleInTree()) {
        return false;
    }

    // 世界坐标 → 本地坐标（相对按钮左上角）。命中用的是**设计坐标**里的
    // GetAbsX()/GetAbsY()，和渲染提交时用的是同一套推导，所以整体缩放
    // 不需要重烘任何东西（docs/InputDesign.md §2 第 3 条）。
    const float localX = worldX - GetAbsX();
    const float localY = worldY - GetAbsY();

    // 形状是唯一真相：渲染、命中、AABB 都从 GetShape() 派生。
    return ShapeContains(GetShape(), localX, localY, GetWidth(), GetHeight());
}

ButtonState InkingStaticButton::GetState() const noexcept {
    return _state;
}

const ShapeSpec& InkingStaticButton::GetShape() const noexcept {
    return _data.shape;
}

const ButtonAppearance& InkingStaticButton::GetAppearance() const noexcept {
    return appearanceFor(_state);
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
    switch (state) {
        case ButtonState::Hover:
            return _hover;
        case ButtonState::Pressed:
            return _onclicked;
        case ButtonState::Normal:
        default:
            return _normal;
    }
}

ButtonState InkingStaticButton::ResolveState(bool hover, bool press) noexcept {
    // 按下 > 悬停 > 通常。按下期间指针滑出去了也还是"按下"——
    // 用户按着不放往外拖，按钮不该看起来已经松开了。
    if (press) {
        return ButtonState::Pressed;
    }
    return hover ? ButtonState::Hover : ButtonState::Normal;
}

// ---------------------------------------------------------------------------
// [final] 状态写入口：幂等，只换颜色
// ---------------------------------------------------------------------------

bool InkingStaticButton::MouseHover(bool hover) noexcept {
    if (_hovered == hover) {
        return false;
    }
    _hovered = hover;

    const ButtonState next = ResolveState(_hovered, _pressed);
    if (next == _state) {
        // 三态没变（例如拖拽时"在里面"变了但状态还是 Pressed），
        // 那就连颜色都不用换，更不用惊动场景。
        return false;
    }
    _state = next;
    // 基类的 color 跟着当前状态走，保持"从基类读到的是同一件事"。
    _color = appearanceFor(_state).color;
    return true;
}

bool InkingStaticButton::MousePress(bool press) noexcept {
    if (_pressed == press) {
        return false;
    }
    _pressed = press;

    const ButtonState next = ResolveState(_hovered, _pressed);
    if (next == _state) {
        return false;
    }
    _state = next;
    _color = appearanceFor(_state).color;
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
    renderShape(pixelX, pixelY);
    if (_data.HasLabel()) {
        renderText(pixelX, pixelY);
    }
}

void InkingStaticButton::renderShape(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }

    const float scale = magnify(*this);
    const ShapeBounds bounds = GetShapeBounds(GetShape(), GetWidth(),
                                              GetHeight());
    // 设备像素尺寸 = 设计坐标尺寸 × 展示倍率。
    const ShapeBounds scaled{bounds.x * scale, bounds.y * scale,
                             bounds.width * scale, bounds.height * scale};

    // 三态之间的差别现在只体现在颜色上（含 alpha），形状与位置完全不动——
    // 这正是按钮能是**静态**组件的原因（见头文件那段说明）。
    // 形状层接手抗锯齿填充时，换掉 fillShapeBounds 这一处即可。
    detail::fillShapeBounds(renderer, scaled, pixelX, pixelY,
                            appearanceFor(_state).color);
}

void InkingStaticButton::renderText(float pixelX, float pixelY) const {
    SDL_Renderer* renderer = detail::currentRenderer();
    if (renderer == nullptr) {
        return;
    }

    const float scale = magnify(*this);
    const float fontSize = _data.text.fontSize > 0.0f ? _data.text.fontSize
                                                      : kDefaultFontSize;

    // 文字块的左上角：配置里给的是**距组件左上角的间距**，不是坐标。
    // 宽度按"字号 × 字数"估一个，够把区域钉住；真正的字形推进要等图集。
    const float textX = pixelX + _data.text.leftSpace * scale;
    const float textY = pixelY + _data.text.topSpace * scale;
    const float textWidth =
        fontSize * scale * static_cast<float>(_data.label.size());
    const float textHeight = fontSize * scale;

    const SDL_FRect rect{textX, textY, textWidth, textHeight};
    detail::setDrawColor(renderer, visibleTextColor(_data.textColor));
    SDL_RenderFillRect(renderer, &rect);
}

}  // namespace ink
