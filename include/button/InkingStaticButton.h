#pragma once

// 这里是 InkingStaticButton —— 项目里第一个**完整组件**，也是"组件该怎么写"
// 这套框架的样板。
//
// 作为静态 Button，全部可配置内容写在 CXXCSS/Button/<名字>.json 里
// （模板见 CXXCSS/Button/button.example.json）：名称、设计尺寸、形状层、
// 自身锚点与上级锚点、三态外观、文字。
// 代码生成器会把**每个独立名字**的 Button 展开成一个独立的新类并替换掉泛型
// 实现，所以名字必须与配置文件、代码里的用法三处一致。生成器落地之前，
// 手写这条路走 `ButtonData`（见下"两种构造"）。
//
// 用法：
//     ink::InkingStaticButton button(parent, data);      // 手写数据
//     ink::InkingStaticButton button(parent, "name");    // 按名字从 ButtonLibrary 取
//     button.SetOnClicked([] { ... });
//
// ---------------------------------------------------------------------------
// 为什么是**静态**组件（这个选择不能回头改，所以把理由写在这里）
//
// 判据只有一条：几何会不会在构造之后自己变（InkingAnchor.h 末尾那段）。
//
//   - 常 / 悬 / 按三态**只换颜色**，而颜色不标脏（docs/API.md「标脏」）；
//   - 按下如果要做"下沉"效果，那是**绘制偏移**（onRender 里把 y 挪几像素），
//     不是几何变化——命中区域不动，所以也不需要动态组件；
//   - 静态组件没有几何写入口，拿到 `InkingAnchor*` 也改不了它的几何。
//
// 结论：三态外观 + 点击回调全部落在静态档，进命中表那条路就一直是通的。
// 真要一个"按下会改变尺寸、会撑开布局"的按钮，那是另一个类型——它已经存在了：
// `InkingDynamicButton`（include/button/InkingDynamicButton.h），几何可以运行期改，
// 代价是不进命中表、指针由它自己在 Tick 里拉。**不要在这里加几何写入口**
// 把静态按钮变回动态：`src/ink.cpp` 里有编译期断言钉着这条。
//
// ---------------------------------------------------------------------------
// 三态外观与状态机是**静态 / 动态共用**的一份（include/button/ButtonLook.h）
//
// 两条路的输入方式不同（静态是场景派发喂、动态是自己拉），但"哪个状态配哪份
// 外观""按下 > 悬停 > 通常"只允许有一份定义，所以挪进了 ButtonLook。
// 本类只负责"什么时候把两个标志填进去"——那是场景派发的事。
//
// ---------------------------------------------------------------------------
// 状态是本组件自己的事，不是"每个按钮都要转发一遍鼠标"
//
// 三态是**渲染状态**，所以状态机放在按钮里；命中判定走形状层同一份定义
// （`ShapeContains`，见 GetShape），所以"画的是圆角、点到的是直角"不会发生。
//
// 对外提供的是**幂等的状态写入口**——谁拿到鼠标谁调。今天调它们的是
// `InkingScene::DispatchPointer`（线性扫绘制列表，从最上面往下问 HitTest，
// 然后推给按钮），所以业务代码一般不用自己调：
//
//     button.MouseHover(true/false);   // 指针进出
//     button.MousePress(true);         // 按下
//     button.MouseRelease();           // 抬起（回哪一态问按钮自己）
//     button.TriggerClick();           // 抬起且仍在按钮内 → 回调
//
// 它们只换颜色、不标脏、也不通知场景——这正是"静态组件"该有的开销。
//
// 动态按钮（`InkingDynamicButton`）**没有**这三个入口：它的输入是自己从场景
// 拉的（`JudgePointer`）。所以"输入与静态版不同"那句话，落到接口上就是
// "一边有这三个推的入口、另一边只有一个拉的入口"。

#include <button/ButtonLook.h>
#include <ink/basic/InkingAnchor.h>

#include <cstdint>
#include <functional>
#include <string>

namespace ink {

/// 点击回调（`ButtonClickCallback`）与三态（`ButtonState`）都在 ButtonLook.h 里：
/// 静态 / 动态两块按钮共用同一份定义，不能各写一套。

class InkingStaticButton : public InkingStaticAnchor {
public:
    /**
     * 手写数据构造：不走配置，直接把一份 ButtonData 交给它。
     *
     * @param parent 上级锚点；非拥有。按钮必须在某个场景的子树下才能被渲染。
     * @param data   配置数据（构造时会 Normalize 一次，补齐省略的三态）。
     */
    InkingStaticButton(InkingAnchor* parent, const ButtonData& data);

    /**
     * 配置驱动构造：只给 parent + 名字，配置从 `ButtonLibrary` 里按名字取。
     *
     * **它不是生成的按钮类走的路**：生成器为每个名字生成一个独立的类，
     * 数据直接烘在类里（`ink::cxxcss::NormalButton` 之类），构造时不查任何表。
     * 这条构造留着给"名字要到运行期才知道"的少数场景用。
     *
     * 名字没登记时：写一条**错误**日志，给一个空按钮（不画东西、不响应点击），
     * 并让 `UnknownNameCount()` 加一。以前这里是"静默退回一份默认外观"——
     * 按钮长得不对却没有任何报错，只能靠肉眼发现；现在它是错的、也是可断言的。
     *
     * 注意构造完之后**再去登记是不会生效的**：数据在构造时就拷进来了。
     * 登记必须在所有按钮构造之前完成（生成代码里的启动期登记天然满足）。
     */
    InkingStaticButton(InkingAnchor* parent, const std::string& name);

    /**
     * 按名字从库里取配置。取不到返回 nullptr。
     *
     * 给"我想先看看配置里写了什么"或者"我要自己判断该不该建"这类场景用；
     * 按名字构造内部也走它，所以两者看到的永远是同一份数据。
     */
    static const ButtonData* FindData(const std::string& name);

    /**
     * 发生过多少次"按名字构造却查不到配置"。
     *
     * 每次发生都会写一条错误日志并给出一个空按钮。留这个计数是为了让这件事
     * **可断言**——静默失败才有藏身处，能被断言的失败没有。
     */
    static int UnknownNameCount() noexcept;

    /**
     * 指针在这个位置上吗。坐标是**世界坐标（设计空间）**。
     *
     * 重写的是基类的 `[√]` 钩子：世界坐标 → 本地坐标，再问形状层
     * （`ShapeContains`）——**和渲染用的是同一份形状定义**，所以不会出现
     * "画的是圆角、点到的是直角"。隐藏的按钮不参与命中（父链上任何一层
     * 不可见都算隐藏）。
     *
     * z 序不在这里判：那是调用方的事（`InkingScene::DispatchPointer`
     * 从绘制列表尾部往前问）。
     */
    bool HitTest(float worldX, float worldY) const override;

    // ---------------- 属性（运行期可改） ----------------

    /// 当前三态中的一个。初始是 Normal。
    ButtonState GetState() const noexcept;

    /// 形状定义：渲染 / 命中 / 包围盒三者都从它派生（见 InkingShapeSpec.h）。
    const ShapeSpec& GetShape() const noexcept;

    /**
     * 三态外观（按当前状态）。注意它是**目标**那一份配置（json 里写的），
     * 过渡进行中它与屏幕上正在显示的颜色不是一回事——要那个用 `GetDisplayColor()`。
     */
    const ButtonAppearance& GetAppearance() const noexcept;

    /**
     * 现在**实际画出来**的颜色（0xAARRGGBB）。
     *
     * 没有过渡在跑时它等于当前状态那份配置的颜色；过渡进行中是插值出来的中间色。
     * 渲染用的就是它，自检也拿它验证"过渡真的在跑"。
     */
    std::uint32_t GetDisplayColor() const noexcept;

    /**
     * 现在**实际生效**的变换（平移 / 旋转 / 缩放）。过渡进行中是插值出来的中间值。
     *
     * 绘制用它过正变换、命中用它过逆变换——自检拿它验证"几何动画真的在跑"，
     * 以及"画的和点的是同一份变换"。
     */
    const TransformSpec& GetDisplayTransform() const noexcept;

    /**
     * 命中索引用的**保守包络**：三态变换的并集（`docs/InputDesign.md` §11）。
     *
     * 命中表的格子登记是**冻结**的，而按钮的变换会随三态变（悬停平移+放大、
     * 按下下沉）——所以登记时必须盖住"所有可能的样子"，精判才用当前变换。
     * 只按当前变换登记会漏命中：悬停那一帧按钮挪出去一点，边缘的点就落在表外了。
     */
    ShapeBounds GetHitEnvelope() const override;

    /// 指定某一个状态的外观。默认值来自配置里的 normal / hover / onclicked。
    const ButtonAppearance& GetAppearance(ButtonState state) const noexcept;

    /// 构造时用的那份配置（已经 Normalize 过）。
    const ButtonData& GetData() const noexcept;

    /// 按钮上的文字。当前只认 `text.content.normal`（生成器会把它填进 label）。
    const std::string& GetText() const noexcept;

    /**
     * 设置文字。
     *
     * 与"不要在按钮里配文字"那条建议不冲突：**文字是运行期数据**，
     * 多语言就该在这里换（配置里的文字会被烘进纹理，换语言就废了）。
     * 文字不参与命中，所以这个写入口不标脏。
     */
    void SetText(const std::string& text);

    /// 点击回调挂上了吗。
    bool HasOnClicked() const noexcept;

    /**
     * 设置点击回调。原来骨架里的 `setOnClicked` 按 CodeStyleRule §5
     * 改成 PascalCase（写入口是 PascalCase，钩子才是 `on` 开头）。
     */
    void SetOnClicked(ButtonClickCallback callback);

    // ---------------- [final] 状态写入口（幂等，只换颜色） ----------------
    //
    // 名字用 MouseXxx 而不是 SetHover：一眼能看出"这是鼠标事件推过来的"，
    // 而不是某个业务状态。三个都不标脏、不通知场景——颜色不属于会标脏的
    // 四件事（几何 / 可见性 / 层级 / 鼠标移动位置）。

    /**
     * 指针进出按钮。
     *
     * 这个标志一直更新（即使正在按下）：它同时是"抬起时该回哪一态"的依据，
     * 而拖拽时指针滑出按钮再松开，本来就该回到 Normal 而不是 Hover。
     *
     * @return 三态真的变了才返回 true。
     */
    bool MouseHover(bool hover) noexcept;

    /**
     * 按下 / 抬起。
     *
     * `press(true)` 进入 Pressed；`press(false)` 按**当前**指针位置回到
     * Hover（还在里面）或 Normal（已经出去了）。所以框架层不需要自己记住
     * "抬起时该回到哪一态"——那份状态只有按钮自己知道。
     *
     * @return 三态真的变了才返回 true。
     */
    bool MousePress(bool press) noexcept;

    /**
     * 抬起。等价于 `MousePress(false)`，写成两个名字是因为调用方那两处
     * （按下事件 / 抬起事件）读起来更直白，也不会有人把参数写反。
     */
    bool MouseRelease() noexcept { return MousePress(false); }

    /**
     * 触发一次点击回调。
     *
     * 由框架在"抬起且指针仍在按钮内"时调；也可以直接调来模拟一次点击
     * （自检就是这么用的）。没有回调时是空操作，返回 false。
     *
     * @return 真的调用了回调才返回 true。
     */
    bool TriggerClick();

protected:
    /// 按状态选外观、按形状填色，再画上文字。
    ///
    /// 重写的是**钩子**（[√] onRender），不是写入口：基类给"一块实心矩形"
    /// 的默认实现，这里只换成"按形状的一笔"。
    void onRender(float pixelX, float pixelY) const override;

    /**
     * 每**渲染帧**一次：推进三态之间的颜色过渡。
     *
     * 静态按钮收不到 `InkingDynamicAnchor::Tick`（那是动态档的逻辑步），
     * 所以颜色过渡走这条独立通道（`InkingAnchor::TickAnimation` →
     * `InkingScene::TickFrame` 分发）。**它只改颜色，不碰几何**——这正是
     * 静态按钮能一边有动画、一边继续待在"进命中表"那一档的原因。
     */
    void onAnimationTick(float deltaSeconds) override;

    /// 三态外观的选择。放 protected 而不是匿名函数里：派生类（例如将来的
    /// 圆角带描边的按钮）要复用同一套选择规则，不能各写一份。
    /// 规则本体在 `ButtonLook::For`——这里只是个转发。
    const ButtonAppearance& appearanceFor(ButtonState state) const noexcept;

    /**
     * 按优先级从"指针在不在按钮里"和"按没按下"推出当前状态。
     *
     * 规则本体在 `ButtonLook::Resolve`（静态 / 动态共用）；这里留一个转发，
     * 是因为它以前就在这儿，派生类可能已经用上了。
     */
    static ButtonState ResolveState(bool hover, bool press) noexcept;

private:
    /// 把 data 里的字段搬到基类与自己的成员上；两个构造函数共用。
    void applyData(const ButtonData& data);

    /// 把 ButtonLook 里**当前显示**的颜色同步到基类那份 color：
    /// 从基类窗口看颜色时读到的得是同一件事（本类重写了 onRender，用不到它，
    /// 但一致性不能破）。过渡进行中同步的是插值出来的中间色。
    void syncBaseColor() noexcept;

    /**
     * 画文字。
     *
     * **现在还不是真的文字**：文字渲染要字形图集 + 纹理层，属于 `text` 那段
     * 尚未落地的工作。占位块的实现在 `src/button/ButtonPaint.cpp`，
     * 静态 / 动态按钮共用同一份（等字形图集接上，换那一个函数体即可）。
     */
    void renderText(float pixelX, float pixelY) const;

    /// 构造完成后的公共收尾：Normalize、建形状、按需打日志。
    void finishConstruction(bool foundInLibrary);

    ButtonData _data{};

    /// 三态外观 + 状态机（静态 / 动态共用的那一份，见 ButtonLook.h）。
    ButtonLook _look{};

    ButtonClickCallback _onClicked{};
};

}  // namespace ink
