#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <core/Canvas.h>
#include <ink/basic/InkingAnchor.h>
#include <scene/HitTable.h>
#include <scene/InkEvent.h>

namespace ink {

/**
 * 场景：窗口之下最高的结点（docs/API.md「InkingScene」）。
 *
 * 这一层只做四件事：
 *
 *   1. **几何靠锚点推**：继承 InkingAnchor，自身锚点对上父级锚点，再叠偏移。
 *      场景层没有位置 / 尺寸写入口——锚点是初始化对齐，不是布局变化。
 *   2. **静态层**：把自己和静态子孙压平成「命中索引 + 平铺绘制表」，几何、
 *      可见性、层级一变就标脏，下一帧重烘（docs/InputDesign.md §9 / §10）。
 *      声明成等宽等距的容器用纯算术索引，连表都不建（§10.4）。
 *   3. **命中**：三态 query（Block / PassThrough / Miss）+ 目标。静态查表、
 *      动态自判，两者按 zindex 合并（§6）。
 *   4. **重绘**：只影响外观的写入口自己调 MakeDirty()，窗口层照
 *      RedrawScheduler 决定这帧画不画——和输入那一路彼此独立。
 *
 * 场景永远静态（§2）：会动的东西压在动态子树上，父场景把它记成一个「洞」，
 * 所以静态表一次都不用重烘。
 */
class InkingScene : public InkingAnchor {
public:
    /// 配置驱动构造：构造即注册到场景库，Show(name) 按名字取（docs/API.md）。
    explicit InkingScene(const std::string& name, bool dynamic = false);
    ~InkingScene() override;

    InkingScene(const InkingScene&) = delete;
    InkingScene& operator=(const InkingScene&) = delete;
    InkingScene(InkingScene&&) = delete;
    InkingScene& operator=(InkingScene&&) = delete;

    // ------------------------------------------------------------------
    // 树：所有组件都要注册到一个场景里（docs/InputDesign.md §8）
    // ------------------------------------------------------------------

    /// 挂一个已有场景进来；生命周期由调用方自己管，必须比本场景活得久。
    InkingScene& Add(InkingScene& child);
    /// 现场建一个子场景并接管它的生命周期。
    template <class T, class... Args>
    T& Make(Args&&... args) {
        static_assert(std::is_base_of_v<InkingScene, T>, "Make<T>() 只接受 InkingScene 及其派生类");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& created = *owned;
        _owned.push_back(std::move(owned));
        Add(created);
        return created;
    }

    InkingScene* GetParentScene() const noexcept;
    const std::vector<InkingScene*>& GetChildren() const noexcept;
    InkingScene& RootScene() noexcept;

    /// 绝对矩形：锚点推出来的位置 + 自身尺寸。
    Rect GetRect() const noexcept;

    // ------------------------------------------------------------------
    // [final] 写入口（场景层只有可见性这一档）
    // ------------------------------------------------------------------

    bool GetVisible() const noexcept;
    bool SetVisible(bool visible);
    /// [√] 可见性变化的钩子：挂附加逻辑，不标脏、也不拦截写入口。
    virtual void onVisibleChanged(bool visible);

    // ------------------------------------------------------------------
    // 命中策略 / 档位
    // ------------------------------------------------------------------

    HitPolicy GetHitPolicy() const noexcept;
    /// 改命中策略（Block / PassThrough）——影响命中，所以会标脏。
    bool SetHitPolicy(HitPolicy policy);
    bool IsDynamic() const noexcept;

    // ------------------------------------------------------------------
    // 重绘（和输入那一路独立实现）
    // ------------------------------------------------------------------

    /// 需要重绘。颜色、文字、悬停、按下这些**只影响重绘**的属性写入口明确调它；
    /// 几何 / 可见性 / 层级由写入口内部顺带标一次，不用你自己调。
    void MakeDirty(const std::string& reason = "外观变化");

    // ------------------------------------------------------------------
    // 静态层：烘焙
    // ------------------------------------------------------------------

    /// 建索引：命中索引（网格表或等宽等距专用查找）+ 平铺绘制表 + 动态洞。
    void Bake();
    bool NeedsBake() const noexcept;
    bool IsBaked() const noexcept;

    /// 命中结果可能过期（几何 / 层级 / 可见性变过）。输入层消费这个标记。
    bool IsTreeChanged() const noexcept;
    void ClearTreeChanged() noexcept;

    /// 烘焙次数：自检用，证明「静态层不因为动态子树移动而重烘」。
    std::uint64_t BakeCount() const noexcept;

    /// 声明本容器的静态子节点是「等宽等距」排列（菜单栏 / 工具条 / 分页条）：
    /// 命中不用建表，一次除法就够（docs/InputDesign.md §10.4）。
    void UseSlotLayout();
    bool IsSlotLayout() const noexcept;
    bool UsesSlotIndex() const noexcept;

    const HitTable& GetHitTable() const noexcept;
    const SlotIndex& GetSlotIndex() const noexcept;
    const std::vector<DynamicHole>& GetDynamicHoles() const noexcept;

    // ------------------------------------------------------------------
    // 命中：三态 + 目标（静态查表、动态自判，按 zindex 合并）
    // ------------------------------------------------------------------

    HitTarget Query(float x, float y);

    // ------------------------------------------------------------------
    // 绘制
    // ------------------------------------------------------------------

    void Draw(Canvas& canvas) const;
    /// 只画自己，不画子场景。
    virtual void onDraw(Canvas& canvas) const;

    // ------------------------------------------------------------------
    // 每帧：静态子树不接这个回调，只有动态子树收得到
    // ------------------------------------------------------------------

    void Tick(float deltaSeconds);
    virtual void onTick(float deltaSeconds);

    // ------------------------------------------------------------------
    // 输入：默认都不处理，返回 false 让事件继续往上冒泡
    // ------------------------------------------------------------------

    virtual bool onPointerDown(PointerEvent& event);
    virtual bool onPointerUp(PointerEvent& event);
    virtual void onPointerEnter(PointerEvent& event);
    virtual void onPointerLeave(PointerEvent& event);
    virtual void onPointerCancel(PointerEvent& event);
    virtual bool onClick(PointerEvent& event);

protected:
    /// 写入口内部标脏时走的钩子（InkingAnchor::MarkDirty → 这里）。
    /// 几何、层级变化都从这里过一遍，所以「表该重烘 + 重绘」不会漏。
    void onDirty() override;

private:
    /// 平铺绘制表里的一项：recursive 表示这棵子树是动态的，要递归画。
    struct DrawItem {
        const InkingScene* node = nullptr;
        bool recursive = false;
    };

    void InvalidateHit();
    void MarkTreeChanged() noexcept;
    void CollectSubtree(std::vector<StaticEntry>& entries, std::vector<DynamicHole>& holes,
                        std::vector<DrawItem>& drawList);
    HitTarget QueryDynamic(float x, float y);

    bool _dynamic = false;
    bool _visible = true;
    HitPolicy _hitPolicy = HitPolicy::Block;
    bool _slotLayout = false;     ///< 声明：静态子节点等宽等距
    bool _usesSlotIndex = false;  ///< 上一次烘焙实际用了专用查找
    std::uint64_t _bakeCount = 0;

    InkingScene* _parentScene = nullptr;
    std::vector<InkingScene*> _children;
    std::vector<std::unique_ptr<InkingScene>> _owned;  ///< Make() 建出来的孩子

    HitTable _hitTable;
    SlotIndex _slotIndex;
    std::vector<DynamicHole> _holes;   ///< 按 z 降序
    std::vector<DrawItem> _drawList;   ///< 按 z 升序（画得早的先）
    bool _baked = false;
    bool _bakeDirty = true;
    bool _treeChanged = true;
};

}  // namespace ink
