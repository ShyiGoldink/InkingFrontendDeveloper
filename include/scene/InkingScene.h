//InkingScene是场景的基类
//类似于QWidget，用户创建场景需要通过继承的方式来具体实例化内部的场景组件
//由于InkingScene是静态的，所以InkingScene本身的大小不能自己改变，只能跟随Window的大小改变
//想使用能随意改变大小的组件请参考InkingDynamicWidget

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "ink/basic/InkingAnchor.h"
#include "ink/ui/HitTable.h"
#include "scene/SceneLibrary.h"
#include "scene/SceneRegisterToken.h"

// 只做前向声明：公开头里不拖 SDL 进来（见 docs/AGENTS.md §6 第 8 条）。
struct SDL_Renderer;

namespace ink {

/**
 * @brief 场景：window 之下最高的结点，继承**静态锚点**。
 *
 * 场景永远静态：没有位置与尺寸写入口，只负责"渲染 / 不渲染"，
 * 会动的东西压在 widget 层（见 docs/API.md「InkingScene」）。
 *
 * ---------------------------------------------------------------------------
 * 登记：同名同期只能有一个
 *
 * 构造时把自己登记进 SceneLibrary。**名字被占了就当场驳回**——对象照样构造
 * 出来了，但它没进库，所以既不会渲染也不会被 tick，是一块"停放"的荒地；
 * 想用这个名字，得先把占用者 `SceneLibrary::CloseScene(名字)` 关掉。
 *
 * 被驳回的场景 `IsRegistered()` 为 false。库不拥有场景对象，关掉也不析构它，
 * 生命周期始终归调用方——这样"从库里拿指针 → 关掉 → 再自己决定何时析构"
 * 就不会踩野指针。
 *
 * ---------------------------------------------------------------------------
 * 三组逻辑
 *
 * 场景和组件的回调分成两组，由场景按当前状态分别开关；再算上渲染本身，
 * 就是"三组"：
 *
 *   | 回调                | 谁驱动 / 什么时候跑                          |
 *   | ------------------- | -------------------------------------------- |
 *   | onRenderBoundTick   | 每个**渲染帧**一次，且只在**活跃**时跑       |
 *   | onTick              | 时间线的**固定逻辑步**（默认 50Hz），一直跑  |
 *
 * 于是三种状态就是这两组的组合：
 *
 *   - **活跃**：渲染 + 两组逻辑都跑；
 *   - **静默**（被别的场景顶掉）：不渲染，跟着渲染的那组停；
 *     固定逻辑步照跑——后台的事情不该因为切场景就停摆。数据能读，
 *     所以可以先把数据拿完再决定去留；
 *   - **停放**：注册被驳回的场景，什么都没开始，永远不会跑。
 *
 * "需要不渲染但逻辑动"的场景，用 onTick 就行——它不依赖活跃态；
 * 反过来"不渲染就彻底别动"，把逻辑放 onRenderBoundTick。
 * 更细的频率（例如 24fps 的动画）不走这两个钩子，去订阅 `ink::Timeline`
 * （include/ink/basic/Timeline.h），那里按需采样。
 *
 * ---------------------------------------------------------------------------
 * 渲染树（骨架 + 扁平绘制列表）
 *
 * 层级（`_parent`）是**唯一真相源**，场景持有的绘制列表只是它的派生索引：
 * 子节点在构造时沿父链找到自己所属的场景并登记进来，场景这边只是一份
 * 按 z 序排好的扁平数组。
 *
 * 为什么扁平而不是递归遍历：
 *   - 绝对坐标必须走父链，而 `GetAbsX()` 是 O(深度)。扁平列表里父指针是
 *     现成的，取绝对坐标不用额外结构；
 *   - 命中侧（docs/InputDesign.md §10.2）已经定了"压平成 HitTable + 平铺
 *     _drawList"，渲染跟着扁平，两边遍历才不会越走越远；
 *   - 线性遍历对缓存友好，也没有深递归的栈风险。
 * 「整块在动」那档（抽屉 / Toast）将来要的是"子树自带子表 + 变换通道"，
 * 那时再加子表，不走这条默认路径。
 *
 * 顺序的维护方式是**标脏 + 帧末惰性重建**：
 *   - 骨架变化（构造 / 析构 / SetParent / ChangeZIndex / SetVisible / 动态几何写入）
 *     只把场景标脏，不立刻排序。一帧里连改十个 zindex 也就重建一次；
 *   - 重建推迟到 `Render()` 开头做，重建完就是最新的；
 *   - 提交顺序是 **z 从小到大**（先画底下的），和 `InkingAnchor::IsAbove`
 *     的答案相反但规则同源（z 优先、z 相同看注册序号）。两边规则必须一致，
 *     否则会出现"画的是 A、点到的是 B"。
 * ---------------------------------------------------------------------------
 */
class InkingScene : public InkingStaticAnchor {
public:
    /**
     * 场景的 RAII 登记令牌：绑定"场景类 → SceneLibrary"。
     * 类型别名让派生类不用关心模板参数。
     */
    using SceneToken = ink::RegisterToken<InkingScene, SceneLibrary>;

    /**
     * 绘制列表里的一项：节点本身 + 提交时算绝对坐标用的父指针。
     *
     * `absX` / `absY` 是静态节点的绝对左上角，**建表时算一次就冻住**
     * （静态组件的几何构造即定型）。动态节点存在这里的是建表那一刻的
     * 快照，每帧提交时会重新算——所以别拿它当动态节点的当前坐标用。
     *
     * 把这一项暴露出去、让窗口层自己遍历提交，是**临时的**：
     * 等合批（DrawCall 合批 + 静态烘焙）落地，提交要收进框架内部统一做，
     * 那时这个类型和 `BuildDrawList()` 都会收回去。
     */
    struct DrawItem {
        InkingAnchor* node = nullptr;
        InkingAnchor* parent = nullptr;
        float absX = 0.0f;
        float absY = 0.0f;
    };

    /** 运行时构造：不绑定场景名，不登记进场景库。 */
    InkingScene();

    /**
     * 配置驱动：传入 sceneName 作为场景名，并登记进场景库。
     *
     * 推荐用 k 常量命名，编译期就能排查问题（见 docs/API.md）。
     * 场景的尺寸与锚点在构造时定下，之后不挪动。
     * 名字被占用时注册被驳回，场景进入停放态（见类注释）。
     */
    explicit InkingScene(const std::string& sceneName);

    /**
     * InkingScene的另一种构造方式
     * 如果不想要用CXXCSS进行初始化，可以选择直接传入场景必须要的参数进行初始化
     * 一般用在小场景的情况下
     * 但是考虑到一般使用场景，Scene一般都需要和window的大小对齐，否则可能会有奇怪的黑边
     * 但是依然要提供这样的一个方法
     */
    explicit InkingScene(const std::string& sceneName, const AnchorData& anchorData);

    virtual ~InkingScene();

    // 禁止拷贝构造和赋值操作：场景持有父指针、还要登记进场景库，
    // 复制出的第二份关系会让父子链和场景库同时指向同一个对象。
    InkingScene(const InkingScene&) = delete;
    InkingScene& operator=(const InkingScene&) = delete;
    InkingScene(InkingScene&&) = delete;
    InkingScene& operator=(InkingScene&&) = delete;

    /** 场景名；构造时定下，没有写入口。 */
    const std::string& GetSceneName() const noexcept;

    /** 这个场景当前是否登记在场景库里（令牌还在自己手上且没被驳回）。 */
    bool IsRegistered() const noexcept;

    /** 注册被驳回了吗（后来者、名字被占）。停放态就是它。 */
    bool IsRejected() const noexcept;

    /** 被库关掉过吗（SceneLibrary::CloseScene）。关掉之后不能再注册。 */
    bool IsClosed() const noexcept;

    /**
     * 这个场景是不是"正在渲染"的那个。
     *
     * 由 SceneLibrary::SetActiveScene 维护：同一时刻最多一个为 true。
     * 切场景时旧的自动变 false（静默）。
     */
    bool IsActive() const noexcept;

    // ---------------- 每帧回调（由时间线 / 框架驱动） ----------------

    /**
     * **固定逻辑步**：由时间线按固定步长派发（默认 50Hz）。
     *
     * 内部做两件事：
     *   1. `onTick(fixedStep)`——场景自己的逻辑；
     *   2. 遍历绘制列表里的**动态组件**，逐个 `Tick(fixedStep)`——
     *      它们才是"几何会自己变"的那一档。静态组件的 `Tick` 压根不存在，
     *      所以这里不用问类型（登记时分流已经在类型上了）。
     *
     * 步长是**固定的**，不是真实 delta：状态机要的就是确定的步。
     * 被驳回 / 已关闭的场景不跑。
     */
    void TickLogic(double fixedStepSeconds);

    /**
     * **每帧一次**：由时间线在每个渲染帧派发，传真实 delta。
     *
     * 内部只在**活跃**（正在渲染）时调 `onRenderBoundTick(delta)`——
     * 插值、和画面同步的东西放这里。静默 / 停放 / 关闭都不跑。
     */
    void TickFrame(double deltaSeconds);

    // ---------------- 绘制列表（框架内部与自检用） ----------------

    /**
     * 把绘制顺序重建到最新，返回只读的扁平列表。
     *
     * 名字用 Get 是为了说明"它不会改你的东西"：没脏时直接返回现成的，
     * 脏了才重建一次。
     */
    const std::vector<DrawItem>& BuildDrawList() noexcept;

    /** 当前绘制列表里有多少项（自检与预算估算用）。 */
    std::size_t GetDrawItemCount() noexcept;

    /** 标脏：骨架变了，绘制顺序需要重建。写入口内部已经调过。 */
    void NotifyStructureChanged() noexcept;

    // ---------------- 标脏的统一消费（T-3） ----------------
    //
    // 三种脏的三个消费点，各管一段，别混：
    //
    //   1. **命中脏**（几何 / 可见性 / 层级 / 父子）→ 帧首。`sortDrawList()` 在
    //      渲染之前把绘制列表重建好（将来命中表落地后，重烘表也在这里）。
    //   2. **指针脏**             → 帧末（下面这个函数清）。指针一动"谁被指着"就可能变，
    //      但**表不用重烘**——这是它必须和命中脏分开的原因，见 `SetPointerState`。
    //   3. **重绘脏**（颜色 / 文字 / 变换）→ 帧末（下面这个函数记账 + 计时清零）。
    //
    // 为什么是"帧末统一消费"而不是"谁改谁立刻处理"：一帧里可能连着 Resize 三次、
    // ChangeZIndex 两次，立刻处理就是重烘五次表；攒到帧末只处理一次
    // （`docs/InputDesign.md` §5 的 isDirty 生命周期、TaskGuide T-4 那句"同一帧里
    // 最后统一消费"）。

    /**
     * 帧末统一消费：由窗口层在主循环的收尾那一步调一次（**只对活跃场景**）。
     *
     * 它做两件事：推进每个节点的安静帧计数（够数就清脏），然后清掉指针脏。
     * 它**不重建绘制列表**——那是帧首 `sortDrawList()` 的事；消费的语义是
     * "这一帧的处理已经完成了，把账记上"，不是"立刻去做重活"。
     *
     * 静默 / 停放 / 关闭的场景不消费：它们的脏标记要留着，切回来时才有意义。
     */
    void ConsumeFrameDirty() noexcept;

    /** 这一帧（上一次消费时）还有多少节点处于**重绘脏**——也就是"画面还在动"。 */
    int GetRepaintingNodeCount() const noexcept;

    /** 这一帧还有多少节点处于**命中脏**（要重算悬停 / 将来要重烘表）。 */
    int GetHitDirtyNodeCount() const noexcept;

    /**
     * 指针脏：指针状态变过、这一帧的悬停判断还没做。
     *
     * 它**不代表命中表要重烘**——指针移动不改任何几何。把两者混在一起，
     * 就会出现"鼠标一动就重烘整张表"（`docs/InputDesign.md` §5 把 isDirty
     * 单列出来正是为了避开这个）。
     */
    bool IsPointerDirty() const noexcept;

    // ---------------- 命中（T-4：烘焙表 + 三态） ----------------
    //
    // 形态照 `docs/InputDesign.md` §6 / §10：静态层查**烘焙好的表**（簇 + 格子 +
    // CSR 候选），动态层自判（它不进表），两者按**全局 z 序**合并成三态。
    //
    // 两件事和设计稿不同，都是被现状决定的（写在这里免得下一个人以为漏了）：
    //
    //   - **没有溢出桶**（§10.5 规则 2）：那条规则是给"运行时新增的 UI 不在
    //     生成期结构里"准备的。我们的组件**构造即登记进绘制列表**，而表是
    //     跟着列表重建的——所以"新增"表现为"表脏了"，不是"表外还挂着条目"。
    //   - **没有跨簇条目仲裁**（§10.5 规则 4）：那条规则是给**局部层级**
    //     （堆叠上下文）准备的。我们的 z 序是全局一条（`InkingAnchor::IsAbove`，
    //     §1 定的"规则只能有一条"），所以查表按全局 z 降序取第一个命中的就行。
    //     索引层面一个条目会登记进多个簇（大面板横跨粗块），那只是为了查得到，
    //     不涉及"谁在上面"的仲裁。**等真做堆叠上下文时，这两条都要补回来。**

    /**
     * 查一次命中：命中的最上面那个 + 三态。
     *
     * 坐标是**设计坐标**。悬停与点击共用它（§6）；点击不过脏标记，单独调一次。
     *
     * 返回的是**静态层与动态层合并之后**的答案：点在动态组件的洞里时，
     * 会拿动态层最上面那个和静态结果比 z 序。
     */
    HitResult QueryHit(float designX, float designY) noexcept;

    /** 命中表里有多少条静态条目 / 多少个簇 / 多少个格子（自检与预算估算用）。 */
    std::size_t GetHitTableEntryCount() noexcept;
    std::size_t GetHitTableClusterCount() noexcept;
    std::size_t GetHitTableCellCount() noexcept;

    /** 一次查询最多扫过多少候选（上界；用来判断格子尺寸定得是不是太粗）。 */
    std::size_t GetHitTableMaxCellCandidates() noexcept;

    // ---------------- 骨架登记的写入口 ----------------
    //
    // 只由 InkingAnchor 的构造 / 析构 / SetParent 调用，别在业务代码里手工调。

    /** 把一个节点加进绘制列表（内容脏）。 */
    void AddDrawNode(InkingAnchor* node) noexcept;

    /** 把一个节点从绘制列表里摘掉（内容脏）。 */
    void RemoveDrawNode(InkingAnchor* node) noexcept;

    /** 场景析构前把子树里指向自己的 _scene 摘成 nullptr，避免野指针。 */
    void DetachSceneFromNodes() noexcept;

    /** 骨架结构变了多少次（构造 / 析构 / 换父级 / 换场景）。自检用得上。 */
    std::uint64_t GetStructureGeneration() const noexcept;

    // ---------------- 指针输入（今天的最小闭环） ----------------

    /**
     * 把这一帧的指针状态喂进来。由窗口层在主循环里调。
     *
     * 坐标是**设计坐标**（窗口层已经用渲染器换算过，含 letterbox 偏移）。
     * 这里只存不判——真正的派发在 `DispatchPointer()`，那是"每帧只判断一次"
     * 那条设计（docs/InputDesign.md §1）的落点。
     *
     * @param inside 指针在不在窗口里。移出窗口时不该继续悬停/按下。
     */
    void SetPointerState(float designX, float designY, bool down,
                         bool inside) noexcept;

    // ---------------- 指针状态的只读查询（给动态组件"自己拉"用） ----------------
    //
    // 静态组件的输入是**推**过去的（DispatchPointer 扫表之后调 MouseHover /
    // MousePress），动态组件的输入是**拉**的：它不进表、也不被遍历，所以
    // 每帧在自己的 Tick 里读这里的四个值，自己判命中、自己推三态
    // （docs/InputDesign.md §7，用法见 InkingDynamicButton::JudgePointer）。
    //
    // 四个都只是"当前这一帧的状态"，不标脏、不改任何东西。

    /// 指针的 x，设计坐标。
    float GetPointerX() const noexcept;

    /// 指针的 y，设计坐标。
    float GetPointerY() const noexcept;

    /// 左键按着没（**原始按键状态**，不折进"在不在窗口里"）。
    bool IsPointerDown() const noexcept;

    /// 指针在不在窗口里。窗口外不该继续悬停。
    bool IsPointerInside() const noexcept;

    /**
     * 按当前指针状态派发一次：悬停进出、按下、抬起触发点击。
     *
     * **这是今天的最小实现**：直接线性扫绘制列表（从列表尾往前，z 最大的先问），
     * 取第一个 `HitTest` 命中的节点。完整方案（静态烘焙的命中表、三态
     * Block / PassThrough / Miss）在 docs/InputDesign.md，还没落地——
     * 但接口是照着那时候的形状定的，换实现时调用方不用改。
     *
     * 每帧调一次，幂等：同一帧调两次结果一样。
     */
    void DispatchPointer();

    /** 现在按着哪个节点（没有时为 nullptr）。自检观测口。 */
    InkingAnchor* GetPressedNode() const noexcept;

    /** 指针现在悬停在哪个节点上（没有时为 nullptr）。自检观测口。 */
    InkingAnchor* GetHoveredNode() const noexcept;

    // ---------------- 只给 SceneLibrary 调 ----------------

    /**
     * 被库关掉时的通知（`SceneLibrary::CloseScene`）。
     *
     * 摘掉活跃态与登记：关掉之后它不渲染、不再活跃，也不能再注册。
     * 数据全部保留，仍然可读——"先把重要数据 move 出来再关"就是这么用的。
     */
    void MarkClosed() noexcept;

    /** 被库切换活跃态时的通知（`SceneLibrary::SetActiveScene`）。 */
    void SetActiveFromLibrary(bool active) noexcept;

protected:
    /**
     * 把 this 登记进 SceneLibrary。
     *
     * 与后端 ShineBasicModule::registerToStatusChecker() 同一套写法：
     * 在**构造函数末尾**调用，表示自身已经构造完成、可以登记了。
     */
    void registerToSceneLibrary();

    // ---------------- [√] 可重写钩子：两组每帧逻辑 ----------------

    /**
     * **固定逻辑步**的场景逻辑：按固定步长跑，和渲染解耦。
     *
     * 适合状态机、计时、和画面无关的推进。即使场景静默（被别的场景顶掉）
     * 也照跑——这样后台的事情不会因为切场景就停摆。
     * 想"不渲染就彻底不动"，把逻辑放 `onRenderBoundTick`。
     */
    virtual void onTick(double fixedStepSeconds);

    /**
     * **跟着渲染的那组逻辑**：只有活跃（正在渲染）时才跑，每个渲染帧一次。
     *
     * 适合插值、和画面同步的推进——跑在渲染帧上，和实际画出来的那帧对齐。
     * 入参是真实 delta，不保证固定。
     */
    virtual void onRenderBoundTick(double deltaSeconds);

    /**
     * 场景在被绘制时，除了绘制列表里的子节点，自己要不要也画一笔。
     * 基类默认不画（场景是容器，不是可见组件）。
     */
    virtual void onRenderScene(SDL_Renderer* renderer);

    /**
     * 渲染入口：按 z 序把绘制列表里的节点提交给渲染器。
     *
     * **protected**：只有框架该调它（窗口层经 `SceneLibrary::RenderScene`
     * 走一道，保证"只有活跃场景会被渲染"这条规矩有唯一落点）。
     */
    void Render(SDL_Renderer* renderer);

    /** 场景名，需要在构造时直接构造好（令牌要用它当登记用的 key）。 */
    std::string _sceneName;

private:
    friend class SceneLibrary;

    /** 重建绘制列表：剔除失效项、重算静态坐标，并按 z 升序排好。 */
    void sortDrawList();

    /** 场景根矩形的初始数据；三个构造函数共用。 */
    static AnchorData sceneRootAnchorData();

    /** 三个构造函数的公共收尾：盖章成根 + 登记进库。 */
    void finishConstruction();

    /**
     * 把"在盖章成根之前就建出来、因而没找到场景"的节点补登记进来。
     *
     * 场景是构造完成才盖章的，所以**场景构造函数体里建出来的子节点**
     * （最典型的是按钮成员）沿父链找不到它。不补这一趟，它们既不渲染
     * 也点不到——而且不报错，只有画面上什么都没有。
     */
    void rebindEarlyChildren();

    /** 按当前指针位置找最上面那个命中的节点（线性扫，见 DispatchPointer）。 */
    InkingAnchor* hitTestTopmost(float designX, float designY) noexcept;

    /**
     * 重建命中表。由 `sortDrawList()` 在**真的重建了列表之后**调——
     * 表与列表必须同生同灭：顺序或内容一变，候选的 z 序与包络就都过期了。
     *
     * 变换（平移 / 旋转 / 缩放）**不在**重建触发源里：表里存的是组件的
     * **保守包络**（覆盖所有可能变换），精判用当前变换——所以表全程冻结
     * （`docs/InputDesign.md` §11「变换通道不改表」）。
     */
    void rebuildHitTable();

    /**
     * 动态层最上面那个命中者（动态组件不进表，只能现问）。
     *
     * 只在"点落在某个动态洞里"时才会被调用——洞就是为省这一趟而存在的。
     */
    InkingAnchor* topmostDynamicHit(float designX, float designY,
                                    HitKind& kind) noexcept;

    /**
     * 绘制列表，按 z 升序（先画的在底下）。
     *
     * 析构顺序上有一件事要记住：本类析构体先跑（去摘子节点上的 `_scene`），
     * 然后是成员 `_drawList`，最后才是基类 `InkingAnchor`——而 `_scene`
     * 是**基类的成员**，它比 `_drawList` 活得久。所以"列表先没、_scene 后没"
     * 是正常的，析构体里不要去碰列表。
     */
    std::vector<DrawItem> _drawList;

    /** 延迟构造的登记令牌：登记发生在构造函数体内，不在初始化列表里。 */
    std::optional<SceneToken> _sceneRegisterToken;

    /** 列表内容变了（增删、换父级换场景 → 需要剔除失效项并重算坐标）。 */
    bool _contentDirty = true;
    /** 只有顺序变了（zindex → 内容不变，只需重排）。 */
    bool _orderDirty = true;
    /** 骨架结构变更次数，覆盖"内容"与"顺序"两类。 */
    std::uint64_t _structureGeneration = 0;

    /**
     * 烘焙好的命中表（静态条目 + 动态洞）。
     *
     * 与 `_drawList` **同生同灭**：`sortDrawList()` 真的重建列表时会顺手重建它
     * （`rebuildHitTable()`）。没脏的时候一次都不碰——这正是"每帧 0 次重烘"
     * 的来源（`InputDesign.md` §13）。
     */
    HitTable _hitTable;

    /** 正在渲染（由 SceneLibrary 维护，同期最多一个）。 */
    bool _active = false;
    /** 被库关掉过：不能再注册，也不再活跃。 */
    bool _closed = false;
    /** 注册被驳回（名字被占）。这是"停放态"。 */
    bool _rejected = false;

    // ---------------- 指针状态（每帧由窗口层喂） ----------------
    //
    // 三个都是**非拥有**指针，生命周期靠"节点析构时从列表里摘掉"这条纪律：
    // RemoveDrawNode 会把这里指向该节点的指针清成 nullptr，所以不会悬垂。

    float _pointerX = 0.0f;     ///< 设计坐标
    float _pointerY = 0.0f;
    bool _pointerDown = false;  ///< 左键按着没
    bool _pointerInside = false;///< 指针在不在窗口里

    /**
     * 上一帧的按下状态。
     *
     * 放成员不放函数内静态量：静态量会让"两个场景的按下历史"串在一起，
     * 自检里建了又拆的场景会把上一帧的状态留给下一个（实测这种串味最难查）。
     */
    bool _pointerWasDown = false;

    /**
     * 指针脏：指针状态变过，这一帧的悬停判断还没做。
     *
     * 由 `SetPointerState` 在**真的变了**的时候置位（每帧都喂同一个坐标不算变），
     * 帧末统一消费时清掉。T-4 的"每帧 query"就是拿它当开关。
     */
    bool _pointerDirty = false;

    /**
     * 上一次帧末消费时，还有多少节点各自处于脏状态。
     *
     * 纯观测值（自检与诊断用）：动画在跑的时候它应该一直 > 0，
     * 动画停下 `kRepaintQuietFrames` 帧之后回落到 0——"画面稳了"就是这么看的。
     */
    int _repaintingNodeCount = 0;
    int _hitDirtyNodeCount = 0;

    InkingAnchor* _hoveredNode = nullptr;  ///< 这一帧悬停在谁身上
    InkingAnchor* _pressedNode = nullptr;  ///< 按下去时抓的是谁（拖出按钮也还是它）
};

}  // namespace ink
