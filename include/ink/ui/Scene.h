#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <ink/ui/Canvas.h>
#include <ink/ui/HitTable.h>
#include <ink/ui/InkRect.h>
#include <ink/ui/UiEvent.h>

namespace ink {

// 场景：窗口之下最高的结点，场景里可以再套场景。
//
// 静态场景（isDynamic = false）
//   第一次被命中或显式 bake() 时，把自己和所有静态子孙压平成
//   「坐标 → UI 映射表 + 平铺绘制表」。之后点击只查表、绘制只走数组，
//   不遍历树、不递归、不分配。几何一变（SetRect / SetVisible / add）
//   会自己标脏，下一帧重烘焙。
//
// 动态场景（isDynamic = true）
//   不烘焙，每帧老老实实遍历子树，位置会动的场景才该付这份代价。
//   动态子树挂在静态场景下时，父场景的映射表把它记成一个"洞"：
//   查表命中的静态结果与这些洞按声明序号比大小，谁在上面谁赢。
//
// 事件分发：命中目标先处理，返回 true 就不再往上冒泡，返回 false 就交给
// 父场景——所以"面板吃掉点击"和"按钮点击冒到面板"是同一个机制。
class Scene {
public:
    // name 同时是场景库里的名字（InkingWindow::Show 按名字取根场景）；
    // dynamic = true 表示不烘焙，每帧遍历。
    explicit Scene(std::string name, bool dynamic = false);
    virtual ~Scene();

    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;

    // ------------------------------------------------------------------
    // 属性
    // ------------------------------------------------------------------
    const std::string& GetName() const noexcept;
    Rect GetRect() const noexcept;
    void SetRect(const Rect& rect);
    bool GetVisible() const noexcept;
    void SetVisible(bool visible);
    bool IsDynamic() const noexcept;
    /// 建树时分配的全局序号：越大越靠上，同级里后建的盖住先建的。
    int GetOrder() const noexcept;
    Scene* GetParent() noexcept;
    const Scene* GetParent() const noexcept;
    const std::vector<Scene*>& GetChildren() const noexcept;

    // ------------------------------------------------------------------
    // 套场景
    // ------------------------------------------------------------------
    /// 挂一个已有场景进来；生命周期由调用方自己管，必须比本场景活得久。
    Scene& add(Scene& child);
    /// 现场建一个子场景并接管它的生命周期。
    template <class T, class... Args>
    T& make(Args&&... args) {
        static_assert(std::is_base_of_v<Scene, T>, "make<T>() 只接受 Scene 及其派生类");
        auto owned = std::make_unique<T>(std::forward<Args>(args)...);
        T& created = *owned;
        addOwned(std::move(owned));
        return created;
    }
    Scene& root() noexcept;
    const Scene& root() const noexcept;

    // ------------------------------------------------------------------
    // 烘焙
    // ------------------------------------------------------------------
    /// 静态场景：展开映射表与平铺绘制表。动态场景：空操作。
    void bake();
    /// 标脏并一路标到根，下一次 bake() 重来。动态场景里是空操作。
    void invalidate();
    bool needsBake() const noexcept;
    bool isBaked() const noexcept;
    const HitTable& GetHitTable() const noexcept;

    // ------------------------------------------------------------------
    // 命中
    // ------------------------------------------------------------------
    /// 这个点上是哪个场景。静态场景查映射表，动态场景遍历子树。
    HitResult hitTest(float x, float y);

    // ------------------------------------------------------------------
    // 绘制
    // ------------------------------------------------------------------
    void draw(Canvas& canvas) const;
    /// 只画自己，不画子场景。
    virtual void onDraw(Canvas& canvas) const;

    // ------------------------------------------------------------------
    // 每帧
    // ------------------------------------------------------------------
    /// 窗口层每帧调一次。静态子树不接这个回调，只有动态子树收得到。
    void tick(float deltaSeconds);
    virtual void onTick(float deltaSeconds);

    // ------------------------------------------------------------------
    // 输入：默认都不处理，返回 false 让事件继续冒泡
    // ------------------------------------------------------------------
    virtual bool onPointerDown(PointerEvent& event);
    virtual bool onPointerUp(PointerEvent& event);
    virtual void onPointerEnter(PointerEvent& event);
    virtual void onPointerLeave(PointerEvent& event);
    virtual void onPointerCancel(PointerEvent& event);
    virtual bool onClick(PointerEvent& event);

    // ------------------------------------------------------------------
    // 窗口命令
    // ------------------------------------------------------------------
    /// 提一个窗口命令，冒泡到根场景排队，等窗口层取走执行。
    void requestWindowCommand(WindowCommand command);
    /// 取出一条待执行的窗口命令（窗口层在根场景上调用）。
    WindowCommand takeWindowCommand();
    /// 窗口最大化状态变了：默认下发给所有子场景。
    virtual void onWindowStateChanged(bool maximized);

    /// 平铺绘制表里的一项：recursive 表示这棵子树是动态的，要递归画。
    struct DrawItem {
        const Scene* node = nullptr;
        bool recursive = false;
    };

private:
    void addOwned(std::unique_ptr<Scene> child);
    void collect(std::vector<HitEntry>& entries,
                 std::vector<HitEntry>& holes,
                 std::vector<DrawItem>& drawList);
    HitResult hitTestDynamic(float x, float y);

    std::string _name;
    Rect        _rect;
    bool        _dynamic = false;
    bool        _visible = true;
    int         _order = 0;
    Scene*      _parent = nullptr;

    std::vector<Scene*>                 _children;  ///< 按 order 升序，后建的排后面
    std::vector<std::unique_ptr<Scene>> _owned;     ///< make() 建出来的孩子
    std::vector<WindowCommand>          _commands;  ///< 根场景上的待办窗口命令

    HitTable              _hitTable;      ///< 静态场景的坐标 → UI 映射表
    std::vector<DrawItem> _drawList;      ///< 静态场景的平铺绘制表
    std::vector<HitEntry> _dynamicHoles;  ///< 静态场景里那些"会动的洞"
    bool _baked = false;
    bool _dirty = true;
};

}  // namespace ink
