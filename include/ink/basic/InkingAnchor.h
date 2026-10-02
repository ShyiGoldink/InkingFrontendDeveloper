#pragma once

// 这里是整个项目的锚点，根据该结构实现整个项目的拓展基础。
//
// 设计方向：
//   - 每个 ui 和窗口都以此为基类；除窗口外，其余组件创建时都要传 parent
//   - 只给 parent + 名字的构造走 CXXCSS 配置；给完整数据结构的构造走静态初始化
//   - 不同组件怎么读 CXXCSS 由子类自行实现，基类只负责数据结构与变化联动
//   - onclick 这类回调由接口挂上去
//
// 三条硬约定（docs/API.md「关键词」）：
//   1. 写入口不可重写（不加 virtual），标脏由写入口内部统一完成；
//      要挂附加逻辑就重写 onXxxChanged 钩子，钩子不标脏、也不拦截写入口
//   2. 会标脏的只有几何、可见性、层级、鼠标移动这四件事；颜色、不影响命中的
//      装饰、以及展示倍率都不标脏
//   3. 位置不存，由锚点推导：自身矩形里的「自身锚点」落在父级矩形里的
//      「上级锚点」上，再叠偏移
//
// 渲染侧的约定（骨架变化才重建顺序）：
//   - 每个锚点记住自己属于哪个场景（沿父链找最近的 InkingScene），构造时建立；
//   - 骨架变化——构造、析构、SetParent、ChangeZIndex、SetVisible——只调
//     NotifySceneStructureChanged() 把场景标脏，**不在这里排序**；
//     真正的排序推迟到帧末，由场景惰性做一次，避免一帧里连改十个 zindex
//     就重建十次；
//   - 绘制列表是**索引不是真相源**：父子关系才是真相，列表只是它的派生视图，
//     结构一变就重建。两份真相不同步是野指针和"画的是一个、点的是另一个"
//     这类问题的来源（docs/InputDesign.md §8）。
//
// 命中相关的部分（命中区域、三态返回、形状判定、命中档位）等 ui 层落地时再作为
// 接口加回来——所谓的命中区域本质上就是项目里多边形 / 圆角（SDF）那套方案。
//
// 实现见 src/ink/basic/InkingAnchor.cpp。

#include <ink/dataStruct/InkingShapeSpec.h>

#include <cstdint>
#include <string>

// 必须在 namespace 之外前向声明：写成 `struct SDL_Renderer*` 放在 namespace ink
// 里面，声明出来的是 ink::SDL_Renderer 这个**新类型**，和真头文件里的
// ::SDL_Renderer 不是一个东西，于是所有 SDL 调用都会"参数类型不匹配"。
struct SDL_Renderer;

namespace ink {

class InkingScene;

/// 锚点：百分比，(0,0) 是左上角，(1,1) 是右下角。组件使用锚点时都被视作矩形。
struct Anchor {
    float x = 0.0f;
    float y = 0.0f;
};

/// 预置锚点，拿来即用；要自定义直接填 Anchor。
struct InkingChangeAnchor {
    static constexpr Anchor LeftTop{0.0f, 0.0f};
    static constexpr Anchor CenterTop{0.5f, 0.0f};
    static constexpr Anchor RightTop{1.0f, 0.0f};
    static constexpr Anchor LeftCenter{0.0f, 0.5f};
    static constexpr Anchor Center{0.5f, 0.5f};
    static constexpr Anchor RightCenter{1.0f, 0.5f};
    static constexpr Anchor LeftBottom{0.0f, 1.0f};
    static constexpr Anchor CenterBottom{0.5f, 1.0f};
    static constexpr Anchor RightBottom{1.0f, 1.0f};
};

/// Resize 的哨兵值：某个方向传 none，表示这个方向不动。
struct InkingResize {
    static constexpr int none = -1;
};

/**
 * 层级的下限：zindex 一律不小于这个值。
 *
 * 绘制是从 z **低到高**提交的（先画底下的），也就是**画得最晚的盖在最上面**。
 * 所以"背景板"这种铺满整屏、又想待在一切之下的东西，必须有一个比其它所有
 * 组件都低的 z——一旦它和别的组件同 z，注册序号靠后就会把它排到最后一个画，
 * 整屏全被盖掉。这条下限把"背景层"单独留出来，其它组件正常用 0 及以上即可。
 *
 * （早先这里写反成"从 z 高到低提交"，方向是错的，已按实测改回来。）
 */
struct InkingZIndex {
    static constexpr int background = -128;
};

/// 静态初始化用的数据；配置驱动（CXXCSS）那条路由子类/生成器补齐。
struct AnchorData {
    Anchor selfAnchor{};   ///< 自身锚点
    Anchor traceAnchor{};  ///< 上级锚点（追踪的父级锚点）
    float offsetX = 0.0f;  ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;
    int width = 0;         ///< 自身尺寸，设计坐标
    int height = 0;
    int zIndex = 0;        ///< 越大越靠上，同场景内全局比较
    bool visible = true;   ///< 可见性；不可见连命中都不必问
    std::uint32_t color = 0xFF569CD6u;  ///< 占位填充色 0xAARRGGBB（样式层接手）
};

class InkingAnchor {
public:
    /// 虚析构 + 禁拷贝/移动：本类要被继承、持有父指针，还要登记进场景的
    /// 绘制列表，复制出的第二份关系会让父子链和注册表同时指向同一个对象。
    /// 析构体在 .cpp 里：它要反注册自己，不能写成 = default。
    virtual ~InkingAnchor();
    InkingAnchor(const InkingAnchor&) = delete;
    InkingAnchor& operator=(const InkingAnchor&) = delete;
    InkingAnchor(InkingAnchor&&) = delete;
    InkingAnchor& operator=(InkingAnchor&&) = delete;

    // ---------------- 只读查询 ----------------
    const std::string& GetName() const noexcept;
    InkingAnchor* GetParent() const noexcept;

    const Anchor& GetSelfAnchor() const noexcept;
    const Anchor& GetTraceAnchor() const noexcept;

    int GetWidth() const noexcept;   ///< 自身尺寸，设计坐标
    int GetHeight() const noexcept;

    float GetOffsetX() const noexcept;  ///< 对齐之后的微调，设计坐标
    float GetOffsetY() const noexcept;

    float GetX() const noexcept;     ///< 相对父级左上角，设计坐标
    float GetY() const noexcept;
    float GetAbsX() const noexcept;  ///< 相对根；O(深度)
    float GetAbsY() const noexcept;

    int GetZIndex() const noexcept;
    int GetRegisterOrder() const noexcept;  ///< 只在 z 相同时决定先后

    /// 自己这一层的可见性；父级不可见时本项仍为 true。
    /// 要问"实际会不会被画出来"用 IsVisibleInTree()。
    bool IsVisible() const noexcept;
    /// 自己与全部祖先都可见时为 true（骨架遍历只对可见子树下探）。
    bool IsVisibleInTree() const noexcept;

    /// 本节点的占位填充色，0xAARRGGBB。颜色不标脏（docs/API.md「标脏」）。
    std::uint32_t GetColor() const noexcept;

    /// 设计单位 → 设备像素（窗口缩放 × 系统 DPI）。letterbox 下 XY 同倍率。
    /// 来源是窗口层，不由组件自己倒推；它不标脏，因为它只影响绘制换算。
    float GetMagnification() const noexcept;
    float GetDeviceWidth() const noexcept;
    float GetDeviceHeight() const noexcept;

    /**
     * **命中脏**：上一次"会影响命中"的改动还没被消费。
     *
     * 触发源（`docs/InputDesign.md` §4）：几何、可见性、层级、父子。
     * 消费点在**帧首**（渲染之前）——用之前保证新鲜；将来命中表落地后，
     * 这里就是"要不要重烘表"的依据。
     */
    bool IsDirty() const noexcept;

    /**
     * 清掉命中脏。
     *
     * 一般不用手工调：帧末的统一消费（`ConsumeDirtyFrame`）会在连续
     * `kHitQuietFrames` 帧没有新脏时自己清。手工调只有一种正当用途——
     * "这个组件构造完了，几何到此定型"（静态按钮构造末尾就是这么做的）。
     */
    void ClearDirty() noexcept;

    /**
     * **重绘脏**：画面变了（颜色 / 文字 / 变换 / 几何），但命中表**不需要**重烘。
     *
     * 这是 T-3 补的那条"额外的标脏方式"，它和命中脏**不是一回事**：
     *
     * | | 命中脏 | 重绘脏 |
     * | --- | --- | --- |
     * | 谁触发 | 几何 / 可见性 / 层级 / 父子 | 颜色 / 文字 / 变换 / 几何 |
     * | 消费点 | 帧首（重建绘制列表，将来还要重烘表） | 帧末（记账 + 计时清零） |
     * | 不处理的后果 | 点到的地方和画的地方对不上 | 画面停在上一帧的样子 |
     *
     * **动画每帧改颜色时如果错标成命中脏，就会每帧白重建一次绘制列表**
     * （排序 + 重算所有静态节点的坐标快照）——这条分开就是为了不付那个代价。
     */
    bool IsRepaintDirty() const noexcept;
    void ClearRepaintDirty() noexcept;

    /// 重绘脏要连续安静多少帧才算"画面稳了"（T-3 定的口径：5 帧）。
    static constexpr int kRepaintQuietFrames = 5;

    /// 命中脏要连续安静多少帧（`InputDesign.md` §5：2~3 即可，取 3）。
    static constexpr int kHitQuietFrames = 3;

    /**
     * 帧末统一消费里的一步：推进这个节点自己的安静帧计数，够数就清脏。
     *
     * 由场景在 `InkingScene::ConsumeFrameDirty()` 里对本场景每个节点调一次——
     * **同一帧里标了多少次脏，都只走这一回**。这正是"标脏"存在的意义：
     * 把一帧里的多次改动（连着 Resize 三次、ChangeZIndex 两次）合并成一次处理，
     * 而不是每改一次就重烘一次表。
     */
    void ConsumeDirtyFrame() noexcept;

    /// 自己就是某棵渲染树的根（场景 / 窗口），没有场景祖先可登记。
    bool IsSceneRoot() const noexcept;

    /// 是动态组件吗（几何会自己变、每帧收到 Tick）。
    ///
    /// 判据在**构造时就定死**（静态 / 动态是两个类型），所以这不是运行期
    /// 猜出来的，而是登记那一刻就明确的——渲染时靠它决定"绝对坐标能不能
    /// 用建表时的快照"。
    bool IsDynamic() const noexcept;

    /// 沿父链找到最近的场景祖先；自己是场景、或不在任何场景下时返回 nullptr。
    InkingScene* GetSceneAncestor() noexcept;

    /**
     * 指针在这个位置上吗。坐标是**世界坐标（设计空间）**。
     *
     * 默认返回 false：基类不可实例化，也没有形状概念——"怎么算在里面"是
     * 具体组件的知识（按钮问自己的 SDF 形状，见 `ShapeContains`）。
     *
     * 谁负责 z 序：**调用方**。这个函数只回答"我在不在这个点下面"，
     * 不回答"是不是我最上面"——后者要按绘制列表从高到低问，规则只有一份
     * （`InkingScene` 的命中表 / `hitTestTopmost`）。
     *
     * 它只回答"几何上在不在"，**不回答"要不要让过"**——后者是三态里的
     * `PassThrough`，见 `SetHitPassThrough`（把几何与策略分开）。
     */
    virtual bool HitTest(float worldX, float worldY) const;

    /**
     * 命中索引用的**保守包络**，相对本节点左上角（设计坐标）。
     *
     * 默认给整块矩形（基类不认识形状，那是组件的事）——格子只负责缩小范围，
     * 包络给大了只是候选多几个，**不会误命中**，所以"保守"是安全的默认。
     *
     * **什么时候必须 override**：本节点的变换会在运行期变（三态之间的
     * `transform`）时。包络要覆盖**所有可能变换**，否则表里登记不全 → 漏命中。
     * 变换通道的设计是"表冻结、查表时反变换"（`InputDesign.md` §11），
     * 靠的就是这里的包络不动、而精判用当前变换。
     */
    virtual ShapeBounds GetHitEnvelope() const;

    /**
     * 命中之后让不让过（三态里的 `PassThrough`）。
     *
     * 默认 `false` = **Block**（事件到此为止）——半透明遮罩、普通控件都是这一类；
     * 置 true 表示"我在这儿，但事件继续往下找"。点击与悬停共用这条规则。
     * 它**不标脏**：让不让过是策略，不是几何，改了不会让命中表失效
     * （代价是当前这一帧的判定还用旧策略，最迟下一帧生效）。
     */
    void SetHitPassThrough(bool passThrough) noexcept;
    bool IsHitPassThrough() const noexcept;

    /**
     * 重新认定"我属于哪个场景"，并补登记进它的绘制列表。
     *
     * 只给场景在构造末尾用：场景是"构造完成才盖章成渲染树的根"的，所以
     * **场景构造函数体里建出来的子节点**沿父链找不到它（那时它还没盖章），
     * 既不渲染也点不到。场景盖章之后回头把这些节点补登记一遍，就是这个函数。
     *
     * 已经在列表里的节点是空操作（`AddDrawNode` 自己查重），所以重复调用安全。
     * 只由框架调用，别在业务代码里手工调。
     */
    void RebindScene(InkingScene* scene) noexcept;

    /// 本节点所属的场景，非拥有指针；没有场景时为 nullptr。
    InkingScene* GetScene() const noexcept;

    // ---------------- [final] 写入口：骨架变化 ----------------
    //
    // 可见性、层级、父子关系这类"骨架变化"两个版本都有，所以放在基类。
    // 一律不加 virtual：子类不可重写（docs/API.md「[final] 写入口」）。
    // 标脏与钩子都在内部完成，所以不存在"哪次改动忘了标脏"的漏。
    // 返回 bool 表示这次调用是否真的改动了；没变就不标脏、不触发钩子。
    //
    // 几何写入口（尺寸 / 自身锚点 / 上级锚点 / 偏移）**不在这里**——那是动态
    // 组件才有的东西，见 InkingDynamicAnchor。放进基类就等于"表里存的基类
    // 指针也能改静态组件的几何"，而那正是要避免的事。

    /// 可见性变化：不画了，但结构还在（数据、子节点、命中条目都保留）。
    /// 连带影响绘制顺序，所以按骨架变化处理——标脏 + 通知场景重排。
    bool SetVisible(bool visible) noexcept;

    bool ChangeZIndex(int zIndex) noexcept;  ///< 层级变化是要标脏的四件事之一
    bool SetParent(InkingAnchor* parent) noexcept;

    void SetMagnification(float magnification) noexcept;  ///< 窗口层写，不标脏

    /// a 是否盖在 b 上面：同场景内全局比 zindex，z 相同时晚注册的在上。
    ///
    /// 规则只能有一条，跨子树 / 跨簇 / 溢出桶才好统一比较（docs/InputDesign.md §1、
    /// §15）。将来要做局部层级（堆叠上下文），把这里换成"从根向下的 z 序列按
    /// 字典序比较"即可，调用方不用改。
    ///
    /// **渲染顺序和命中顺序必须共用这一个比较器**，否则会出现"画的是 A、
    /// 点到的是 B"——两边单独看都对，合起来才错。
    static bool IsAbove(const InkingAnchor& a, const InkingAnchor& b) noexcept;

    /// 标记自己"变了"。写入口内部已经调过，子类不要自己调。
    /// 等命中表落地，这里再按 docs/InputDesign.md §8 一级级传到最近的表边界。
    void MarkDirty() noexcept;

    /// 标记自己"画面变了"。写入口与**动画钩子**内部调（`onAnimationTick` 里
    /// 颜色/变换推进了一帧就该调它）——动画每帧都在变，但它不该惊动命中表。
    void MarkRepaintDirty() noexcept;

    /// 通知所属场景"骨架变了，绘制顺序得重排"。
    /// 构造、析构、SetParent、ChangeZIndex、SetVisible 内部都会调。
    void NotifySceneStructureChanged() noexcept;

    /// 把 this 登记进场景的扁平绘制列表。
    /// 由 InkingAnchor 构造时沿父链登记自己，以及 InkingScene 在场景构造时
    /// 对整棵现有子树补登记一次（场景必须比子树后构造时才会用到）。
    /// 只由框架调用，别在业务代码里手工调。
    void RegisterToScene(InkingScene* scene) noexcept;

    /// 从所属场景的绘制列表里摘掉自己（解引用、换父级时用）。
    void UnregisterFromScene() noexcept;

    /// 把 `_scene` 指针置空，**但不碰场景的绘制列表**。
    ///
    /// 给"InkingScene 正在析构"这条路径用：那时列表马上随场景一起销毁，
    /// 而列表里的裸指针可能正指向正在析构的兄弟节点，碰不得。
    /// 只清指针，节点留着的那份"我属于这个场景"就不会变成野指针。
    void DetachFromScene() noexcept;

    /// 把本节点提交给渲染器。由场景的渲染入口按 z 序逐个调用，
    /// 传进来的绝对坐标是**设计坐标**，这里换算成设备像素再交给 onRender。
    void SubmitToRenderer(SDL_Renderer* renderer, float absX, float absY) const;

    /**
     * **每渲染帧一次**：推进这个节点自己的动画（颜色 / 透明度过渡）。
     *
     * 由场景在渲染之前调（`InkingScene::TickFrame` 里分发），传真实 delta。
     *
     * 为什么静态组件也有这个入口：`InkingDynamicAnchor::Tick` 是**动态档**的逻辑步
     * （几何会变的那一档才收得到），而颜色过渡不该因为"几何不变"就没法推进。
     * 两条通道分开是有意的——颜色动画不标脏、不进命中表，所以它不该把组件
     * 拉进动态档（`TaskGuide.md` 的 D-1 把这条写成硬约束）。
     *
     * 放渲染帧而不是逻辑步：过渡要和画面同拍，否则快速动画下会差帧
     * （docs/InputDesign.md §11「命中与渲染必须用同一个 t」）。
     */
    void TickAnimation(float deltaSeconds) noexcept {
        onAnimationTick(deltaSeconds);
    }

protected:
    /// 只给两个派生类调：**本类不可直接实例化**，每个组件声明时必须选一种
    /// （静态还是动态）。根（窗口 / 场景）传 nullptr 当 parent。
    /// `dynamic` 就是"这个组件属于哪一档"，由派生类在构造时如实传进来。
    InkingAnchor(InkingAnchor* parent, const AnchorData& data, bool dynamic);

    /// 配置驱动：只给 parent + 名字，怎么读 CXXCSS 交给子类自己实现。
    InkingAnchor(InkingAnchor* parent, const std::string& name, bool dynamic);

    // ---------------- [√] 可重写钩子 ----------------
    //
    // 钩子只挂附加逻辑：不负责标脏（写入口已经标了），也不拦截写入口。
    virtual void onSizeChanged();
    virtual void onSelfAnchorChanged();
    virtual void onTraceAnchorChanged();
    virtual void onOffsetChanged();
    virtual void onZIndexChanged(int zIndex);
    virtual void onParentChanged(InkingAnchor* parent);
    virtual void onVisibleChanged(bool visible);
    virtual void onDirty();

    /**
     * 本节点自己的动画，**每渲染帧一次**（真实 delta）。基类什么都不做。
     *
     * 静态 / 动态两档都会收到：颜色过渡与几何无关，所以它不归动态档那条 `Tick`。
     * 只被 `TickAnimation` 调，别在业务代码里手工调。
     */
    virtual void onAnimationTick(float deltaSeconds);

    /// 本节点怎么画。场景的渲染入口在提交每个节点时调一次。
    /// 传进来的是本节点的绝对左上角，已经是**设备像素**（展示倍率换算过了）。
    /// 现在的实现只是"按 color 填一个矩形"，SDF 形状层接手后换成形状描述。
    virtual void onRender(float pixelX, float pixelY) const;

    /**
     * **骨架变化**的统一出口：命中脏 + 重绘脏 + 通知场景重排，一次做完。
     *
     * 写入口（几何四个 + \c SetVisible / \c ChangeZIndex / \c SetParent）内部都调它。
     * 集中在一个函数里有两个好处：
     *   1. 不会漏——漏一个就是"这条改动没被消费"，而且往往不报错、只是画面或命中悄悄不对；
     *   2. 将来要按 \c InputDesign.md §8 把标脏"送到最近的表边界"时，只改这一处。
     */
    void MarkStructureChanged() noexcept;

    /// 场景专用：进构造函数体后把自己声明为渲染树的根。
    ///
    /// 不能指望父链查找在构造期就认出场景——构造 InkingScene 时它的动态类型
    /// 还是 InkingAnchor，那一趟查找认不出来，所以由场景自己在构造末尾盖章。
    void MarkAsSceneRoot(InkingScene* scene) noexcept;

    /// 场景专用：场景析构前把子树里指向自己的 _scene 摘掉，
    /// 避免留下指向已销毁场景的野指针。
    void DetachSceneFromSubtree() noexcept;

    std::string _name;

    Anchor _selfAnchor;   ///< 自身锚点
    Anchor _traceAnchor;  ///< 追踪的上级锚点

    float _offsetX = 0.0f;  ///< 对齐之后的微调，设计坐标
    float _offsetY = 0.0f;

    int _width = 0;   ///< 自身尺寸，设计坐标
    int _height = 0;

    float _magnification = 1.0f;  ///< 展示倍率：设计单位 → 设备像素

    int _zIndex = 0;        ///< 层级：z 越高渲染越靠后（越靠上）
    int _registerOrder = 0; ///< 注册序号，构造时自动赋；只在 z 相同时决定先后

    bool _visible = true;  ///< 自己这一层的可见性

    /// 命中后让不让过（三态里的 PassThrough）。默认 Block。
    ///
    /// 放基类而不是"每个组件自己实现三态"：**让不让过和形状无关**，
    /// 它是策略；而"在不在形状里"才是组件知识。两件事分开之后，
    /// 组件只管把形状判对，策略由写入口改、由场景合并。
    bool _hitPassThrough = false;

    /// 属于哪一档：静态（false，几何构造即定型）还是动态（true，几何会自己变）。
    /// 派生类在构造时如实传进来，之后不变——不支持运行期在两者之间迁移
    /// （docs/InputDesign.md §2 第 3 条）。
    bool _dynamic = false;

    std::uint32_t _color = 0xFF569CD6u;  ///< 占位填充色 0xAARRGGBB

    InkingAnchor* _parent = nullptr;  ///< 非拥有关系；窗口 / 场景为空

    InkingScene* _scene = nullptr;  ///< 所属场景，非拥有；构造时沿父链定下

    /// **命中脏**：会影响命中的改动还没被消费（帧首消费）。
    bool _dirty = true;

    /// **重绘脏**：画面变了还没重绘（帧末消费）。初始为真——第一帧总得画一次。
    bool _repaintDirty = true;

    /// 两个脏各自"已经安静了多少帧"。够 `kHitQuietFrames` / `kRepaintQuietFrames`
    /// 就清掉对应的脏；任何一次新的标脏都会把它归零。
    int _hitQuietFrames = 0;
    int _repaintQuietFrames = 0;
};

// ---------------------------------------------------------------------------
// 两种锚点：静态组件与动态组件
//
// 判据只有一条 —— **几何会不会在构造之后自己变**。基类不可直接实例化，
// 所以每个组件声明的时候就必须选一种：
//
//   静态（InkingStaticAnchor）：构造时定型，之后几何不动 → 进命中表。
//   动态（InkingDynamicAnchor）：几何会自己变 → 不进表，自己维护命中状态，
//       每帧收到一次 Tick。
//
// 分流不靠运行期标记：谁属于哪一档，在**登记**那一刻就是明确的（静态的登记
// 进表、动态的登记进动态档），所以查询与合并时不需要再问一次"你是不是动态的"。
//
// 两版共用的东西（都在 InkingAnchor 里）：只读查询、层级、父级、可见性、
// 展示倍率、标脏、IsAbove。也就是说"静态"少的**只有几何写入口**。
// ---------------------------------------------------------------------------

/// 静态组件：几何构造即定型。
///
/// 它没有几何写入口，基类里也没有——所以就算拿到 InkingAnchor*（命中表里存
/// 的就是它）也改不了几何。这是类型上的硬保证，不靠约定。
class InkingStaticAnchor : public InkingAnchor {
public:
    InkingStaticAnchor(InkingAnchor* parent, const AnchorData& data);
    InkingStaticAnchor(InkingAnchor* parent, const std::string& name);

protected:
    /// 默认画成一块实心矩形，用 AnchorData 里的 color。
    ///
    /// 这是第 0 层渲染：够用、能看，而且**不用每个静态组件都自己写一遍**。
    /// SDF 形状层接手后，这里换成"按形状描述生成 SDF 并合批"，
    /// 但"基类给个默认实现、子类按需重写"这个结构不用变。
    void onRender(float pixelX, float pixelY) const override;
};

/// 动态组件：几何会自己变，所以不进表、自己维护命中状态。
class InkingDynamicAnchor : public InkingAnchor {
public:
    InkingDynamicAnchor(InkingAnchor* parent, const AnchorData& data);
    InkingDynamicAnchor(InkingAnchor* parent, const std::string& name);

    // ---------------- [final] 写入口：几何变化 ----------------
    // 只有动态版有这几个入口。同样不加 virtual，标脏与钩子都在内部完成；
    // 返回 bool 表示这次是否真的改了，没变就不标脏、不触发钩子。

    /// 改自身尺寸；某个方向传 InkingResize::none 表示该方向不动，负值忽略。
    bool Resize(int width, int height) noexcept;
    bool ChangeSelfAnchor(const Anchor& anchor) noexcept;
    bool ChangeTraceAnchor(const Anchor& anchor) noexcept;
    bool ChangeOffset(float offsetX, float offsetY) noexcept;

    /// 每帧一次，由场景调。静态组件压根没有这个入口。
    void Tick(float deltaSeconds) noexcept { onTick(deltaSeconds); }

protected:
    /// 每帧一次的钩子，动态组件按需重写。
    virtual void onTick(float deltaSeconds);

    /// 默认画成一块实心矩形，和静态版一致：动态组件常常只是位置在动，
    /// 形状本身没变，所以默认实现能直接复用。
    void onRender(float pixelX, float pixelY) const override;
};

}  // namespace ink
