// 场景示例：**手写场景 + 生成出来的按钮类**。
//
// 这个示例最初是"连场景也交给生成器"（TaskGuide 的 T-7），后来评估下来回退了：
// 生成场景带来的两处收益手写都能拿到，而代价是固定的（见下面这段说明）。
//
//   - **组件不用传 parent** —— 靠的是"成员 + 默认成员初始化器 `{this}`"这个写法，
//     和谁写这几行无关。手写同样是 5 行，而且还能按条件建组件、能加自定义逻辑。
//   - **场景名有编译期保险** —— 一行 `kName` 常量就够；真正需要编译期保险的是
//     **组件类型名**，那份保险在生成的按钮类那一层已经拿到了
//     （`ink::cxxcss::NormalButton` 拼错就是编译错误）。
//
// 所以这里的分工是：**按钮类交给生成器**（数据烘成字面量、类型即名字、
// 构造不查表），**场景手写**（它是编排，不是数据）。
//
// 这个程序同时是集成自检：先在无渲染器的情况下把"结构 + 命中"断言一遍，
// 再开窗给人看。`INK_AUTOQUIT=1` 时窗口会自动关掉，退出码就是自检结果。

#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>
#include <ink/ink.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <string>

// ← inkgen 生成的**按钮类**（配置烘成字面量）。场景不生成，所以没有场景头。
#include <inkgen/scene_demo/button_service.h>

namespace {

/**
 * 整个场景——**这就是全部**，5 行。
 *
 * 两件事值得注意：
 *   1. `kName` 是编译期常量，登记进 `SceneLibrary` 用的就是它；
 *   2. 组件是成员，`{this}` 由默认成员初始化器填父级——少写一处样板，
 *      也不会有"忘了传 parent → 组件不登记 → 既不渲染也点不到"那种静默失败。
 *      成员在基类构造**之后**初始化，而那时场景已经盖过章了，
 *      所以父链查找直接就找得到它（`rebindEarlyChildren` 那趟用不上）。
 */
class MainScene : public ink::InkingScene {
public:
    static constexpr const char* kName = "mainScene";

    MainScene() : ink::InkingScene(kName) {}

    ink::cxxcss::NormalButton normalButton{this};
    ink::cxxcss::DynamicTestButton dynamicTestButton{this};
};

int gFailed = 0;
int gClicks = 0;

void check(bool ok, const std::string& what) {
    std::printf(ok ? "  [通过] %s\n" : "  [失败] %s\n", what.c_str());
    if (!ok) {
        ++gFailed;
    }
}

/// 在设计坐标 (x, y) 上，命中表给的是谁。
std::string WhoIsAt(ink::InkingScene& scene, float x, float y) {
    const ink::HitResult hit = scene.QueryHit(x, y);
    if (hit.kind == ink::HitKind::Miss || hit.target == nullptr) {
        return "（空）";
    }
    return hit.target->GetName();
}

}  // namespace

int main() {
    std::printf("场景示例自检（SDL3 %s）\n", ink::sdl3_version().c_str());

    // -----------------------------------------------------------------------
    // 1. 一行建出整个场景
    // -----------------------------------------------------------------------
    MainScene scene;

    check(std::string(MainScene::kName) == "mainScene",
          "场景名是编译期常量（一行，而且能 grep 到）");
    check(scene.IsRegistered(), "场景构造即登记（RAII 令牌，没有登记函数）");
    check(ink::SceneLibrary::FindScene("mainScene") == &scene,
          "按字面量名字找得到这个场景");
    check(ink::SceneLibrary::FindScene(MainScene::kName) == &scene,
          "按 kName 找的也是它（两处同源，不会拼错）");

    // -----------------------------------------------------------------------
    // 2. 组件是成员，父级由 `{this}` 填好了
    // -----------------------------------------------------------------------
    check(scene.GetDrawItemCount() == 2, "场景里有 2 个组件");
    check(scene.normalButton.GetParent() == &scene,
          "静态按钮的父级是场景（默认成员初始化器 `{this}` 填的）");
    check(scene.dynamicTestButton.GetParent() == &scene,
          "动态按钮的父级也是场景");
    check(scene.normalButton.GetWidth() == 200
              && scene.normalButton.GetHeight() == 100,
          "按钮的尺寸来自它自己的配置（生成器烘的，场景里不写尺寸）");
    check(scene.normalButton.GetName() == "normalButton"
              && scene.dynamicTestButton.GetName() == "dynamicTestButton",
          "两个组件的名字都没有张冠李戴");

    // 位置是组件自己的锚点算出来的：两边都是 selfAnchor=traceAnchor=0.5，
    // 所以左上角落在设计画布中心各偏半个身位的地方。
    //   公式：父级尺寸 × 上级锚点 − 自身尺寸 × 自身锚点 + 偏移
    //   静态 200×100：(960−100, 540−50) = (860, 490)
    //   动态 220×100：(960−110−320, 540−50) = (530, 490)   ← 它配置里有 offsetX −320
    check(scene.normalButton.GetAbsX() == 860.0f
              && scene.normalButton.GetAbsY() == 490.0f,
          "静态按钮落在配置指定的位置（860, 490）");
    check(scene.dynamicTestButton.GetAbsX() == 530.0f
              && scene.dynamicTestButton.GetAbsY() == 490.0f,
          "动态按钮落在自己的位置（530, 490，它的配置里有 offsetX -320）");

    // -----------------------------------------------------------------------
    // 3. 命中：手写场景照样能查（走的是 T-4 的烘焙表）
    // -----------------------------------------------------------------------
    check(WhoIsAt(scene, 960.0f, 540.0f) == "normalButton",
          "点静态按钮中心 → 命中它");
    // 动态组件不进表，只在表里留一个"洞"——所以这一条同时验了
    // "洞登记对了"和"静态与动态按 z 序合并"。
    check(WhoIsAt(scene, 640.0f, 540.0f) == "dynamicTestButton",
          "点动态按钮中心 → 命中它（靠动态洞 + z 序合并）");
    check(WhoIsAt(scene, 100.0f, 100.0f) == "（空）", "点空白 → Miss");

    // 点击真的到回调：这是"配置烘出来的组件也是真组件"的最后一环。
    scene.normalButton.SetOnClicked([] { ++gClicks; });
    scene.normalButton.TriggerClick();
    check(gClicks == 1, "点击回调真的挂上了（TriggerClick → 回调）");

    // -----------------------------------------------------------------------
    // 4. 开窗给人看
    // -----------------------------------------------------------------------
    ink::SceneLibrary::SetActiveScene(&scene);

    const bool autoQuit = SDL_getenv("INK_AUTOQUIT") != nullptr;
    if (autoQuit) {
        std::printf("\n失败项：%d\n", gFailed);
        // 自动跑时也要过一遍窗口主循环（否则"窗口那条路"没被验证）。
        // Show() 在 INK_AUTOQUIT 下会自己退出。
    }

    ink::InkingWindow::Instance().Show();

    if (autoQuit) {
        std::printf("窗口已关闭，按钮被点了 %d 次\n", gClicks);
    }
    return gFailed == 0 ? 0 : 1;
}
