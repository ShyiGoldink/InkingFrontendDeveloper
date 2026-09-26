#include <scene/InkingScene.h>

#include <algorithm>
#include <atomic>

#include <core/RedrawScheduler.h>
#include <ink/basic/InkLog.h>
#include <scene/SceneRegistry.h>

namespace ink {

namespace {

constexpr const char* kModuleName = "Scene";

/// 全局注册序号。场景一建出来就领一个，一辈子不变：
/// 同 z 时数字大的算在上面——「后加的更靠上」就是这么来的。
int NextOrder() {
    static std::atomic<int> counter{0};
    return ++counter;
}

/// (z, order) 谁在上面：先比 z，z 相同比注册顺序。
/// 语义和 InkingAnchor::IsAbove 一致，这里是它的「裸数字版」，给合并用。
bool zAbove(int z, int order, int otherZ, int otherOrder) noexcept {
    if (z != otherZ) {
        return z > otherZ;
    }
    return order > otherOrder;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构：构造即注册，析构即注销
// ---------------------------------------------------------------------------

InkingScene::InkingScene(const std::string& name, bool dynamic)
    : InkingAnchor(nullptr, name), _dynamic(dynamic) {
    SetRegisterOrder(NextOrder());
    SceneRegistry::Register(*this);
    INK_LOG_DEBUG(kModuleName, "登记场景：" + GetName() + (_dynamic ? "（动态）" : "（静态）"));
}

InkingScene::~InkingScene() {
    // 先断开孩子，再销毁孩子：孩子析构时会看 _parentScene，
    // 这里让它们看到的是一根空指针，而不是一块已经归还的内存。
    for (InkingScene* child : _children) {
        if (child != nullptr && child->_parentScene == this) {
            child->_parentScene = nullptr;
        }
    }
    _children.clear();
    _owned.clear();

    if (_parentScene != nullptr) {
        auto& siblings = _parentScene->_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), this), siblings.end());
        _parentScene->_bakeDirty = true;
        _parentScene->_baked = false;
        _parentScene->MarkTreeChanged();
        _parentScene = nullptr;
    }

    SceneRegistry::Unregister(*this);
}

// ---------------------------------------------------------------------------
// 树
// ---------------------------------------------------------------------------

InkingScene& InkingScene::Add(InkingScene& child) {
    if (child._parentScene == this) {
        return child;
    }

    // 换爹：先从原父场景的名单里摘掉，否则那边会一直拿着一个「已经不属于它」
    // 的孩子去命中与冒泡。
    if (child._parentScene != nullptr) {
        InkingScene* previous = child._parentScene;
        auto& siblings = previous->_children;
        siblings.erase(std::remove(siblings.begin(), siblings.end(), &child), siblings.end());
        previous->MarkDirty();
    }

    child._parentScene = this;
    child.SetParent(this);  // 锚点层的父子关系（位置由锚点推，不靠这里的链表）
    _children.push_back(&child);

    // 结构变了：自己的表要重烘，命中与重绘都要标一次。
    MarkDirty();
    return child;
}

InkingScene* InkingScene::GetParentScene() const noexcept {
    return _parentScene;
}

const std::vector<InkingScene*>& InkingScene::GetChildren() const noexcept {
    return _children;
}

InkingScene& InkingScene::RootScene() noexcept {
    InkingScene* node = this;
    while (node->_parentScene != nullptr) {
        node = node->_parentScene;
    }
    return *node;
}

Rect InkingScene::GetRect() const noexcept {
    return Rect{GetAbsX(), GetAbsY(), static_cast<float>(GetWidth()),
                static_cast<float>(GetHeight())};
}

// ---------------------------------------------------------------------------
// [final] 写入口：可见性
// ---------------------------------------------------------------------------

bool InkingScene::GetVisible() const noexcept {
    return _visible;
}

bool InkingScene::SetVisible(bool visible) {
    if (_visible == visible) {
        return false;
    }
    _visible = visible;
    MarkDirty();  // 可见性是会标脏的四件事之一
    onVisibleChanged(visible);
    return true;
}

void InkingScene::onVisibleChanged(bool) {
    // 钩子只挂附加逻辑，标脏已经由写入口做完了。
}

// ---------------------------------------------------------------------------
// 命中策略 / 档位
// ---------------------------------------------------------------------------

HitPolicy InkingScene::GetHitPolicy() const noexcept {
    return _hitPolicy;
}

bool InkingScene::SetHitPolicy(HitPolicy policy) {
    if (_hitPolicy == policy) {
        return false;
    }
    _hitPolicy = policy;
    MarkDirty();
    return true;
}

bool InkingScene::IsDynamic() const noexcept {
    return _dynamic;
}

// ---------------------------------------------------------------------------
// 标脏：三条路各走各的
// ---------------------------------------------------------------------------

void InkingScene::onDirty() {
    // 写入口（Resize / 锚点 / 偏移 / 层级 / 可见性 / 挂孩子）内部标脏都会到这里。
    InvalidateHit();                              // ① 命中表：标到最近的表边界
    MakeDirty("几何、层级或可见性变化");            // ② 重绘：独立的一路
    // ③ 输入层要的「结果可能过期」在 InvalidateHit 里一并标到根（一个 bool）。
}

void InkingScene::MakeDirty(const std::string& reason) {
    RedrawScheduler::MakeDirty();
    INK_LOG_DEBUG(kModuleName, GetName() + " → makeDirty（" + reason + "）");
}

void InkingScene::InvalidateHit() {
    // 标脏只标到最近的表边界，不一路标到根、白重烘（docs/InputDesign.md §8）。
    InkingScene* node = this;
    for (;;) {
        node->_bakeDirty = true;
        node->_baked = false;

        if (node->_dynamic) {
            // 动态结点不进表：父级只把它记成一个「洞」，答案每帧现问自己，
            // 所以它怎么动都不该把父级的表拖去重烘。
            break;
        }
        InkingScene* parent = node->_parentScene;
        if (parent == nullptr) {
            break;  // 到根了：它自己就是表边界
        }
        if (parent->_dynamic) {
            break;  // 动态父级没有表：它每帧自己判
        }
        if (parent->_usesSlotIndex) {
            // 父级是等宽等距的表边界：我是它的直接槽位，重烘它就够，
            // 再往上那些祖先只记了父级一条「嵌套条目」，不用动。
            parent->_bakeDirty = true;
            parent->_baked = false;
            break;
        }
        node = parent;  // 我被平铺进了父表：继续往上
    }

    MarkTreeChanged();
}

void InkingScene::MarkTreeChanged() noexcept {
    InkingScene* node = this;
    while (node->_parentScene != nullptr) {
        node = node->_parentScene;
    }
    // 只标根上一个 bool：命中可能过期 ≠ 要重烘，输入层消费这个标记。
    node->_treeChanged = true;
}

// ---------------------------------------------------------------------------
// 烘焙：静态层压平成 索引 + 绘制表
// ---------------------------------------------------------------------------

void InkingScene::Bake() {
    _hitTable.Clear();
    _slotIndex.Clear();
    _holes.clear();
    _drawList.clear();
    _usesSlotIndex = false;
    ++_bakeCount;

    if (_dynamic) {
        // 动态结点没有表可烘：每帧自己判。
        _baked = false;
        _bakeDirty = false;
        return;
    }

    std::vector<StaticEntry> entries;
    // 本结点是这张表的兜底条目：点在空白处，答案就是它（docs/InputDesign.md §10.5）。
    entries.push_back(StaticEntry{this, GetRect(), GetZIndex(), GetRegisterOrder(), false});
    for (InkingScene* child : _children) {
        child->CollectSubtree(entries, _holes, _drawList);
    }

    // 表里那些「自带专用查找」的结点顺手一起烘：静态层一次烘完，之后只查。
    // （不这么做的话，父表烘好了、子表的索引还得等第一次查到这个结点才补。）
    for (const StaticEntry& entry : entries) {
        if (entry.nestedIndex && entry.node != this) {
            entry.node->Bake();
        }
    }

    // 绘制表按 z 升序：画得早的先，后面的盖上来。
    std::stable_sort(_drawList.begin(), _drawList.end(),
                     [](const DrawItem& lhs, const DrawItem& rhs) {
                         return !zAbove(lhs.node->GetZIndex(), lhs.node->GetRegisterOrder(),
                                        rhs.node->GetZIndex(), rhs.node->GetRegisterOrder());
                     });

    // 动态子树按 z 降序：查表时「先问上面的」。
    std::stable_sort(_holes.begin(), _holes.end(),
                     [](const DynamicHole& lhs, const DynamicHole& rhs) {
                         return zAbove(lhs.z, lhs.order, rhs.z, rhs.order);
                     });

    // 声明了等宽等距就试专用查找；槽位不满足等宽等距（或者不是叶子）就退回网格表。
    if (_slotLayout) {
        std::vector<StaticEntry> slots;
        bool usable = true;
        for (InkingScene* child : _children) {
            if (child == nullptr || !child->_visible || child->_dynamic) {
                continue;
            }
            if (!child->_children.empty()) {
                usable = false;  // 槽位必须是叶子：本原型不做「槽位里再套一层」
                break;
            }
            slots.push_back(StaticEntry{child, child->GetRect(), child->GetZIndex(),
                                        child->GetRegisterOrder(), false});
        }
        if (usable) {
            _usesSlotIndex = _slotIndex.Build(std::move(slots));
        }
        if (!_usesSlotIndex) {
            INK_LOG_WARN(kModuleName,
                         GetName() + " 声明了等宽等距但用不了专用查找，退回网格表");
        }
    }

    if (!_usesSlotIndex) {
        _hitTable.Build(std::move(entries));
    }

    _baked = true;
    _bakeDirty = false;

    INK_LOG_DEBUG(kModuleName,
                  "烘焙 " + GetName() + "："
                      + (_usesSlotIndex
                             ? ("等宽等距专用查找（算术、无表），" + std::to_string(_slotIndex.Size())
                                + " 个槽位，槽间距 " + std::to_string(static_cast<int>(_slotIndex.Gap())))
                             : ("网格表，" + std::to_string(_hitTable.Size()) + " 条条目、"
                                + std::to_string(_hitTable.CandidateCount()) + " 条候选"))
                      + "，动态洞 " + std::to_string(_holes.size()) + " 个");
}

bool InkingScene::NeedsBake() const noexcept {
    return !_dynamic && (_bakeDirty || !_baked);
}

bool InkingScene::IsBaked() const noexcept {
    return _baked;
}

bool InkingScene::IsTreeChanged() const noexcept {
    return _treeChanged;
}

void InkingScene::ClearTreeChanged() noexcept {
    _treeChanged = false;
}

std::uint64_t InkingScene::BakeCount() const noexcept {
    return _bakeCount;
}

void InkingScene::UseSlotLayout() {
    if (_slotLayout) {
        return;
    }
    _slotLayout = true;
    MarkDirty();
}

bool InkingScene::IsSlotLayout() const noexcept {
    return _slotLayout;
}

bool InkingScene::UsesSlotIndex() const noexcept {
    return _usesSlotIndex;
}

const HitTable& InkingScene::GetHitTable() const noexcept {
    return _hitTable;
}

const SlotIndex& InkingScene::GetSlotIndex() const noexcept {
    return _slotIndex;
}

const std::vector<DynamicHole>& InkingScene::GetDynamicHoles() const noexcept {
    return _holes;
}

// ---------------------------------------------------------------------------
// 静态层压平
// ---------------------------------------------------------------------------

void InkingScene::CollectSubtree(std::vector<StaticEntry>& entries,
                                 std::vector<DynamicHole>& holes,
                                 std::vector<DrawItem>& drawList) {
    if (!_visible) {
        return;  // 藏起来的子树既不画也进不了表
    }

    if (_dynamic) {
        // 动态子树不摊平：父表把它记成一个「洞」，每帧由它自己判（§10.5）。
        holes.push_back(DynamicHole{this, GetRect(), GetZIndex(), GetRegisterOrder()});
        drawList.push_back(DrawItem{this, true});
        return;
    }

    entries.push_back(
        StaticEntry{this, GetRect(), GetZIndex(), GetRegisterOrder(), _slotLayout});
    // 自带专用查找的结点有自己的平铺绘制表，所以它这一项要递归画；
    // 被平铺进父表的那些结点则由父表一项一项直接画，不递归。
    drawList.push_back(DrawItem{this, _slotLayout});

    if (_slotLayout) {
        return;  // 自带专用查找：父表里只记这一条，答案从它自己的索引来
    }
    for (InkingScene* child : _children) {
        if (child != nullptr) {
            child->CollectSubtree(entries, holes, drawList);
        }
    }
}

// ---------------------------------------------------------------------------
// 命中：三态 + 目标
// ---------------------------------------------------------------------------

HitTarget InkingScene::Query(float x, float y) {
    if (!_visible) {
        return HitTarget{};
    }
    if (_dynamic) {
        return QueryDynamic(x, y);
    }
    if (NeedsBake()) {
        Bake();  // 懒烘焙：谁先用谁掏这一次成本
    }

    HitTarget best = _usesSlotIndex ? _slotIndex.Query(x, y) : _hitTable.Query(x, y);

    // 静态查完，再把动态子树自己判的结果按 zindex 并进来。
    for (const DynamicHole& hole : _holes) {
        if (hole.node == nullptr) {
            continue;
        }
        if (best.target != nullptr
            && !zAbove(hole.z, hole.order, best.z, best.target->GetRegisterOrder())) {
            continue;  // 静态层已经更高，动态层不用问
        }
        HitTarget candidate = hole.node->Query(x, y);
        if (!candidate.Found()) {
            continue;
        }
        if (best.target == nullptr
            || zAbove(candidate.z, candidate.target->GetRegisterOrder(), best.z,
                      best.target->GetRegisterOrder())) {
            best = candidate;
        }
    }
    return best;
}

HitTarget InkingScene::QueryDynamic(float x, float y) {
    // 动态结点不进表，自己判自己的：祖先矩形先算，再看谁压在上面。
    HitTarget block;
    HitTarget passThrough;

    const Rect box = GetRect();
    if (box.valid() && box.contains(x, y)) {
        if (_hitPolicy == HitPolicy::Block) {
            block = HitTarget{HitState::Block, this, GetZIndex()};
        } else {
            passThrough = HitTarget{HitState::PassThrough, this, GetZIndex()};
        }
    }

    for (InkingScene* child : _children) {
        if (child == nullptr || !child->GetVisible()) {
            continue;
        }
        const HitTarget candidate = child->Query(x, y);
        if (!candidate.Found() || candidate.target == nullptr) {
            continue;
        }
        if (candidate.Blocked()) {
            if (block.target == nullptr
                || zAbove(candidate.z, candidate.target->GetRegisterOrder(), block.z,
                          block.target->GetRegisterOrder())) {
                block = candidate;
            }
        } else if (passThrough.target == nullptr
                   || zAbove(candidate.z, candidate.target->GetRegisterOrder(), passThrough.z,
                             passThrough.target->GetRegisterOrder())) {
            passThrough = candidate;
        }
    }

    return block.Found() ? block : passThrough;
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------

void InkingScene::Draw(Canvas& canvas) const {
    if (!_visible) {
        return;
    }

    onDraw(canvas);

    if (!_dynamic && _baked) {
        // 静态场景走平铺表：一次循环画完自己和所有静态子孙，不遍历树。
        for (const DrawItem& item : _drawList) {
            if (item.node == nullptr) {
                continue;
            }
            if (item.recursive) {
                item.node->Draw(canvas);  // 动态子树：递归画整棵
            } else {
                item.node->onDraw(canvas);  // 静态结点：平铺表里画一次
            }
        }
        return;
    }

    for (const InkingScene* child : _children) {
        if (child != nullptr) {
            child->Draw(canvas);
        }
    }
}

void InkingScene::onDraw(Canvas&) const {
    // 默认什么都不画：纯粹用来分组、接管点击的场景不需要外观。
}

// ---------------------------------------------------------------------------
// 每帧：只有动态子树收得到
// ---------------------------------------------------------------------------

void InkingScene::Tick(float deltaSeconds) {
    if (_dynamic) {
        onTick(deltaSeconds);
        for (InkingScene* child : _children) {
            if (child != nullptr) {
                child->Tick(deltaSeconds);
            }
        }
        return;
    }

    if (!_baked) {
        // 还没烘焙，无从判断哪些子孙是静的，老老实实走一遍。
        for (InkingScene* child : _children) {
            if (child != nullptr) {
                child->Tick(deltaSeconds);
            }
        }
        return;
    }

    // 烘过了：静态部分不会动，只有那些「洞」需要每帧机会。
    for (const DynamicHole& hole : _holes) {
        if (hole.node != nullptr) {
            hole.node->Tick(deltaSeconds);
        }
    }
}

void InkingScene::onTick(float) {
    // 默认不做事。静态场景收不到这个回调，动态场景才会被每帧叫醒。
}

// ---------------------------------------------------------------------------
// 输入：默认都不处理，交给父级
// ---------------------------------------------------------------------------

bool InkingScene::onPointerDown(PointerEvent&) {
    return false;
}

bool InkingScene::onPointerUp(PointerEvent&) {
    return false;
}

void InkingScene::onPointerEnter(PointerEvent&) {}

void InkingScene::onPointerLeave(PointerEvent&) {}

void InkingScene::onPointerCancel(PointerEvent&) {}

bool InkingScene::onClick(PointerEvent&) {
    return false;
}

}  // namespace ink
