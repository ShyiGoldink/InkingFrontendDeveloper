// T-6 验收：把 T-1…T-5 的功能**按任务编号**逐条过一遍。
//
// 与其它测试的分工（避免重复劳动）：
//
//   ink_test            框架层：锚点推导 / 场景登记 / 渲染树 / 时间线 / 标脏原语
//   ink_button_test     组件层：颜色 / 形状 / 三态 / 离屏像素（T-2 的像素对账、
//                       T-3 的 21 条标脏断言、T-4 的 24978 点逐点对账都在这里）
//   inkgen_test         生成器：每条报错 + 产物内容（含 T-1 的 `dynamic`、
//                       T-2 的 `transition` / `transform` 有没有烘成字面量）
//   ink_generated_test / buttons_demo / scene_demo   端到端（配置 → 窗口）
//   **本文件**          按任务串一遍：每条任务的**核心契约**一句话一条
//
// 这里**不建渲染器**（离屏像素那部分由 ink_button_test 覆盖，那是它的强项），
// 所以跑得很快——可以当作"改完东西先跑一遍"的冒烟验收。
// 每条失败信息都写得能直接定位到任务，看到红就知道是哪一轮的哪条契约破了。

#include <button/ButtonData.h>
#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>
#include <ink/ink.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailed = 0;

void check(bool ok, const std::string& what) {
    std::printf(ok ? "  [通过] %s\n" : "  [失败] %s\n", what.c_str());
    if (!ok) {
        ++gFailed;
    }
}

void group(const char* title) {
    std::printf("\n== %s ==\n", title);
}

class AcceptanceScene : public ink::InkingScene {
public:
    AcceptanceScene() : ink::InkingScene("Acceptance") {}
};

/// 一份**手写**的按钮配置（不依赖生成器：生成器那侧由 inkgen_test 覆盖）。
ink::ButtonData makeButton(float x, float y, int width, int height) {
    ink::ButtonData data;
    data.name = "probe";
    data.width = width;
    data.height = height;
    data.selfAnchor = ink::Anchor{0.0f, 0.0f};   // 左上角对上父级左上角
    data.traceAnchor = ink::Anchor{0.0f, 0.0f};
    data.offsetX = x;
    data.offsetY = y;

    // ↓ 这两行**必须有**：`hoverInheritsNormal` / `onclickedInheritsNormal`
    //   默认是 true（"省略即继承"），而构造按钮时会调一次 `Normalize()`——
    //   忘了置 false 的话，hover / onclicked 会被 normal **整个覆盖**：
    //   状态机照常工作、`GetState()` 照常说 Hover，但外观一个字节都没变。
    //   这个坑实测踩过（症状是"动画一动不动"，查了半天才发现配置被自己吞了）。
    data.hoverInheritsNormal = false;
    data.onclickedInheritsNormal = false;

    data.normal.color = 0x59FFFFFFu;             // 0.35 白
    data.hover.color = 0xD9FFFFFFu;              // 0.85 白
    data.onclicked.color = 0x59FFFFFFu;
    return data;
}

// ===========================================================================
// T-1 动态 Button：同一个组件的两档，差别在"几何能不能自己变"和"输入从哪来"
// ===========================================================================

void testT1() {
    group("T-1 动态 Button");

    AcceptanceScene scene;
    ink::SceneLibrary::SetActiveScene(&scene);

    // 两档的按钮默认都是"左上角对父级左上角"，所以这里**错开摆**：
    // 叠在一起的话，"谁被派发到"取决于 z 序（后构造的在上），
    // 那就测不到"静态被推、动态不被推"这件事了。
    const ink::ButtonData staticData = makeButton(100.0f, 100.0f, 200, 100);
    const ink::ButtonData dynamicData = makeButton(600.0f, 100.0f, 200, 100);
    ink::InkingStaticButton staticOne(&scene, staticData);
    ink::InkingDynamicButton dynamicOne(&scene, dynamicData);

    check(!staticOne.IsDynamic() && dynamicOne.IsDynamic(),
          "两档在类型上就分开了（IsDynamic 一眼可辨）");
    check(dynamicOne.GetParent() == &scene,
          "动态按钮照样登记进渲染树（父级是场景）");
    check(staticOne.GetShape().kind == dynamicOne.GetShape().kind
              && staticOne.GetWidth() == dynamicOne.GetWidth(),
          "两档读的是同一份数据（同一个 ButtonData / ButtonLook）");

    // 输入方式：动态是**拉**的，静态是**推**的。
    const float staticX = 200.0f;
    const float staticY = 150.0f;
    const float dynamicX = 700.0f;
    const float dynamicY = 150.0f;

    scene.SetPointerState(staticX, staticY, /*down=*/false, /*inside=*/true);
    scene.DispatchPointer();   // 只派发、不推逻辑步
    check(dynamicOne.GetState() == ink::ButtonState::Normal,
          "指针在别人的位置上：动态按钮纹丝不动");
    check(staticOne.GetState() == ink::ButtonState::Hover,
          "同一下派发：静态按钮已经变 Hover（推过去的）");

    scene.SetPointerState(dynamicX, dynamicY, /*down=*/false, /*inside=*/true);
    scene.DispatchPointer();   // 只派发、不推逻辑步
    check(dynamicOne.GetState() == ink::ButtonState::Normal,
          "指针挪到动态按钮上、只 DispatchPointer：它还是不动（三态不由派发推）");
    scene.TickLogic(1.0 / 50.0);
    check(dynamicOne.GetState() == ink::ButtonState::Hover,
          "推一个逻辑步之后，动态按钮才自己判出 Hover（拉过来的）");

    // 点击也走它自己的逻辑步。
    int clicks = 0;
    dynamicOne.SetOnClicked([&clicks] { ++clicks; });
    scene.SetPointerState(dynamicX, dynamicY, /*down=*/true, /*inside=*/true);
    scene.TickLogic(1.0 / 50.0);
    check(dynamicOne.GetState() == ink::ButtonState::Pressed,
          "按下也由它自己判（Pressed）");
    scene.SetPointerState(dynamicX, dynamicY, /*down=*/false, /*inside=*/true);
    scene.TickLogic(1.0 / 50.0);
    check(clicks == 1, "抬起时它自己触发点击回调");

    // 移出去：静态靠派发、动态靠逻辑步，两边都要回 Normal。
    scene.SetPointerState(-100.0f, -100.0f, /*down=*/false, /*inside=*/true);
    scene.DispatchPointer();
    scene.TickLogic(1.0 / 50.0);
    check(staticOne.GetState() == ink::ButtonState::Normal
              && dynamicOne.GetState() == ink::ButtonState::Normal,
          "移出去之后两档都回到 Normal");

    // 拖出去再松开算取消——两档都不能误触发。
    clicks = 0;
    scene.SetPointerState(dynamicX, dynamicY, /*down=*/true, /*inside=*/true);
    scene.DispatchPointer();
    scene.TickLogic(1.0 / 50.0);
    scene.SetPointerState(-100.0f, -100.0f, /*down=*/false, /*inside=*/true);
    scene.DispatchPointer();
    scene.TickLogic(1.0 / 50.0);
    check(clicks == 0, "按着拖出去再松开：算取消，不触发点击");
}

// ===========================================================================
// T-2 动画：颜色与变换按同一段时长插值，时长写在**进入**的那个状态上
// ===========================================================================

void testT2() {
    group("T-2 动画（颜色过渡 + 变换）");

    AcceptanceScene scene;
    ink::SceneLibrary::SetActiveScene(&scene);

    // 动画的**前提**：场景得登记上、而且是活跃的（TickFrame 只在活跃场景上跑）。
    // 这两条不是废话——上一轮测试的场景析构后名字要能空出来，
    // 否则这里会静默地拿不到"活跃"，动画一动不动。
    check(scene.IsRegistered(), "T-2 的场景登记上了（上一轮的名字已释放）");
    check(scene.IsActive(), "T-2 的场景是活跃的（TickFrame 才会跑）");

    ink::ButtonData data = makeButton(100.0f, 100.0f, 200, 100);
    data.normal.transitionSeconds = 0.4f;   // 退回慢
    data.hover.transitionSeconds = 0.2f;    // 进得快
    data.hover.transform = ink::TransformSpec{0.0f, 0.0f, 90.0f, 1.0f};
    ink::InkingStaticButton button(&scene, data);

    const std::uint32_t startColor = button.GetDisplayColor();

    // 动画能跑的三级前提，一级级验（这几条本身也值得是断言：
    // 动画"不动"时，先怀疑的就是这三条，而不是插值算法）。
    check(button.GetScene() == &scene, "按钮认得这个场景（进了渲染树）");
    check(scene.GetDrawItemCount() == 1,
          "按钮真的在绘制列表里（TickFrame 就是遍历它分发动画的；实得 "
              + std::to_string(scene.GetDrawItemCount()) + "）");
    check(button.MouseHover(true), "MouseHover 报告「三态真的变了」");
    check(button.GetState() == ink::ButtonState::Hover,
          "状态已经是 Hover（动画只是把值追过去）");

    // 推一半（0.2 的一半）：颜色与变换都该在中点。
    scene.TickFrame(0.1);
    const std::uint32_t midColor = button.GetDisplayColor();
    const float midRotate = button.GetDisplayTransform().rotate;
    check(midColor != startColor && midColor != data.hover.color,
          "推进一半：颜色是插值出来的中间色（不是起点也不是终点）");
    check(midRotate > 44.0f && midRotate < 46.0f,
          "变换与颜色**同一段时长**：一半时转到 45°（实得 "
              + std::to_string(midRotate) + "°）");

    // 推完：落到目标。
    scene.TickFrame(0.5);
    check(button.GetDisplayColor() == data.hover.color,
          "推完：颜色落到目标色");
    check(std::fabs(button.GetDisplayTransform().rotate - 90.0f) < 0.01f,
          "推完：变换落到 90°");
    check(button.GetState() == ink::ButtonState::Hover,
          "过渡跑完不影响状态机（状态早就切了，动画只是把值追过去）");

    // 退回用 normal 自己的时长（0.4），推 0.2 应该正好走一半。
    button.MouseHover(false);
    scene.TickFrame(0.2);
    const float halfBack = button.GetDisplayTransform().rotate;
    check(halfBack > 44.0f && halfBack < 46.0f,
          "退回用 normal 的时长（0.4s）：推 0.2 回到 45°（实得 "
              + std::to_string(halfBack) + "°）");

    // 没配 transition：当场到位，不需要推时间。
    ink::ButtonData instant = makeButton(400.0f, 100.0f, 200, 100);
    instant.hover.transform = ink::TransformSpec{0.0f, 0.0f, 30.0f, 1.0f};
    ink::InkingStaticButton snapped(&scene, instant);
    snapped.MouseHover(true);
    check(std::fabs(snapped.GetDisplayTransform().rotate - 30.0f) < 0.01f,
          "没配 transition：状态一切，值当场到位");

    // 驱动通道是**显式**的：不推 TickFrame，过渡就停在原地，不会自己往前走。
    // （上面那个 button 还有半段过渡没跑完——它停在 halfBack 这个角度上。）
    check(std::fabs(button.GetDisplayTransform().rotate - halfBack) < 0.01f,
          "不推 TickFrame：过渡停在原地（驱动通道是显式的）");

    // 决定性实验：**绕过场景分发**，直接推这个节点一帧。
    // 它能一刀切开"插值对不对"和"分发链通不通"这两件事。
    {
        ink::ButtonData direct = makeButton(800.0f, 100.0f, 200, 100);
        direct.hover.transitionSeconds = 0.2f;
        direct.hover.transform = ink::TransformSpec{0.0f, 0.0f, 90.0f, 1.0f};
        ink::InkingStaticButton directButton(&scene, direct);

        directButton.MouseHover(true);
        directButton.TickAnimation(0.1f);
        const float directRotate = directButton.GetDisplayTransform().rotate;
        check(directRotate > 44.0f && directRotate < 46.0f,
              "直接 TickAnimation(0.1)：转到 45°（插值本身没问题；实得 "
                  + std::to_string(directRotate) + "°）");
    }
}

// ===========================================================================
// T-3 标脏：三种脏走三条路（深度版在 ink_button_test，这里只钉最核心的三条）
// ===========================================================================

void testT3() {
    group("T-3 标脏协议");

    AcceptanceScene scene;
    ink::SceneLibrary::SetActiveScene(&scene);

    ink::ButtonData data = makeButton(100.0f, 100.0f, 200, 100);
    data.hover.transitionSeconds = 0.2f;
    data.hover.transform = ink::TransformSpec{0.0f, 0.0f, 45.0f, 1.0f};
    ink::InkingStaticButton button(&scene, data);

    for (int i = 0; i <= ink::InkingAnchor::kRepaintQuietFrames; ++i) {
        scene.ConsumeFrameDirty();
    }
    const std::uint64_t generation = scene.GetStructureGeneration();
    check(!button.IsRepaintDirty() && !button.IsDirty(),
          "静置几帧之后，按钮不再是脏的（脏会自己清）");

    // 动画每帧都在动：重绘脏 + 命中脏（变换改变了命中答案），但**不碰结构**。
    button.MouseHover(true);
    scene.TickFrame(0.05);
    check(button.IsRepaintDirty(), "动画推进一帧 → 重绘脏");
    check(button.IsDirty(), "变换在变 → 命中脏（同一个点可能落到别的组件上）");
    check(scene.GetStructureGeneration() == generation,
          "动画**不惊动结构**（惊动了就是每帧白重建一次绘制列表）");

    scene.TickFrame(0.5);
    for (int i = 0; i < ink::InkingAnchor::kRepaintQuietFrames; ++i) {
        scene.ConsumeFrameDirty();
    }
    check(!button.IsRepaintDirty(), "连续 5 帧没有新脏 → 重绘脏自清");
    check(scene.GetRepaintingNodeCount() == 0, "场景报告「画面稳了」");

    // 几何写入口是另一条路：结构脏 + 重绘脏。
    ink::AnchorData moverData;
    moverData.selfAnchor = ink::Anchor{0.0f, 0.0f};
    moverData.traceAnchor = ink::Anchor{0.0f, 0.0f};
    moverData.offsetX = 1200.0f;
    moverData.offsetY = 100.0f;
    moverData.width = 80;
    moverData.height = 40;
    ink::InkingDynamicAnchor mover(&scene, moverData);
    for (int i = 0; i <= ink::InkingAnchor::kRepaintQuietFrames; ++i) {
        scene.ConsumeFrameDirty();
    }
    const std::uint64_t beforeMove = scene.GetStructureGeneration();
    mover.Resize(60, 40);
    check(scene.GetStructureGeneration() > beforeMove,
          "几何变化 → 结构脏（帧首要重算坐标快照）");
    check(mover.IsRepaintDirty(), "几何变化 → 重绘脏（画面也变了）");

    // 指针脏：只有真的变了才算。
    scene.SetPointerState(500.0f, 500.0f, false, true);
    check(scene.IsPointerDirty(), "指针动了 → 指针脏");
    scene.ConsumeFrameDirty();
    check(!scene.IsPointerDirty(), "帧末消费一次就清掉");
    scene.SetPointerState(500.0f, 500.0f, false, true);
    check(!scene.IsPointerDirty(), "原地喂同一个坐标不算脏（窗口层每帧都喂）");
}

// ===========================================================================
// T-4 命中：烘焙表 + 三态 + 动态洞（24978 点的深度对账在 ink_button_test）
// ===========================================================================

void testT4() {
    group("T-4 命中表 + 三态");

    AcceptanceScene scene;
    ink::SceneLibrary::SetActiveScene(&scene);

    // A：普通静态；B：压在 A 上；C：让过（PassThrough）；D：被 C 盖住；
    // E：隐藏；F：动态（不进表，只留洞）。
    ink::ButtonData dataA = makeButton(100.0f, 100.0f, 200, 100);
    dataA.zIndex = 0;
    ink::InkingStaticButton a(&scene, dataA);

    ink::ButtonData dataB = makeButton(200.0f, 150.0f, 200, 100);
    dataB.zIndex = 1;
    ink::InkingStaticButton b(&scene, dataB);

    ink::ButtonData dataC = makeButton(100.0f, 400.0f, 200, 100);
    dataC.zIndex = 5;
    ink::InkingStaticButton c(&scene, dataC);
    c.SetHitPassThrough(true);

    ink::ButtonData dataD = makeButton(100.0f, 400.0f, 200, 100);
    ink::InkingStaticButton d(&scene, dataD);

    ink::ButtonData dataE = makeButton(600.0f, 500.0f, 200, 100);
    ink::InkingStaticButton e(&scene, dataE);
    e.SetVisible(false);

    ink::ButtonData dataF = makeButton(900.0f, 200.0f, 200, 100);
    dataF.zIndex = 3;
    ink::InkingDynamicButton f(&scene, dataF);

    check(scene.GetHitTableEntryCount() == 4,
          "表里只装**静态可见**的组件（A/B/C/D，隐藏的 E 与动态的 F 不算；实得 "
              + std::to_string(scene.GetHitTableEntryCount()) + "）");
    check(scene.GetHitTableClusterCount() >= 1 && scene.GetHitTableCellCount() > 0,
          "表建出了簇与格子（聚类索引真的在）");

    // 三态
    const ink::HitResult block = scene.QueryHit(250.0f, 200.0f);
    check(block.kind == ink::HitKind::Block && block.target == &b,
          "重叠区取 z 更高的那个（Block）");
    const ink::HitResult through = scene.QueryHit(150.0f, 450.0f);
    check(through.kind == ink::HitKind::Block && through.target == &d,
          "PassThrough 让过：点 C 穿到了下面的 D");
    const ink::HitResult miss = scene.QueryHit(1800.0f, 1000.0f);
    check(miss.kind == ink::HitKind::Miss && miss.target == nullptr,
          "空白是 Miss");
    const ink::HitResult hidden = scene.QueryHit(700.0f, 550.0f);
    check(hidden.kind == ink::HitKind::Miss,
          "隐藏的组件既命不中也留不下洞");

    // 动态：靠"洞 + 按 z 序合并"参与
    const ink::HitResult dynamicHit = scene.QueryHit(1000.0f, 250.0f);
    check(dynamicHit.target == &f, "动态组件照样能被查到（洞 + z 序合并）");

    // 轻量对账：表 vs 线性扫（深度版在 ink_button_test，这里只扫一片）
    {
        int mismatch = 0;
        int checked = 0;
        const std::vector<ink::InkingScene::DrawItem>& items =
            scene.BuildDrawList();
        const auto linear = [&items](float x, float y) -> ink::InkingAnchor* {
            ink::InkingAnchor* passTarget = nullptr;
            for (std::size_t i = items.size(); i > 0; --i) {
                ink::InkingAnchor* node = items[i - 1].node;
                if (node == nullptr || !node->IsVisibleInTree()) {
                    continue;
                }
                if (!node->HitTest(x, y)) {
                    continue;
                }
                if (node->IsHitPassThrough()) {
                    if (passTarget == nullptr) {
                        passTarget = node;
                    }
                    continue;
                }
                return node;
            }
            return passTarget;
        };

        for (int y = 60; y <= 700; y += 17) {
            for (int x = 60; x <= 1200; x += 17) {
                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);
                ++checked;

                const ink::InkingAnchor* tableHit = scene.QueryHit(fx, fy).target;
                const ink::InkingAnchor* linearHit = linear(fx, fy);

                // 动态层：QueryHit 只在"点在洞里"时问它，对账时补上同一套合并。
                if (f.HitTest(fx, fy)
                    && (linearHit == nullptr
                        || ink::InkingAnchor::IsAbove(f, *linearHit))) {
                    linearHit = &f;
                }
                if (tableHit != linearHit) {
                    ++mismatch;
                }
            }
        }
        std::printf("      [实测] 表 vs 线性扫：查了 %d 个点，不一致 %d 个\n",
                    checked, mismatch);
        check(mismatch == 0, "表查出来的目标和线性扫一致（换实现没换语义）");
    }

    // 变换不触发重烘，但命中跟着走。
    {
        ink::ButtonData spin = makeButton(400.0f, 800.0f, 200, 100);
        spin.hover.transitionSeconds = 0.1f;
        spin.hover.transform = ink::TransformSpec{0.0f, 0.0f, 90.0f, 1.0f};
        ink::InkingStaticButton s(&scene, spin);

        const std::size_t entries = scene.GetHitTableEntryCount();
        s.MouseHover(true);
        scene.TickFrame(0.5);
        check(scene.GetHitTableEntryCount() == entries,
              "变换不触发重烘（条目数没变——包络是保守的、冻结的）");
        check(scene.QueryHit(500.0f, 850.0f).target == &s,
              "旋转 90° 之后照样点得到（精判用当前变换）");
    }
}

// ===========================================================================
// T-5 形状：命中就是形状忠实的（填充与命中同源；像素那部分在 ink_button_test）
// ===========================================================================

void testT5() {
    group("T-5 形状层（命中按形状）");

    AcceptanceScene scene;
    ink::SceneLibrary::SetActiveScene(&scene);

    ink::ButtonData rounded = makeButton(100.0f, 100.0f, 200, 100);
    rounded.shape = ink::ShapeSpec::RoundedRect(30.0f);
    ink::InkingStaticButton roundButton(&scene, rounded);

    // 圆角被磨掉的地方**不该**命中——这就是"命中按形状、不是按包围盒"。
    check(roundButton.HitTest(150.0f, 150.0f), "中心命中");
    check(!roundButton.HitTest(102.0f, 102.0f),
          "30px 圆角的那个角**不命中**（命中不是按包围盒）");

    // 圆：AABB 比整块矩形小，且四角不命中。
    ink::ButtonData circular = makeButton(500.0f, 100.0f, 200, 200);
    circular.shape = ink::ShapeSpec::Circle();
    ink::InkingStaticButton circleButton(&scene, circular);
    const ink::ShapeBounds circleBounds
        = ink::GetShapeBounds(circleButton.GetShape(), 200, 200);
    check(circleBounds.width == 200.0f && circleBounds.height == 200.0f,
          "宽高相等时圆心直径取 min(w,h)（这里 200）");
    check(circleButton.HitTest(600.0f, 200.0f), "圆心命中");
    check(!circleButton.HitTest(505.0f, 105.0f), "圆的左上角（在方形内）不命中");

    // 半径超过 min(w,h)/2：配置值**原样留着**（不在这里改用户的配置），
    // 但行为按夹取后的算。验行为比验字段稳——字段是配置回声，行为才是契约。
    ink::ButtonData tooBig = makeButton(100.0f, 700.0f, 200, 100);
    tooBig.shape = ink::ShapeSpec::RoundedRect(1000.0f);
    ink::InkingStaticButton clamped(&scene, tooBig);
    check(clamped.GetShape().radius > 100.0f,
          "半径超上限时配置值原样保留（不在形状定义里改用户的数）");
    check(clamped.HitTest(200.0f, 750.0f), "夹取之后中心仍然命中");
    check(!clamped.HitTest(102.0f, 702.0f),
          "夹取到 50 的圆角：角落里不命中（行为按夹取后的算）");
}

}  // namespace

int main() {
    std::printf("T-6 验收：T-1…T-5 的功能逐条过一遍（SDL3 %s）\n",
                ink::sdl3_version().c_str());

    testT1();
    testT2();
    testT3();
    testT4();
    testT5();

    std::printf("\n失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
