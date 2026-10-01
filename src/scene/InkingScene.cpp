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
    // 场景不做布局变化，但第一次渲染总要有对齐的锚点：先按设计画布定型
    // （自身左上角对上窗口左上角）。将来由 CXXCSS 配置
    // （CXXCSS/Scene/<场景名>.json）驱动时，改的就是这里。
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
}

// ---------------------------------------------------------------------------
// 指针输入（今天的最小闭环）
// ---------------------------------------------------------------------------

void InkingScene::SetPointerState(float designX, float designY, bool down,
                                  bool inside) noexcept {
    _pointerX = designX;
    _pointerY = designY;
    _pointerDown = down;
    _pointerInside = inside;
}

InkingAnchor* InkingScene::GetPressedNode() const noexcept {
    return _pressedNode;
}

InkingAnchor* InkingScene::GetHoveredNode() const noexcept {
    return _hoveredNode;
}

InkingAnchor* InkingScene::hitTestTopmost(float designX,
                                          float designY) noexcept {
    // 从列表**尾部**往前问：列表按 z 升序排（先画的在底下），所以尾部是最上面。
    // 第一个命中的就是答案——这条顺序规则和渲染的提交顺序同源，
    // 不会出现"画的是 A、点到的是 B"。
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
    InkingAnchor* topmost =
        _pointerInside ? hitTestTopmost(_pointerX, _pointerY) : nullptr;

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
