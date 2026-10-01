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
// 真要一个"按下会改变尺寸、会撑开布局"的按钮，那是另一个类型
// （InkingDynamicButton），不要在这里加几何写入口把它变回动态。
//
// ---------------------------------------------------------------------------
// 状态是本组件自己的事，不是"每个按钮都要转发一遍鼠标"（设计要点，还没接线）
//
// 三态是**渲染状态**，所以状态机放在按钮里；命中判定走形状层同一份定义
// （`ShapeContains`，见 GetShape），所以"画的是圆角、点到的是直角"不会发生。
//
// 现在窗口层还没有把鼠标事件接到场景上（命中与三态是 TempTask 第 3 步），
// 所以对外提供的是**幂等的状态写入口**——谁拿到鼠标谁调，
// 框架接线时调的就是这三个，不用改本类：
//
//     button.MouseHover(true/false);   // 指针进出
//     button.MousePress(true);         // 按下
//     button.MouseRelease();           // 抬起（回哪一态问按钮自己）
//     button.TriggerClick();           // 抬起且仍在按钮内 → 回调
//
// 它们只换颜色、不标脏、也不通知场景——这正是"静态组件"该有的开销。

#include <button/ButtonData.h>
#include <ink/basic/InkingAnchor.h>

#include <cstdint>
#include <functional>
#include <string>

namespace ink {

/// 按钮的三态。顺序即优先级：按下 > 悬停 > 通常。
enum class ButtonState : std::uint8_t {
    Normal = 0,  ///< 通常
    Hover,       ///< 悬停
    Pressed,     ///< 按下（配置里叫 onclicked）
};

/// 点击回调：不带参数、不返回值。
///
/// 原来写的是 `std::function<std::any()>`（任意返回值），这里收窄成 `void`：
/// "按下之后要算出一个值" 不是按钮的职责——需要返回值就直接在 lambda 里捕获
/// 外部状态，或者把它包成一个 Task 交给 TaskQueue。`std::any` 还会逼每个
/// 调用点都去 `std::any_cast` 一个它根本不关心的东西。
using ButtonClickCallback = std::function<void()>;

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
     * 代码生成器将来生成的代码就是"登记配置 + 用这个名字构造"。名字没登记时
     * 会打一条警告并使用一份默认数据——按名字构造却什么都没登记，
     * 十有八九是名字拼错了，静默给个空按钮比报错更难查。
     *
     * 注意构造完之后**再去登记是不会生效的**：数据在构造时就拷进来了。
     * 登记必须在所有按钮构造之前完成（生成器生成的登记代码跑在启动期，
     * 天然满足）。
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

    /// 三态外观（按当前状态）；渲染取的就是这一份。
    const ButtonAppearance& GetAppearance() const noexcept;

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

    /// 三态外观的选择。放 protected 而不是匿名函数里：派生类（例如将来的
    /// 圆角带描边的按钮）要复用同一套选择规则，不能各写一份。
    const ButtonAppearance& appearanceFor(ButtonState state) const noexcept;

    /**
     * 按优先级从"指针在不在按钮里"和"按没按下"推出当前状态。
     *
     * 做成静态函数（而不是成员）是为了让派生类也能拿来推自己的状态；
     * 两个入参就是全部输入，没有隐藏状态。
     */
    static ButtonState ResolveState(bool hover, bool press) noexcept;

private:
    /// 把 data 里的字段搬到基类与自己的成员上；两个构造函数共用。
    void applyData(const ButtonData& data);

    /// 按形状画填充色（设备像素）。
    void renderShape(float pixelX, float pixelY) const;

    /**
     * 画文字。
     *
     * **现在还不是真的文字**：文字渲染要字形图集 + 纹理层，属于 `text` 那段
     * 尚未落地的工作。这里先按 fontSize / leftSpace / topSpace 占一块
     * **同尺寸的方块**，好处是三个间距参数和"文字区域在哪儿"现在就能被自检
     * 用像素钉住；等字形图集接上，把这个函数体换掉即可，调用点不用动。
     */
    void renderText(float pixelX, float pixelY) const;

    /// 构造完成后的公共收尾：Normalize、建形状、按需打日志。
    void finishConstruction(bool foundInLibrary);

    ButtonData _data{};

    /// 三态外观按状态展开存放，渲染时按当前状态直接取，
    /// 不在每帧去 if-else 拼一遍。
    ButtonAppearance _normal{};
    ButtonAppearance _hover{};
    ButtonAppearance _onclicked{};

    ButtonState _state = ButtonState::Normal;

    /// 指针现在在不在按钮里。它一直更新（即使正在按下）：
    /// 抬起时"该回 Hover 还是 Normal"问的就是它。
    bool _hovered = false;

    /// 现在按着没。与 _hovered 分开存：拖拽时"按着"和"在里面"是两件事。
    bool _pressed = false;

    ButtonClickCallback _onClicked{};
};

}  // namespace ink
