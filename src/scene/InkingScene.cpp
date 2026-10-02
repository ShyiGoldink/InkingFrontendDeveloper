#include <scene/InkingScene.h>

#include <button/InkingStaticButton.h>
#include <config/window_config.h>
#include <ink/basic/InkLog.h>

#include <SDL3/SDL.h>

#include <algorithm>

namespace ink {

namespace {

constexpr const char* kModuleName = "InkingScene";

/**
 * 设计坐标 → 设备像素的换算。
 *
 * letterbox 是整幅等比缩放，倍率由窗口层每帧写进根锚点（不标脏）。
 * 这里只做绘制换算，不参与几何，命中表也不会因为缩放重烘。
 */
float magnify(const InkingAnchor& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

InkingScene::InkingScene() : InkingStaticAnchor(nullptr, sceneRootAnchorData()) {
    finishConstruction();
}

InkingScene::InkingScene(const std::string& sceneName)
    : InkingStaticAnchor(nullptr, sceneRootAnchorData()), _sceneName(sceneName) {
    finishConstruction();
}

InkingScene::InkingScene(const std::string& sceneName,
                         const AnchorData& anchorData)
    : InkingStaticAnchor(nullptr, anchorData), _sceneName(sceneName) {
    finishConstruction();
}

InkingScene::~InkingScene() {
    // 注销在令牌的析构里做，这里只说明顺序：本类析构体 → 成员（令牌、
    // 绘制列表）→ 基类 InkingAnchor。所以令牌一定在 _sceneName 之前销毁，
    // 而注销正是拿 _sceneName 当 key 的——key 全程有效。

    // 场景是渲染树的根，子树里的每个节点都记着"我属于这个场景"（_scene）。
    // 场景先走（比如两个场景互相独立、析构顺序不由场景决定）时，那些指针
    // 就成了野指针，而节点析构时还会拿着它去反注册——先摘干净再拆自己。
    DetachSceneFromNodes();
}

void InkingScene::finishConstruction() {
    // 盖章成渲染树的根：构造期不能靠父链查找认出场景——构造 InkingScene 时
    // 它的动态类型还是 InkingAnchor，那一趟查找认不出来（见头文件说明）。
    //
    // 注意场景**自己不进自己的绘制列表**：它是容器，不是要画的一笔。
    // "只留属于本场景的节点"这条过滤（见 sortDrawList）靠 _scene == this
    // 判定，而场景自己正是唯一一个 _scene == this 的根，所以它天然被排除。
    //
    // 这里不需要"补登记已有子树"：子节点要认这个场景当父级，场景就必须先
    // 存在，所以子节点只可能在场景之后构造，而那时沿父链登记已经生效。
    MarkAsSceneRoot(this);

    // 盖章之后回头补登记：在构造函数体里建出来的子节点（最典型的是按钮成员）
    // 那时还没有"场景"可认，沿父链找不到它。不补这一趟它们既不渲染也点不到，
    // 而且一声不响——画面上什么都没有，最难查。
    rebindEarlyChildren();

    registerToSceneLibrary();
}

void InkingScene::rebindEarlyChildren() {
    // 复制一份再遍历：补登记会往 _drawList 里 push，边遍历边改容器就是悬垂。
    std::vector<InkingAnchor*> early;
    early.reserve(_drawList.size());
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr && item.node->GetScene() == nullptr) {
            early.push_back(item.node);
        }
    }

    for (InkingAnchor* node : early) {
        node->RebindScene(this);
    }
}

// ---------------------------------------------------------------------------
// 登记
// ---------------------------------------------------------------------------

void InkingScene::registerToSceneLibrary() {
    // 构造即在令牌里完成登记，析构即在令牌里完成注销：
    // 调用方永远不会忘记写注销，这正是 RAII 的用意。
    //
    // 登记可能被**驳回**：同名场景已经占了库。驳回时令牌是空转的
    // （不会去注销别人的登记），本场景进入停放态——对象还在、数据还在，
    // 但永远不渲染也不 tick，直到占用者被关闭后重建。
    _sceneRegisterToken.emplace(_sceneName, this);

    switch (_sceneRegisterToken->GetResult()) {
        case SceneLibrary::RegisterResult::Ok:
            break;
        case SceneLibrary::RegisterResult::NameTaken:
            _rejected = true;
            INK_LOG_WARN(kModuleName,
                         "场景名已被占用，注册被驳回：" + _sceneName
                             + "（此场景不会渲染；要复用这个名字请先 "
                               "SceneLibrary::CloseScene）");
            break;
        case SceneLibrary::RegisterResult::Invalid:
            INK_LOG_DEBUG(kModuleName, "场景未登记（名字为空）");
            break;
    }
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const std::string& InkingScene::GetSceneName() const noexcept {
    return _sceneName;
}

bool InkingScene::IsRegistered() const noexcept {
    // "有令牌"不算数：令牌可能是被驳回后空转的那个（见 RegisterToken）。
    // 要问令牌自己真的进库了没有——这个 bug 以前被自检抓到过。
    return _sceneRegisterToken.has_value() && _sceneRegisterToken->IsRegistered();
}

bool InkingScene::IsRejected() const noexcept {
    return _rejected;
}

bool InkingScene::IsClosed() const noexcept {
    return _closed;
}

bool InkingScene::IsActive() const noexcept {
    return _active;
}

std::uint64_t InkingScene::GetStructureGeneration() const noexcept {
    return _structureGeneration;
}

// ---------------------------------------------------------------------------
// 每帧回调
// ---------------------------------------------------------------------------

void InkingScene::TickLogic(double fixedStepSeconds) {
    // 停放（注册被驳回）与已关闭的场景不跑任何逻辑：它们连"存在感"都没有。
    if (_rejected || _closed) {
        return;
    }

    // 场景自己的逻辑：固定步长，和渲染解耦。静默态也照跑——
    // 后台的事情不该因为切场景就停摆。
    onTick(fixedStepSeconds);

    // 动态组件才是"几何会自己变"的那一档，逐个喂固定步。
    // 静态组件压根没有 Tick 入口，所以这里不用问类型——
    // 分流在登记那一刻就在类型上了（docs/InputDesign.md §2）。
    //
    // 注意复制一份指针再遍历：组件的 Tick 里可能会增删节点
    // （构造 / 析构会改 _drawList），拿着引用边遍历边改就是悬垂。
    std::vector<InkingAnchor*> dynamicNodes;
    dynamicNodes.reserve(_drawList.size());
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr && item.node->IsDynamic()) {
            dynamicNodes.push_back(item.node);
        }
    }

    for (InkingAnchor* node : dynamicNodes) {
        static_cast<InkingDynamicAnchor*>(node)->Tick(
            static_cast<float>(fixedStepSeconds));
    }
}

void InkingScene::TickFrame(double deltaSeconds) {
    // 只有活跃（正在渲染）时才跑"跟着渲染的那组"。
    // 静默 / 停放 / 关闭都不跑——画面外的插值推进没有意义。
    if (_rejected || _closed || !_active) {
        return;
    }

    onRenderBoundTick(deltaSeconds);

    // 再把这一帧的真实 delta 分发给**所有**节点（静态也要）：
    // 颜色 / 透明度过渡不该因为"几何不变"就收不到帧——那正是静态档的代价边界。
    // 动态档的几何推进走的是另一条（`TickLogic` 的固定逻辑步），两者别混。
    //
    // 复制一份指针再遍历：回调里可能增删节点（构造 / 析构会改 _drawList），
    // 拿着引用边遍历边改就是悬垂——和 TickLogic 那边同一个理由。
    std::vector<InkingAnchor*> nodes;
    nodes.reserve(_drawList.size());
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr) {
            nodes.push_back(item.node);
        }
    }

    const float delta = static_cast<float>(deltaSeconds);
    for (InkingAnchor* node : nodes) {
        node->TickAnimation(delta);
    }
}

void InkingScene::onTick(double /*fixedStepSeconds*/) {}

void InkingScene::onRenderBoundTick(double /*deltaSeconds*/) {}

// ---------------------------------------------------------------------------
// 只给 SceneLibrary 调
// ---------------------------------------------------------------------------

void InkingScene::MarkClosed() noexcept {
    _closed = true;
    _active = false;

    // 登记已经由库那边摘掉了，把令牌丢掉：留着它的话，场景析构时会再去
    // 注销一次——虽然库那边有"指针不匹配就不动"的保护，但让状态说实话更好。
    _sceneRegisterToken.reset();
}

void InkingScene::SetActiveFromLibrary(bool active) noexcept {
    _active = active;
}

// ---------------------------------------------------------------------------
// 绘制列表的维护
// ---------------------------------------------------------------------------

AnchorData InkingScene::sceneRootAnchorData() {
    // 场景不做布局变化，但第一次渲染总要有对齐的锚点：按设计画布定型
    // （自身左上角对上窗口左上角）。
    //
    // 这里**不会**改成"由 CXXCSS/Scene/*.json 驱动"：场景不交给生成器
    // （评估结论见 CXXCSS/CXXCSS.md §7.2）。尺寸跟随窗口、锚点按设计画布，
    // 就是它该有的样子，没有可配的东西。
    AnchorData data;
    data.width  = inking::kDesignWidth;
    data.height = inking::kDesignHeight;
    return data;
}

void InkingScene::NotifyStructureChanged() noexcept {
    // 只标脏，不排序：一帧里连改十个 zindex 也就重建一次（见头文件说明）。
    _contentDirty = true;
    _orderDirty = true;
    ++_structureGeneration;
}

void InkingScene::ConsumeFrameDirty() noexcept {
    // 停放 / 关闭的场景不消费：它们的脏要留着，切回来时才有意义。
    if (_rejected || _closed) {
        return;
    }

    // 节点那一半：推进安静帧计数（够数就清脏）。直接在绘制列表上走一遍——
    // 每节点一次 bool 读，几十个节点可以忽略；换"维护一张脏节点表"反而要处理
    // 去重（同一节点一帧可能标脏很多次），得不偿失。
    _repaintingNodeCount = 0;
    _hitDirtyNodeCount = 0;
    for (const DrawItem& item : _drawList) {
        InkingAnchor* node = item.node;
        if (node == nullptr) {
            continue;
        }
        node->ConsumeDirtyFrame();
        // 统计的是"消费之后**仍**处于脏状态"的节点数，也就是"画面还在动"。
        if (node->IsRepaintDirty()) {
            ++_repaintingNodeCount;
        }
        if (node->IsDirty()) {
            ++_hitDirtyNodeCount;
        }
    }

    // 指针那一半：这一帧的悬停判断已经做过了（T-4 起由 query 消费）。
    _pointerDirty = false;
}

int InkingScene::GetRepaintingNodeCount() const noexcept {
    return _repaintingNodeCount;
}

int InkingScene::GetHitDirtyNodeCount() const noexcept {
    return _hitDirtyNodeCount;
}

bool InkingScene::IsPointerDirty() const noexcept {
    return _pointerDirty;
}

void InkingScene::AddDrawNode(InkingAnchor* node) noexcept {
    if (node == nullptr) {
        return;
    }

    // 先查重：同一个指针登记两次会让列表里出现两个同样的节点，
    // 于是同一帧画两遍，去掉一份时又只去掉一份。
    const auto it = std::find_if(_drawList.begin(), _drawList.end(),
                                 [node](const DrawItem& item) {
                                     return item.node == node;
                                 });
    if (it != _drawList.end()) {
        return;
    }

    DrawItem item;
    item.node = node;
    item.parent = node->GetParent();
    item.absX = node->GetAbsX();  // 静态节点算一次就冻住
    item.absY = node->GetAbsY();
    _drawList.push_back(item);

    NotifyStructureChanged();
}

void InkingScene::RemoveDrawNode(InkingAnchor* node) noexcept {
    if (node == nullptr) {
        return;
    }

    // 指针状态里可能正指着这个节点（悬停 / 按下）。这里必须一起清掉：
    // 节点析构之后再有人带着这个指针派发事件，就是野指针（AGENTS §6 第 18 条
    // 那条"生命周期成对处理"的同一条纪律）。
    if (_hoveredNode == node) {
        _hoveredNode = nullptr;
    }
    if (_pressedNode == node) {
        _pressedNode = nullptr;
    }

    // 只比指针、不解引用：调用方（节点析构）可能已经在销毁中途了。
    const auto it = std::remove_if(_drawList.begin(), _drawList.end(),
                                   [node](const DrawItem& item) {
                                       return item.node == node;
                                   });
    if (it == _drawList.end()) {
        return;  // 没在列表里，不用标脏
    }
    _drawList.erase(it, _drawList.end());
    NotifyStructureChanged();
}

void InkingScene::DetachSceneFromNodes() noexcept {
    // 只把节点的"我属于这个场景"摘掉，不动 _drawList：列表马上随场景一起销毁，
    // 而且里面的裸指针有可能是正在析构的兄弟节点，只能比指针、不能解引用。
    for (const DrawItem& item : _drawList) {
        if (item.node != nullptr && item.node->GetScene() == this) {
            item.node->DetachFromScene();
        }
    }
    _drawList.clear();
}

const std::vector<InkingScene::DrawItem>& InkingScene::BuildDrawList() noexcept {
    sortDrawList();
    return _drawList;
}

std::size_t InkingScene::GetDrawItemCount() noexcept {
    sortDrawList();
    return _drawList.size();
}

void InkingScene::sortDrawList() {
    if (!_contentDirty && !_orderDirty) {
        return;
    }

    if (_contentDirty) {
        // 内容重建只剔除两类**真的不该在列表里**的条目：
        // (1) 已经不属于本场景的（换父级跨了场景）；
        // (2) 空指针。
        //
        // **不按可见性剔除**：可见性是可逆的，而"可见"这条信息只有节点自己
        // 知道。列表一旦把隐藏的节点丢掉，就再也没人把它加回来——构造函数
        // 只跑一次，于是"隐藏过的节点永远回不来"。可见性留给提交时判断
        // （见 Render）：那里 IsVisibleInTree 会顺带把整棵隐藏子树短路掉。
        const auto stale = std::remove_if(
            _drawList.begin(), _drawList.end(), [this](const DrawItem& item) {
                return item.node == nullptr || item.node->GetScene() != this;
            });
        _drawList.erase(stale, _drawList.end());

        // 重算绝对坐标：列表里的快照可能是构造那一刻的，而父级之后挪过地方。
        // 动态节点每帧提交时现算，这里的值只当基准。
        for (DrawItem& item : _drawList) {
            if (item.node != nullptr) {
                item.parent = item.node->GetParent();
                item.absX = item.node->GetAbsX();
                item.absY = item.node->GetAbsY();
            }
        }
    }

    // 稳定排序，**按 z 从小到大**：z 小的先画（在底下），z 大的后画（盖在上面）。
    //
    // 这里不能直接用 InkingAnchor::IsAbove——那个回答的是"谁在上面"，
    // 是命中要问的问题（从高到低找第一个挡住点的）。绘制的提交顺序恰好相反，
    // 所以这里自己写比较器，但**用的规则和 IsAbove 完全一致**（z 优先，
    // z 相同看注册序号），只是反过来排。两边规则必须一致，否则会出现
    // "画的是 A、点到的是 B"。
    std::stable_sort(_drawList.begin(), _drawList.end(),
                     [](const DrawItem& a, const DrawItem& b) {
                         return InkingAnchor::IsAbove(*b.node, *a.node);
                     });

    _contentDirty = false;
    _orderDirty = false;

    // 表与列表**同生同灭**：内容或顺序一变，候选项的 z 序与包络就都过期了。
    // 时机也正合设计——这里是"帧首重建一次"的落点，而 Render / BuildDrawList /
    // QueryHit 都会先推一次 sortDrawList，所以谁先来拿到的都是最新的表。
    //
    // **变换不在这里**：组件上报的是"所有可能变换"的保守包络（它不动），
    // 精判用当前变换。所以动画期间表一次都不用重建（§11「变换通道不改表」）。
    rebuildHitTable();
}

// ---------------------------------------------------------------------------
// 指针输入（今天的最小闭环）
// ---------------------------------------------------------------------------

void InkingScene::SetPointerState(float designX, float designY, bool down,
                                  bool inside) noexcept {
    // 只有**真的变了**才算指针脏：窗口层每帧都喂一次（哪怕鼠标没动），
    // 不判一下的话"每帧都脏"就等于这个标记永远是摆设。
    const bool moved = (designX != _pointerX) || (designY != _pointerY)
        || (down != _pointerDown) || (inside != _pointerInside);

    _pointerX = designX;
    _pointerY = designY;
    _pointerDown = down;
    _pointerInside = inside;

    if (moved) {
        _pointerDirty = true;
    }
}

InkingAnchor* InkingScene::GetPressedNode() const noexcept {
    return _pressedNode;
}

float InkingScene::GetPointerX() const noexcept {
    return _pointerX;
}

float InkingScene::GetPointerY() const noexcept {
    return _pointerY;
}

bool InkingScene::IsPointerDown() const noexcept {
    return _pointerDown;
}

bool InkingScene::IsPointerInside() const noexcept {
    return _pointerInside;
}

InkingAnchor* InkingScene::GetHoveredNode() const noexcept {
    return _hoveredNode;
}

InkingAnchor* InkingScene::hitTestTopmost(float designX,
                                          float designY) noexcept {
    // 从列表**尾部**往前问：列表按 z 升序排（先画的在底下），所以尾部是最上面。
    // 第一个命中的就是答案——这条顺序规则和渲染的提交顺序同源，
    // 不会出现"画的是 A、点到的是 B"。
    //
    // **这是参考实现**：正式路径走烘焙表（`QueryHit`）。留它是因为自检要拿
    // "逐点对账"钉住两者同答案——一条规则两种实现，只有在它们互相印证时
    // 才敢说"换实现没换语义"。
    for (std::size_t i = _drawList.size(); i > 0; --i) {
        InkingAnchor* node = _drawList[i - 1].node;
        if (node == nullptr) {
            continue;
        }
        // 不可见的节点不参与命中：隐藏的组件不该还能被点到。父级隐藏时
        // IsVisibleInTree 会顺带把整棵子树短路掉。
        if (!node->IsVisibleInTree()) {
            continue;
        }
        if (node->HitTest(designX, designY)) {
            return node;
        }
    }
    return nullptr;
}

void InkingScene::rebuildHitTable() {
    _hitTable.Clear();

    for (const DrawItem& item : _drawList) {
        InkingAnchor* node = item.node;
        if (node == nullptr) {
            continue;
        }

        // 隐藏的组件：**既不进表、也不留洞**——渲染那边也是提交时跳过它，
        // 两边必须同一个答案（"隐藏的还能点到"是最容易出的那种不一致）。
        if (!node->IsVisibleInTree()) {
            continue;
        }

        // 包络是组件按"自己的形状 + 所有可能的变换"上报的（本地坐标），
        // 这里补上绝对位置。静态节点用列表里的快照，动态节点现算
        // （它每帧都在动，快照已经过期）。
        ShapeBounds envelope = node->GetHitEnvelope();
        if (node->IsDynamic()) {
            envelope.x += node->GetAbsX();
            envelope.y += node->GetAbsY();
        } else {
            envelope.x += item.absX;
            envelope.y += item.absY;
        }

        if (node->IsDynamic()) {
            // 动态组件**不进表**（它自己判命中、自己维护状态，§7），
            // 但它占的地方要记成"洞"：点不在任何洞里，就不必再去问动态层
            // （§10.5 规则 3）。洞只是性能开关——正确性由 QueryHit 里的
            // z 序合并保证，所以洞少登一个也只是多问一次，不会点错。
            _hitTable.AddHole(envelope);
            continue;
        }

        _hitTable.AddEntry(node, envelope, node->IsHitPassThrough());
    }

    _hitTable.Build();
}

InkingAnchor* InkingScene::topmostDynamicHit(float designX, float designY,
                                             HitKind& kind) noexcept {
    // 从列表尾部往前（z 由高到低），和渲染顺序同源。动态组件通常只有几个，
    // 而且只有"点落在洞里"时才会走到这里。
    InkingAnchor* passTarget = nullptr;
    for (std::size_t i = _drawList.size(); i > 0; --i) {
        InkingAnchor* node = _drawList[i - 1].node;
        if (node == nullptr || !node->IsDynamic()) {
            continue;
        }
        if (!node->IsVisibleInTree()) {
            continue;
        }
        if (!node->HitTest(designX, designY)) {
            continue;
        }
        if (node->IsHitPassThrough()) {
            // 让过：记下最上面那个，继续往下找 Block（和静态层同一套三态规则）。
            if (passTarget == nullptr) {
                passTarget = node;
            }
            continue;
        }
        kind = HitKind::Block;
        return node;
    }

    if (passTarget != nullptr) {
        kind = HitKind::PassThrough;
        return passTarget;
    }
    kind = HitKind::Miss;
    return nullptr;
}

HitResult InkingScene::QueryHit(float designX, float designY) noexcept {
    // 用之前保证表是新的。表跟着绘制列表一起重建（`sortDrawList` 里），
    // 所以这里只推一次列表的脏。
    sortDrawList();

    bool inHole = false;
    const HitResult staticResult = _hitTable.Query(designX, designY, &inHole);

    // 点不在任何动态洞里 → 静态表的答案就是最终答案，动态层一次都不用问。
    // 这就是"洞"的全部价值（§13：`isDirty == false` 时每帧 0 次 query 的加强版——
    // 哪怕在查，也常常是一次表查询就结束）。
    if (!inHole) {
        return staticResult;
    }

    HitKind dynamicKind = HitKind::Miss;
    InkingAnchor* dynamicHit = topmostDynamicHit(designX, designY, dynamicKind);
    if (dynamicHit == nullptr) {
        return staticResult;
    }

    // 静态与动态**按全局 z 序合并**（§6）。动态组件压在静态组件上面时，
    // 静态表仍会算出它自己那一份答案，所以这一步不能省。
    if (staticResult.target == nullptr
        || InkingAnchor::IsAbove(*dynamicHit, *staticResult.target)) {
        return HitResult{dynamicKind, dynamicHit};
    }
    return staticResult;
}

std::size_t InkingScene::GetHitTableEntryCount() noexcept {
    // 和 BuildDrawList 一样先推一次脏：表的生命周期挂在列表上，
    // 不推的话"刚建的节点还没进表"——调用方看到 0 会以为表坏了。
    sortDrawList();
    return _hitTable.GetEntryCount();
}

std::size_t InkingScene::GetHitTableClusterCount() noexcept {
    sortDrawList();
    return _hitTable.GetClusterCount();
}

std::size_t InkingScene::GetHitTableCellCount() noexcept {
    sortDrawList();
    return _hitTable.GetCellCount();
}

std::size_t InkingScene::GetHitTableMaxCellCandidates() noexcept {
    sortDrawList();
    return _hitTable.GetMaxCellCandidates();
}

void InkingScene::DispatchPointer() {
    // 停放 / 关闭的场景什么都不派发：它们连渲染都不参与。
    if (_rejected || _closed) {
        return;
    }

    // 场景的骨架可能刚变过（增删、换 z），命中要按最新的顺序问。
    sortDrawList();

    // 现在只有按钮会命中：别的静态组件 HitTest 一律 false，动态组件将来
    // 自己判命中（docs/InputDesign.md §7），那是另一条路。
    const auto asButton = [](InkingAnchor* node) {
        if (node == nullptr || node->IsDynamic()) {
            return static_cast<InkingStaticButton*>(nullptr);
        }
        return static_cast<InkingStaticButton*>(node);
    };

    // ---- 1. 悬停 ----
    // 命中走**烘焙好的表**（T-4）：静态层查表，动态层自判，
    // 两者按全局 z 序合并（`QueryHit` 里做的事）。
    // 这一步的**语义与之前完全一样**——换的是实现，不是规则。
    const HitResult hit =
        _pointerInside ? QueryHit(_pointerX, _pointerY) : HitResult{};
    InkingAnchor* topmost = hit.IsHit() ? hit.target : nullptr;

    // 先让"不再被悬停的"退出。要处理两个：
    //   - 上一帧悬停、这一帧不在它身上了；
    //   - 按着的那个（拖拽时指针滑出按钮，它得知道自己在外面了，
    //     抬起时才能回到 Normal 而不是 Hover）。
    if (_hoveredNode != nullptr && _hoveredNode != topmost) {
        if (InkingStaticButton* left = asButton(_hoveredNode)) {
            left->MouseHover(false);
        }
    }
    if (_pressedNode != nullptr && _pressedNode != topmost
        && _pressedNode != _hoveredNode) {
        if (InkingStaticButton* pressed = asButton(_pressedNode)) {
            pressed->MouseHover(false);
        }
    }

    // 再让新进来的进入。MouseHover 是幂等的，重复同一状态什么都不做。
    if (topmost != nullptr && _hoveredNode != topmost) {
        if (InkingStaticButton* entered = asButton(topmost)) {
            entered->MouseHover(true);
        }
    }
    _hoveredNode = topmost;

    // ---- 2. 按下 / 抬起 ----
    const bool justPressed = _pointerDown && !_pointerWasDown;
    const bool justReleased = !_pointerDown && _pointerWasDown;
    _pointerWasDown = _pointerDown;

    if (justPressed) {
        // 指针在哪按下，谁就抓住这次交互（之后移出去也算它按着）。
        _pressedNode = topmost;
        if (InkingStaticButton* pressed = asButton(_pressedNode)) {
            pressed->MousePress(true);
        }
    }

    if (justReleased) {
        InkingStaticButton* pressed = asButton(_pressedNode);
        // 只有"抬起时指针还在它身上"才算点击：按下去再滑开松开是取消，
        // 这是用户唯一能表达"我反悔了"的方式。
        const bool stillOnIt = (_pressedNode != nullptr) && (_pressedNode == topmost)
                            && _pointerInside;
        _pressedNode = nullptr;
        if (pressed != nullptr) {
            pressed->MouseRelease();
            if (stillOnIt) {
                pressed->TriggerClick();
            }
        }
    }
}

// ---------------------------------------------------------------------------
// 渲染
// ---------------------------------------------------------------------------

void InkingScene::onRenderScene(SDL_Renderer* /*renderer*/) {
    // 基类不画：场景是容器不是可见组件。派生场景要画自己的背景时重写这里。
}

void InkingScene::Render(SDL_Renderer* renderer) {
    if (renderer == nullptr) {
        return;
    }

    // 顺序重建推迟到这一步，之后本帧就是最新的。
    sortDrawList();

    for (const DrawItem& item : _drawList) {
        InkingAnchor* node = item.node;
        if (node == nullptr) {
            continue;
        }

        // 可见性在提交时才判断：隐藏的节点留在列表里（这样"再显示出来"
        // 不需要谁把它加回去），但不提交给渲染器。
        // 不可见的父级会把自己和整棵子树一起短路掉。
        if (!node->IsVisibleInTree()) {
            continue;
        }

        // 绝对坐标：静态组件几何构造即定型，用建表时算好的快照；
        // 动态组件每帧都在动，必须现算，否则会"画在这儿、点在别处"。
        float absX = item.absX;
        float absY = item.absY;
        if (node->IsDynamic()) {
            absX = node->GetAbsX();
            absY = node->GetAbsY();
        }

        node->SubmitToRenderer(renderer, absX, absY);
    }

    onRenderScene(renderer);
}

}  // namespace ink
