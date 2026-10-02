#include <ink/basic/InkingAnchor.h>

#include <scene/InkingScene.h>

// 渲染器槽与填色助手：框架内部共用，见 src/ink/basic/InkingDraw.h。
#include <ink/basic/InkingDraw.h>

namespace ink {

namespace {

/**
 * 注册序号的来源：只增不减的进程内计数器。
 *
 * 构造时自动赋值的意义是让 IsAbove 在 z 相同时有一条确定的先后规则
 * （后构造的在上面），而不是靠未定义的遍历顺序。以前这件事要调用方
 * 自己调 SetRegisterOrder，漏一次就退化成"顺序看运气"。
 */
int nextRegisterOrder() {
    static int counter = 0;
    return ++counter;
}

/** 设计坐标 → 设备像素的倍率；展示倍率非法时退回 1。 */
float magnify(const InkingAnchor& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

/**
 * 层级下限：低于背景层的 z 一律夹到背景层。
 *
 * 绘制是从 z 低到高提交的，"画得最晚的盖在最上面"。如果允许 z 任意低，
 * 一块铺满整屏的背景板只要 z 比某个组件还低、又恰好排在它后面画，
 * 就会把那个组件整个盖掉——而且这种情况下"谁盖谁"完全取决于注册序号，
 * 属于最难查的一类错。夹住下限，背景层之下的空间就不存在了。
 */
int clampZIndex(int zIndex) {
    return zIndex < InkingZIndex::background ? InkingZIndex::background : zIndex;
}

/**
 * 第 0 层渲染：一块实心矩形，颜色取节点的 color。
 *
 * 静态档和动态档共用这一份，所以放在自由函数里——两者是兄弟关系
 * （都直接继承 InkingAnchor），静态版不是动态版的基类，借不到对方的实现。
 *
 * 它走的是**同一套形状填充**（`detail::fillShape`）：
 * "矩形"在这里就是 `ShapeSpec::Rect()`，变换是单位变换（基类没有变换这个概念，
 * 变换属于"组件怎么摆"的知识，归组件自己），于是填充只有一份实现，
 * 抗锯齿、展示倍率换算这些事也不会出现"按钮走一条路、默认矩形走另一条路"。
 * 基类依然不认识形状——形状是组件自己的事，这里只是选了最朴素的那一种。
 */
void drawDefaultRect(const InkingAnchor& node, float pixelX, float pixelY) {
    detail::fillShape(detail::currentRenderer(), ShapeSpec::Rect(),
                      node.GetWidth(), node.GetHeight(), magnify(node), pixelX,
                      pixelY, TransformSpec{}, node.GetColor());
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

InkingAnchor::InkingAnchor(InkingAnchor* parent, const std::string& name,
                           bool dynamic)
    : _name(name),
      _registerOrder(nextRegisterOrder()),
      _dynamic(dynamic),
      _parent(parent) {
    // 先记住自己在哪个场景（沿父链找），再向场景登记。
    // 这一步是 O(深度)，而且只在构造时付一次，不是每帧。
    RegisterToScene(GetSceneAncestor());
}

InkingAnchor::InkingAnchor(InkingAnchor* parent, const AnchorData& data,
                           bool dynamic)
    : _name(),
      _selfAnchor(data.selfAnchor),
      _traceAnchor(data.traceAnchor),
      _offsetX(data.offsetX),
      _offsetY(data.offsetY),
      _width(data.width > 0 ? data.width : 0),
      _height(data.height > 0 ? data.height : 0),
      _zIndex(clampZIndex(data.zIndex)),
      _registerOrder(nextRegisterOrder()),
      _visible(data.visible),
      // 顺序按**声明顺序**写：_dynamic 在头文件里声明在 _color 之前，
      // 初始化列表里写反了编译器会按声明顺序初始化，并给一条 -Wreorder
      // （读代码的人会以为写在前面的先初始化）。
      _dynamic(dynamic),
      _color(data.color),
      _parent(parent) {
    RegisterToScene(GetSceneAncestor());
}

InkingAnchor::~InkingAnchor() {
    // 从所属场景的绘制列表里摘掉自己。
    //
    // 这一步不能省：场景的绘制列表存的是裸指针，节点销毁后如果还留在列表里，
    // 就是一份野指针，下一帧提交时直接崩（docs/InputDesign.md §8 那条
    // "生命周期成对处理"说的就是这个）。场景已经先销毁时 _scene 已经被摘成
    // nullptr，这里是空操作。
    UnregisterFromScene();
}

// ---------------------------------------------------------------------------
// 只读查询
// ---------------------------------------------------------------------------

const std::string& InkingAnchor::GetName() const noexcept {
    return _name;
}

InkingAnchor* InkingAnchor::GetParent() const noexcept {
    return _parent;
}

const Anchor& InkingAnchor::GetSelfAnchor() const noexcept {
    return _selfAnchor;
}

const Anchor& InkingAnchor::GetTraceAnchor() const noexcept {
    return _traceAnchor;
}

int InkingAnchor::GetWidth() const noexcept {
    return _width;
}

int InkingAnchor::GetHeight() const noexcept {
    return _height;
}

float InkingAnchor::GetOffsetX() const noexcept {
    return _offsetX;
}

float InkingAnchor::GetOffsetY() const noexcept {
    return _offsetY;
}

// 自己矩形里的「自身锚点」那个点，落在父级矩形里的「上级锚点」那个点上：
//   parentW * traceAnchor.x == GetX() + ownW * selfAnchor.x
float InkingAnchor::GetX() const noexcept {
    if (_parent == nullptr) {
        return _offsetX;
    }
    return static_cast<float>(_parent->_width) * _traceAnchor.x
         - static_cast<float>(_width) * _selfAnchor.x
         + _offsetX;
}

float InkingAnchor::GetY() const noexcept {
    if (_parent == nullptr) {
        return _offsetY;
    }
    return static_cast<float>(_parent->_height) * _traceAnchor.y
         - static_cast<float>(_height) * _selfAnchor.y
         + _offsetY;
}

float InkingAnchor::GetAbsX() const noexcept {
    if (_parent == nullptr) {
        return _offsetX;
    }
    return _parent->GetAbsX() + GetX();
}

float InkingAnchor::GetAbsY() const noexcept {
    if (_parent == nullptr) {
        return _offsetY;
    }
    return _parent->GetAbsY() + GetY();
}

int InkingAnchor::GetZIndex() const noexcept {
    return _zIndex;
}

int InkingAnchor::GetRegisterOrder() const noexcept {
    return _registerOrder;
}

bool InkingAnchor::IsVisible() const noexcept {
    return _visible;
}

bool InkingAnchor::IsVisibleInTree() const noexcept {
    // 父链上任何一层不可见，自己就不该被画出来。
    for (const InkingAnchor* node = this; node != nullptr; node = node->_parent) {
        if (!node->_visible) {
            return false;
        }
    }
    return true;
}

std::uint32_t InkingAnchor::GetColor() const noexcept {
    return _color;
}

float InkingAnchor::GetMagnification() const noexcept {
    return _magnification;
}

float InkingAnchor::GetDeviceWidth() const noexcept {
    return static_cast<float>(_width) * _magnification;
}

float InkingAnchor::GetDeviceHeight() const noexcept {
    return static_cast<float>(_height) * _magnification;
}

bool InkingAnchor::IsDirty() const noexcept {
    return _dirty;
}

void InkingAnchor::ClearDirty() noexcept {
    _dirty = false;
    _hitQuietFrames = 0;
}

bool InkingAnchor::IsRepaintDirty() const noexcept {
    return _repaintDirty;
}

void InkingAnchor::ClearRepaintDirty() noexcept {
    _repaintDirty = false;
    _repaintQuietFrames = 0;
}

void InkingAnchor::ConsumeDirtyFrame() noexcept {
    // 帧末统一消费的**节点那一半**：只推进计时，不清"刚标过的"。
    // 顺序很关键——先把计数加一再看够不够，于是"标脏那一帧"算第 1 帧安静，
    // 连续 kXxxQuietFrames 帧没有新脏才清。
    if (_repaintDirty) {
        ++_repaintQuietFrames;
        if (_repaintQuietFrames >= kRepaintQuietFrames) {
            _repaintDirty = false;
            _repaintQuietFrames = 0;
        }
    }
    if (_dirty) {
        ++_hitQuietFrames;
        if (_hitQuietFrames >= kHitQuietFrames) {
            _dirty = false;
            _hitQuietFrames = 0;
        }
    }
}

bool InkingAnchor::IsSceneRoot() const noexcept {
    // 场景 / 窗口自己就是根：它们没有父级，也不登记到别的树里。
    return _parent == nullptr;
}

bool InkingAnchor::IsDynamic() const noexcept {
    return _dynamic;
}

InkingScene* InkingAnchor::GetScene() const noexcept {
    return _scene;
}

bool InkingAnchor::HitTest(float /*worldX*/, float /*worldY*/) const {
    // 基类不回答：它不可实例化，也没有形状。具体组件按自己的形状判定
    // （按钮走 ShapeContains，和渲染共用同一份定义）。
    return false;
}

ShapeBounds InkingAnchor::GetHitEnvelope() const {
    // 保守默认：整块矩形。基类不认识形状（形状是组件的事），而包络给大了
    // 只是候选多几个、不会误命中，所以这里宁可保守。
    ShapeBounds bounds;
    bounds.x = 0.0f;
    bounds.y = 0.0f;
    bounds.width = static_cast<float>(_width);
    bounds.height = static_cast<float>(_height);
    return bounds;
}

void InkingAnchor::SetHitPassThrough(bool passThrough) noexcept {
    _hitPassThrough = passThrough;
}

bool InkingAnchor::IsHitPassThrough() const noexcept {
    return _hitPassThrough;
}

InkingScene* InkingAnchor::GetSceneAncestor() noexcept {
    // 自己就是根（场景 / 窗口）→ 没有场景祖先可登记。
    if (IsSceneRoot()) {
        return nullptr;
    }
    // 沿父链向上找第一个"由场景盖章过"的节点，也就是场景本身。
    for (InkingAnchor* node = _parent; node != nullptr; node = node->_parent) {
        if (node->_scene != nullptr && node->IsSceneRoot()) {
            return node->_scene;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// 层级
// ---------------------------------------------------------------------------

bool InkingAnchor::IsAbove(const InkingAnchor& a,
                           const InkingAnchor& b) noexcept {
    if (a._zIndex != b._zIndex) {
        return a._zIndex > b._zIndex;
    }
    return a._registerOrder > b._registerOrder;
}

void InkingAnchor::MarkDirty() noexcept {
    _dirty = true;
    // 新脏一到，安静帧计数归零：清脏看的是"**连续**多少帧没有新脏"。
    _hitQuietFrames = 0;
    onDirty();
}

void InkingAnchor::MarkRepaintDirty() noexcept {
    _repaintDirty = true;
    _repaintQuietFrames = 0;
}

void InkingAnchor::MarkStructureChanged() noexcept {
    // 骨架一变，三件事同时成立：
    //   1. 命中结果可能变（几何 / 顺序 / 可见性都影响）→ 命中脏；
    //   2. 画面一定变 → 重绘脏；
    //   3. 绘制列表里缓存的绝对坐标快照过期 → 通知场景重排。
    //
    // 第 3 条只在**真的改了**的时候才该发生，所以写入口都是"先判有没有变、
    // 变了才调这里"，而不是反过来（`Resize` 传同一个尺寸必须什么都不做）。
    MarkDirty();
    MarkRepaintDirty();
    NotifySceneStructureChanged();
}

void InkingAnchor::NotifySceneStructureChanged() noexcept {
    if (_scene != nullptr) {
        _scene->NotifyStructureChanged();
    }
}

// ---------------------------------------------------------------------------
// 渲染树的登记 / 摘除
// ---------------------------------------------------------------------------

void InkingAnchor::RegisterToScene(InkingScene* scene) noexcept {
    if (scene == nullptr) {
        return;
    }
    _scene = scene;
    scene->AddDrawNode(this);
}

void InkingAnchor::RebindScene(InkingScene* scene) noexcept {
    RegisterToScene(scene);
}

void InkingAnchor::UnregisterFromScene() noexcept {
    if (_scene == nullptr) {
        return;
    }
    // 先把指针存下来：摘除之后 _scene 就该是空的。
    InkingScene* scene = _scene;
    _scene = nullptr;
    scene->RemoveDrawNode(this);
}

void InkingAnchor::DetachFromScene() noexcept {
    _scene = nullptr;
}

void InkingAnchor::MarkAsSceneRoot(InkingScene* scene) noexcept {
    _scene = scene;
}

void InkingAnchor::DetachSceneFromSubtree() noexcept {
    // 只能顺着绘制列表走：基类没有子表（子表是将来"整块在动"那档才需要的
    // 东西，见 docs/InputDesign.md §9）。场景在析构前先把自己从列表里每个
    // 节点的 _scene 上摘掉，列表本身随场景一起销毁。
    if (_scene != nullptr) {
        _scene->DetachSceneFromNodes();
    }
}

// ---------------------------------------------------------------------------
// [final] 写入口：几何变化（只有动态组件有）
// ---------------------------------------------------------------------------

bool InkingDynamicAnchor::Resize(int width, int height) noexcept {
    const bool changeWidth =
        (width != InkingResize::none) && (width >= 0) && (width != _width);
    const bool changeHeight =
        (height != InkingResize::none) && (height >= 0) && (height != _height);

    if (!changeWidth && !changeHeight) {
        return false;
    }
    if (changeWidth) {
        _width = width;
    }
    if (changeHeight) {
        _height = height;
    }

    // 几何变了：命中脏 + 重绘脏 + 让场景重算（绘制列表里缓存的绝对坐标过期了）。
    // 最后那条正是"父级一动，静态子节点的缓存绝对坐标会过期"：
    // 动态节点自己每帧现算没问题，但**它的静态子孙**靠的是列表里的快照。
    MarkStructureChanged();
    onSizeChanged();
    return true;
}

bool InkingDynamicAnchor::ChangeSelfAnchor(const Anchor& anchor) noexcept {
    if (_selfAnchor.x == anchor.x && _selfAnchor.y == anchor.y) {
        return false;
    }
    _selfAnchor = anchor;
    MarkStructureChanged();
    onSelfAnchorChanged();
    return true;
}

bool InkingDynamicAnchor::ChangeTraceAnchor(const Anchor& anchor) noexcept {
    if (_traceAnchor.x == anchor.x && _traceAnchor.y == anchor.y) {
        return false;
    }
    _traceAnchor = anchor;
    MarkStructureChanged();
    onTraceAnchorChanged();
    return true;
}

bool InkingDynamicAnchor::ChangeOffset(float offsetX, float offsetY) noexcept {
    if (_offsetX == offsetX && _offsetY == offsetY) {
        return false;
    }
    _offsetX = offsetX;
    _offsetY = offsetY;
    MarkStructureChanged();
    onOffsetChanged();
    return true;
}

// ---------------------------------------------------------------------------
// [final] 写入口：骨架变化（两版共用）
// ---------------------------------------------------------------------------

bool InkingAnchor::SetVisible(bool visible) noexcept {
    if (_visible == visible) {
        return false;
    }
    _visible = visible;
    // 可见性是要标脏的四件事之一；它同时改变绘制列表的内容，
    // 所以还要通知场景重排（场景在重建时会把隐藏子树整个摘掉）。
    MarkStructureChanged();
    onVisibleChanged(visible);
    return true;
}

bool InkingAnchor::ChangeZIndex(int zIndex) noexcept {
    // 夹住下限，理由见 clampZIndex：不然背景板会盖掉一切。
    const int clamped = clampZIndex(zIndex);
    if (_zIndex == clamped) {
        return false;
    }
    _zIndex = clamped;
    // 顺序变了（列表内容没变，但是"谁盖谁"和命中顺序都变了）→ 骨架变化。
    MarkStructureChanged();
    onZIndexChanged(clamped);
    return true;
}

bool InkingAnchor::SetParent(InkingAnchor* parent) noexcept {
    if (_parent == parent) {
        return false;
    }

    // 先摘掉旧登记——必须在改 _parent 之前做：摘除要按**旧的**场景归属来，
    // 改完 _parent 之后 GetSceneAncestor() 给的已经是新场景了。
    UnregisterFromScene();

    // 再换父级并重新算归属：换父级可能跨场景，所以整条链路重来一遍。
    _parent = parent;
    RegisterToScene(GetSceneAncestor());

    // 换父级可能跨场景，位置也跟着变——骨架变化里最重的一种。
    // （上面两步的摘除 / 登记已经各自通知过场景了，这里再走一次统一出口，
    //  把命中脏与重绘脏也一起标上，免得"只通知了场景、没标脏"。）
    MarkStructureChanged();
    onParentChanged(parent);
    return true;
}

void InkingAnchor::SetMagnification(float magnification) noexcept {
    // 只影响绘制换算，不影响标脏
    if (magnification > 0.0f) {
        _magnification = magnification;
    }
}

// ---------------------------------------------------------------------------
// [√] 可重写钩子：基类里都是空的，子类按需重写
// ---------------------------------------------------------------------------

void InkingAnchor::onSizeChanged() {}
void InkingAnchor::onSelfAnchorChanged() {}
void InkingAnchor::onTraceAnchorChanged() {}
void InkingAnchor::onOffsetChanged() {}
void InkingAnchor::onZIndexChanged(int /*zIndex*/) {}
void InkingAnchor::onParentChanged(InkingAnchor* /*parent*/) {}
void InkingAnchor::onVisibleChanged(bool /*visible*/) {}
void InkingAnchor::onDirty() {}

void InkingAnchor::onAnimationTick(float /*deltaSeconds*/) {
    // 基类没有动画：几何变化归动态档的 Tick，颜色过渡归各组件自己。
    // 留这个空实现是为了让"每渲染帧一次"这条通道两档都能走。
}

void InkingAnchor::onRender(float /*pixelX*/, float /*pixelY*/) const {
    // 基类不画：具体画什么由两个具体锚点或它们的子类决定。
}

void InkingAnchor::SubmitToRenderer(SDL_Renderer* renderer, float absX,
                                    float absY) const {
    if (renderer == nullptr) {
        return;
    }

    // 设计坐标 → 设备像素，然后交给节点自己决定画成什么样。
    // 顺带把渲染器挂到"当前渲染器"上，好让 onRender 不用多接一个参数。
    const float scale = magnify(*this);
    detail::currentRendererSlot() = renderer;
    onRender(absX * scale, absY * scale);
    detail::currentRendererSlot() = nullptr;
}

// ---------------------------------------------------------------------------
// 两种锚点：静态档与动态档
// ---------------------------------------------------------------------------

// 静态档：几何构造即定型，所以 dynamic 传 false。
InkingStaticAnchor::InkingStaticAnchor(InkingAnchor* parent,
                                       const AnchorData& data)
    : InkingAnchor(parent, data, false) {}

InkingStaticAnchor::InkingStaticAnchor(InkingAnchor* parent,
                                       const std::string& name)
    : InkingAnchor(parent, name, false) {}

void InkingStaticAnchor::onRender(float pixelX, float pixelY) const {
    // 第 0 层渲染：一块实心矩形。SDF 形状层接手后换掉 drawDefaultRect。
    drawDefaultRect(*this, pixelX, pixelY);
}

// 动态档：几何会自己变，每帧收到一次 Tick。
InkingDynamicAnchor::InkingDynamicAnchor(InkingAnchor* parent,
                                         const AnchorData& data)
    : InkingAnchor(parent, data, true) {}

InkingDynamicAnchor::InkingDynamicAnchor(InkingAnchor* parent,
                                         const std::string& name)
    : InkingAnchor(parent, name, true) {}

void InkingDynamicAnchor::onTick(float /*deltaSeconds*/) {}

void InkingDynamicAnchor::onRender(float pixelX, float pixelY) const {
    // 和静态版一样：默认一块实心矩形。动态组件默认差的只是"位置每帧变"。
    // 这里不能调 InkingStaticAnchor::onRender——两者是**兄弟**（都直接继承
    // InkingAnchor），静态版不是动态版的基类，没有对象可借用。
    drawDefaultRect(*this, pixelX, pixelY);
}

}  // namespace ink
