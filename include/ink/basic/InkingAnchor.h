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
// 这一版只做锚点核心：尺寸、两个锚点、偏移、父子、层级、变化通知。
// 命中相关的部分（命中区域、三态返回、形状判定、命中档位）等 ui 层落地时再作为
// 接口加回来——所谓的命中区域本质上就是项目里多边形 / 圆角（SDF）那套方案。
//
// 实现见 src/ink/basic/InkingAnchor.cpp。

#include <string>

namespace ink {

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

/// 静态初始化用的数据；配置驱动（CXXCSS）那条路由子类/生成器补齐。
struct AnchorData {
    Anchor selfAnchor{};   ///< 自身锚点
    Anchor traceAnchor{};  ///< 上级锚点（追踪的父级锚点）
    float offsetX = 0.0f;  ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;
    int width = 0;         ///< 自身尺寸，设计坐标
    int height = 0;
    int zIndex = 0;        ///< 越大越靠上，同场景内全局比较
};

class InkingAnchor {
public:
    /// 根（窗口 / 场景）传 nullptr 当 parent；其余组件都必须有 parent。
    InkingAnchor(InkingAnchor* parent, const AnchorData& data);

    /// 配置驱动：只给 parent + 名字，怎么读 CXXCSS 交给子类自己实现。
    InkingAnchor(InkingAnchor* parent, const std::string& name);

    /// 虚析构 + 禁拷贝/移动：本类要被继承、持有父指针，之后还要注册进场景与
    /// 命中表，复制出的第二份关系会让父子链和注册表同时指向同一个对象。
    virtual ~InkingAnchor() = default;
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

    /// 设计单位 → 设备像素（窗口缩放 × 系统 DPI）。letterbox 下 XY 同倍率。
    /// 来源是窗口层，不由组件自己倒推；它不标脏，因为它只影响绘制换算。
    float GetMagnification() const noexcept;
    float GetDeviceWidth() const noexcept;
    float GetDeviceHeight() const noexcept;

    bool IsDirty() const noexcept;  ///< 上一次改动还没被消费
    void ClearDirty() noexcept;

    // ---------------- [final] 写入口 ----------------
    //
    // 一律不加 virtual：子类不可重写（docs/API.md「[final] 写入口」）。
    // 标脏与钩子都在内部完成，所以不存在"哪次改动忘了标脏"的漏。
    // 返回 bool 表示这次调用是否真的改动了；没变就不标脏、不触发钩子。

    /// 改自身尺寸；某个方向传 InkingResize::none 表示该方向不动，负值忽略。
    bool Resize(int width, int height) noexcept;
    bool ChangeSelfAnchor(const Anchor& anchor) noexcept;
    bool ChangeTraceAnchor(const Anchor& anchor) noexcept;
    bool ChangeOffset(float offsetX, float offsetY) noexcept;
    bool ChangeZIndex(int zIndex) noexcept;  ///< 层级变化是要标脏的四件事之一
    bool SetParent(InkingAnchor* parent) noexcept;

    void SetMagnification(float magnification) noexcept;  ///< 窗口层写，不标脏
    void SetRegisterOrder(int order) noexcept;            ///< 场景注册时赋，不标脏

    /// a 是否盖在 b 上面：同场景内全局比 zindex，z 相同时晚注册的在上。
    ///
    /// 规则只能有一条，跨子树 / 跨簇 / 溢出桶才好统一比较（docs/InputDesign.md §1、
    /// §15）。将来要做局部层级（堆叠上下文），把这里换成"从根向下的 z 序列按
    /// 字典序比较"即可，调用方不用改。
    static bool IsAbove(const InkingAnchor& a, const InkingAnchor& b) noexcept;

    /// 标记自己"变了"。写入口内部已经调过，子类不要自己调。
    /// 等命中表落地，这里再按 docs/InputDesign.md §8 一级级传到最近的表边界。
    void MarkDirty() noexcept;

protected:
    // ---------------- [√] 可重写钩子 ----------------
    //
    // 钩子只挂附加逻辑：不负责标脏（写入口已经标了），也不拦截写入口。
    virtual void onSizeChanged();
    virtual void onSelfAnchorChanged();
    virtual void onTraceAnchorChanged();
    virtual void onOffsetChanged();
    virtual void onZIndexChanged(int zIndex);
    virtual void onParentChanged(InkingAnchor* parent);
    virtual void onDirty();

    std::string _name;

    Anchor _selfAnchor;   ///< 自身锚点
    Anchor _traceAnchor;  ///< 追踪的上级锚点

    float _offsetX = 0.0f;  ///< 对齐之后的微调，设计坐标
    float _offsetY = 0.0f;

    int _width = 0;   ///< 自身尺寸，设计坐标
    int _height = 0;

    float _magnification = 1.0f;  ///< 展示倍率：设计单位 → 设备像素

    int _zIndex = 0;        ///< 层级：z 越高渲染越靠后（越靠上）
    int _registerOrder = 0; ///< 注册序号，只在 z 相同时决定先后

    InkingAnchor* _parent = nullptr;  ///< 非拥有关系；窗口 / 场景为空

    bool _dirty = true;  ///< 改动还没被消费
};

}  // namespace ink
