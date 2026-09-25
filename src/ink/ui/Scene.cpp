#include <ink/ui/Scene.h>

#include <algorithm>
#include <atomic>

#include <ink/basic/InkLog.h>
#include <ink/ui/SceneRegistry.h>

namespace ink {

namespace {

constexpr const char* kModuleName = "Scene";

/// 全局声明序号。场景一建出来就领一个，一辈子不变：
/// 数字大的画得晚、盖在上面——"后加的更靠上"就是这么来的。
int nextOrder() {
    static std::atomic<int> counter{0};
    return ++counter;
}

}  // namespace

Scene::Scene(std::string name, bool dynamic)
    : _name(std::move(name)), _dynamic(dynamic), _order(nextOrder()) {
    SceneRegistry::registerScene(*this);
    INK_LOG_DEBUG(kModuleName, "登记场景：" + _name + (_dynamic ? "（动态）" : "（静态）"));
}

Scene::~Scene() {
    // 先断开孩子，再销毁孩子：孩子析构时会看 _parent，
    // 这里让它们看到的是一根空指针，而不是一块已经归还的内存。
    for (Scene* child : _children) {
        if (child != nullptr && child->_parent == this) {
            child->_parent = nullptr;
        }
    }
    _children.clear();
    _owned.clear();

    if (_parent != nullptr) {
        auto& siblings = _parent->_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        _parent->invalidate();
        _parent = nullptr;
    }

    SceneRegistry::unregisterScene(*this);
}

// ---------------------------------------------------------------------------
// 属性
// ---------------------------------------------------------------------------

const std::string& Scene::GetName() const noexcept {
    return _name;
}

Rect Scene::GetRect() const noexcept {
    return _rect;
}

void Scene::SetRect(const Rect& rect) {
    if (_rect.x == rect.x && _rect.y == rect.y && _rect.w == rect.w && _rect.h == rect.h) {
        return;
    }
    _rect = rect;
    // 几何变了，命中表就过期了。动态场景会在这里直接返回。
    invalidate();
}

bool Scene::GetVisible() const noexcept {
    return _visible;
}

void Scene::SetVisible(bool visible) {
    if (_visible == visible) {
        return;
    }
    _visible = visible;
    invalidate();
}

bool Scene::IsDynamic() const noexcept {
    return _dynamic;
}

int Scene::GetOrder() const noexcept {
    return _order;
}

Scene* Scene::GetParent() noexcept {
    return _parent;
}

const Scene* Scene::GetParent() const noexcept {
    return _parent;
}

const std::vector<Scene*>& Scene::GetChildren() const noexcept {
    return _children;
}

// ---------------------------------------------------------------------------
// 套场景
// ---------------------------------------------------------------------------

Scene& Scene::add(Scene& child) {
    if (child._parent == this) {
        return child;
    }

    // 换爹：先从原父场景的名单里摘掉，否则那边会一直拿着一个"已经不属于它"
    // 的孩子去命中与冒泡。
    if (child._parent != nullptr) {
        Scene* previous = child._parent;
        auto& siblings = previous->_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), &child), siblings.end());
        previous->invalidate();
    }

    child._parent = this;

    // 按声明序号插入：序号大的排后面，画得晚也就是盖在上面的那层。
    const auto position = std::upper_bound(
        _children.begin(), _children.end(), &child,
        [](const Scene* lhs, const Scene* rhs) { return lhs->GetOrder() < rhs->GetOrder(); });
    _children.insert(position, &child);

    invalidate();
    return child;
}

void Scene::addOwned(std::unique_ptr<Scene> child) {
    Scene& raw = *child;
    _owned.push_back(std::move(child));
    add(raw);
}

Scene& Scene::root() noexcept {
    Scene* node = this;
    while (node->_parent != nullptr) {
        node = node->_parent;
    }
    return *node;
}

const Scene& Scene::root() const noexcept {
    const Scene* node = this;
    while (node->_parent != nullptr) {
        node = node->_parent;
    }
    return *node;
}

// ---------------------------------------------------------------------------
// 烘焙：静态场景 → 坐标映射表 + 平铺绘制表
// ---------------------------------------------------------------------------

void Scene::collect(std::vector<HitEntry>& entries,
                    std::vector<HitEntry>& holes,
                    std::vector<DrawItem>& drawList) {
    if (!_visible) {
        return;  // 藏起来的子树既不画也进不了表
    }

    if (_dynamic) {
        // 动态子树不摊平：记成一个"洞"，每帧由它自己的遍历负责。
        holes.push_back(HitEntry{this, _rect, _order});
        drawList.push_back(DrawItem{this, true});
        return;
    }

    if (_rect.valid()) {
        entries.push_back(HitEntry{this, _rect, _order});
    }
    drawList.push_back(DrawItem{this, false});

    for (Scene* child : _children) {
        child->collect(entries, holes, drawList);
    }
}

void Scene::bake() {
    if (_dynamic) {
        // 动态场景没有表可烘：清干净，免得留着上一次的陈旧结果。
        _hitTable.clear();
        _drawList.clear();
        _dynamicHoles.clear();
        _baked = false;
        return;
    }

    std::vector<HitEntry> entries;
    std::vector<HitEntry> holes;
    std::vector<DrawItem> drawList;
    collect(entries, holes, drawList);

    // 洞按 order 降序：查表时从上往下比，先比到大的那层就算数。
    std::sort(holes.begin(), holes.end(),
              [](const HitEntry& lhs, const HitEntry& rhs) { return lhs.order > rhs.order; });

    _hitTable.build(std::move(entries));
    _drawList = std::move(drawList);
    _dynamicHoles = std::move(holes);
    _baked = true;
    _dirty = false;

    INK_LOG_DEBUG(kModuleName,
                  "烘焙场景 " + _name + "：" + std::to_string(_hitTable.size()) + " 条命中、"
                      + std::to_string(_hitTable.candidateCount()) + " 条候选、"
                      + std::to_string(_dynamicHoles.size()) + " 个动态洞");
}

void Scene::invalidate() {
    if (_dynamic) {
        return;  // 动态场景本来就不烘，谈不上标脏
    }
    _dirty = true;
    _baked = false;

    // 一路标到根；撞上动态祖先就停——那种祖先根本没有表，
    // 再往上那些静态祖先也只是把它记成一个洞，不需要重建。
    for (Scene* node = _parent; node != nullptr && !node->_dynamic; node = node->_parent) {
        node->_dirty = true;
        node->_baked = false;
    }
}

bool Scene::needsBake() const noexcept {
    return !_dynamic && (_dirty || !_baked);
}

bool Scene::isBaked() const noexcept {
    return _baked;
}

const HitTable& Scene::GetHitTable() const noexcept {
    return _hitTable;
}

// ---------------------------------------------------------------------------
// 命中
// ---------------------------------------------------------------------------

HitResult Scene::hitTestDynamic(float x, float y) {
    HitResult best;
    if (_rect.contains(x, y)) {
        best.hit = true;
        best.target = this;
        best.order = _order;
    }

    for (auto it = _children.rbegin(); it != _children.rend(); ++it) {
        const HitResult candidate = (*it)->hitTest(x, y);
        if (candidate.hit && (!best.hit || candidate.order > best.order)) {
            best = candidate;
        }
    }
    return best;
}

HitResult Scene::hitTest(float x, float y) {
    if (!_visible) {
        return HitResult{};
    }

    if (_dynamic) {
        return hitTestDynamic(x, y);
    }

    if (needsBake()) {
        bake();  // 懒烘焙：谁先用谁掏这一次成本
    }

    HitResult best = _hitTable.hit(x, y);

    // 表里没有，或者表里的结果层号更低时，才轮到动态子树说话。
    for (const HitEntry& hole : _dynamicHoles) {
        if (best.hit && hole.order <= best.order) {
            continue;
        }
        const HitResult candidate = hole.target->hitTest(x, y);
        if (candidate.hit && (!best.hit || candidate.order > best.order)) {
            best = candidate;
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

void Scene::draw(Canvas& canvas) const {
    if (!_visible) {
        return;
    }

    onDraw(canvas);

    if (!_dynamic && _baked) {
        // 静态场景走平铺表：一次循环画完自己和所有静态子孙。
        for (const DrawItem& item : _drawList) {
            if (item.node == this) {
                continue;  // 自己刚才已经画过
            }
            if (item.recursive) {
                item.node->draw(canvas);  // 动态子树：递归画整棵
            } else {
                item.node->onDraw(canvas);  // 静态结点：平铺表里画一次
            }
        }
        return;
    }

    for (const Scene* child : _children) {
        child->draw(canvas);
    }
}

void Scene::onDraw(Canvas&) const {
    // 默认什么都不画：纯粹用来分组、接管点击的场景不需要外观。
}

// ---------------------------------------------------------------------------
// 每帧
// ---------------------------------------------------------------------------

void Scene::tick(float deltaSeconds) {
    if (_dynamic) {
        onTick(deltaSeconds);
        for (Scene* child : _children) {
            child->tick(deltaSeconds);
        }
        return;
    }

    onTick(deltaSeconds);

    if (!_baked) {
        // 还没烘焙，无从判断哪些子孙是静的，老老实实走一遍。
        for (Scene* child : _children) {
            child->tick(deltaSeconds);
        }
        return;
    }

    // 烘焙过了：静态部分不会动，只有那些洞需要每帧机会。
    for (const HitEntry& hole : _dynamicHoles) {
        hole.target->tick(deltaSeconds);
    }
}

void Scene::onTick(float) {
    // 默认不做事。静态场景收不到这个回调，动态场景才会被每帧叫醒。
}

// ---------------------------------------------------------------------------
// 输入：默认不处理，交给父场景
// ---------------------------------------------------------------------------

bool Scene::onPointerDown(PointerEvent&) {
    return false;
}

bool Scene::onPointerUp(PointerEvent&) {
    return false;
}

void Scene::onPointerEnter(PointerEvent&) {}

void Scene::onPointerLeave(PointerEvent&) {}

void Scene::onPointerCancel(PointerEvent&) {}

bool Scene::onClick(PointerEvent&) {
    return false;
}

// ---------------------------------------------------------------------------
// 窗口命令
// ---------------------------------------------------------------------------

void Scene::requestWindowCommand(WindowCommand command) {
    if (command == WindowCommand::None) {
        return;
    }
    // 场景层不认识窗口，只能把意图挂到根场景上，等窗口层来取。
    root()._commands.push_back(command);
}

WindowCommand Scene::takeWindowCommand() {
    if (_commands.empty()) {
        return WindowCommand::None;
    }
    const WindowCommand command = _commands.front();
    _commands.erase(_commands.begin());
    return command;
}

void Scene::onWindowStateChanged(bool maximized) {
    for (Scene* child : _children) {
        child->onWindowStateChanged(maximized);
    }
}

}  // namespace ink
