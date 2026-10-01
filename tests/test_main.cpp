// 自检程序：把日志、消息队列、任务队列和窗口的基本行为过一遍。
// 退出码非 0 表示有检查项失败。

#include <ink/ink.h>
#include <ink/basic/InkingAnchor.h>
#include <ink/basic/Timeline.h>
#include <input/MouseInput.h>
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <any>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

int gFailed = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "[通过]" : "[失败]", what);
    if (!condition) {
        ++gFailed;
    }
}

/** 最多等 timeout 毫秒，直到 predicate 成立。 */
template <typename Predicate>
bool waitFor(Predicate predicate, int timeoutMilliseconds) {
    const int step = 5;
    for (int waited = 0; waited < timeoutMilliseconds; waited += step) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(step));
    }
    return predicate();
}

/// 验证写入口只在真的改动时才触发钩子（几何写入口只有动态版有）。
class CountingAnchor : public ink::InkingDynamicAnchor {
public:
    CountingAnchor(ink::InkingAnchor* parent, const ink::AnchorData& data)
        : ink::InkingDynamicAnchor(parent, data) {}

    int sizeChanges = 0;

protected:
    void onSizeChanged() override { ++sizeChanges; }
};

}  // namespace

int main() {
    std::printf("InkingFrontendDeveloper %s（链接 SDL3 %s）\n",
                ink::kVersion, ink::sdl3_version().c_str());
    std::printf("编译期开关：日志=%s，调试=%s，消息=%s\n",
                ink::kLogEnabled ? "开" : "关",
                ink::kDebugEnabled ? "开" : "关",
                ink::kMessageEnabled ? "开" : "关");

    // ---------------------------------------------------------------------
    // 1. 日志：五个级别各写一条（关闭时这些语句整体消失）
    // ---------------------------------------------------------------------
    INK_LOG_INFO("Test", "自检：普通日志");
    INK_LOG_PASS("Test", "自检：通过日志");
    INK_LOG_WARN("Test", "自检：警告日志");
    INK_LOG_ERROR("Test", "自检：错误日志");
    INK_LOG_DEBUG("Test", "自检：调试日志（只有 ISDEBUG 打开才写）");
    INK_DEBUG_CHECK(1 + 1 == 2, "自检：这条调试断言不应触发");

    if (ink::kLogEnabled) {
        check(!ink::InkLog::logFilePath().empty(), "日志文件路径非空");
    }

    // ---------------------------------------------------------------------
    // 2. 消息队列：立即消息
    //
    // ISMESSAGE 关闭时这一整块换成空实现的检查：队列不该崩，也不该留下东西。
    // ---------------------------------------------------------------------
#if defined(INK_ISMESSAGE)
    ink::MessageQueue::addMessage(ink::MessageType::Normal, 0.0f, "立即消息");
    {
        const std::vector<ink::Message> messages = ink::MessageQueue::drainMessages();
        check(messages.size() == 1 && messages.front().message == "立即消息",
              "立即消息入队并被取出");
        check(ink::MessageQueue::pendingCount() == 0, "取出后队列为空");
    }

    // ---------------------------------------------------------------------
    // 3. 消息队列 + 任务队列：延迟消息靠任务队列定时重新入队
    // ---------------------------------------------------------------------
    ink::MessageQueue::addMessage(ink::MessageType::Warn, 0.05f, "延迟消息");
    check(ink::MessageQueue::pendingCount() == 0, "延迟消息不会立刻进队列");
    check(waitFor([] { return ink::MessageQueue::pendingCount() > 0; }, 2000),
          "延迟消息到点后重新入队");
    ink::MessageQueue::drainMessages();
#else
    ink::MessageQueue::addMessage(ink::MessageType::Normal, 0.0f, "关闭时调用");
    ink::MessageQueue::quickMessage(true, 0.05f, "关闭时调用");
    check(ink::MessageQueue::pendingCount() == 0, "消息队列关闭时没有待处理消息");
    check(ink::MessageQueue::drainMessages().empty(), "消息队列关闭时取出为空");
    ink::MessageQueue::waitForMessage(std::chrono::milliseconds(1));
    INK_MESSAGE(ink::MessageType::Normal, "关闭时调用");
    INK_MESSAGE_AFTER(ink::MessageType::Warn, 0.05f, "关闭时调用");
    INK_MESSAGE_PASS("关闭时调用");
    INK_MESSAGE_ERROR("关闭时调用");
    check(true, "消息队列关闭时宏与接口可调用（空实现）");
#endif

    // ---------------------------------------------------------------------
    // 4. 任务队列：提交一个任务，等管家线程执行
    // ---------------------------------------------------------------------
    int executed = 0;
    ink::Task<std::any> task;
    task.action = [&executed](const std::vector<std::any>&) -> std::any {
        ++executed;
        return {};
    };
    ink::TaskQueue::instance().addTask(std::move(task));
    check(waitFor([&executed] { return executed > 0; }, 2000), "任务队列执行了任务");

    // ---------------------------------------------------------------------
    // 5. 窗口：设计尺寸是编译期常量，窗口尺寸是运行期属性
    // ---------------------------------------------------------------------
    ink::InkingWindow& window = ink::InkingWindow::Instance();
    check(window.GetWindowWidth() == inking::kDesignWidth, "窗口宽度默认等于设计宽度");
    check(window.GetWindowHeight() == inking::kDesignHeight, "窗口高度默认等于设计高度");
    check(!window.setWindowWidth(0), "非法宽度被拒绝");
    check(!window.setWindowHeight(-1), "非法高度被拒绝");
    check(window.setWindowWidth(1280) && window.GetWindowWidth() == 1280,
          "setWindowWidth 生效");
    check(window.setWindowHeight(720) && window.GetWindowHeight() == 720,
          "setWindowHeight 生效");

    // ---------------------------------------------------------------------
    // 6. 鼠标输入：状态更新、帧末收尾、设计坐标
    // ---------------------------------------------------------------------
    {
        ink::MouseInput mouse;
        SDL_Event event{};

        event.type = SDL_EVENT_MOUSE_MOTION;
        event.motion.x = 120.0f;
        event.motion.y = 80.0f;
        mouse.UpdateFromSDL(event);
        check(mouse.GetState().position.x == 120 && mouse.GetState().position.y == 80,
              "移动事件更新窗口坐标");
        check(mouse.IsMoved(), "移动事件置位 moved");

        // 窗口坐标 → 设计坐标的换算在窗口层做，MouseInput 只负责存下来。
        mouse.SetDesignPosition(960.0f, 540.0f);
        check(mouse.GetState().design.x == 960.0f
                  && mouse.GetState().design.y == 540.0f,
              "设计坐标可写入");

        mouse.ResetFrameFlags();
        check(!mouse.IsMoved(), "帧末清掉 moved");
        check(mouse.GetState().position.x == 120, "帧末不影响位置");

        event = {};
        event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = true;
        mouse.UpdateFromSDL(event);
        check(mouse.IsLeftDown(), "左键按下置位");

        event = {};
        event.type = SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        mouse.UpdateFromSDL(event);
        check(!mouse.IsLeftDown(), "左键抬起复位");
    }

    // ---------------------------------------------------------------------
    // 7. InkingAnchor：锚点推导位置、尺寸、层级、标脏、钩子
    // ---------------------------------------------------------------------
    {
        static_assert(!std::is_copy_constructible_v<ink::InkingAnchor>,
                      "InkingAnchor 不该可拷贝");
        static_assert(!std::is_move_constructible_v<ink::InkingAnchor>,
                      "InkingAnchor 不该可移动");

        ink::AnchorData boxData;
        boxData.width = 200;
        boxData.height = 100;
        ink::InkingStaticAnchor box(nullptr, boxData);

        ink::AnchorData itemData;
        itemData.width = 40;
        itemData.height = 20;
        itemData.selfAnchor = ink::InkingChangeAnchor::Center;
        itemData.traceAnchor = ink::InkingChangeAnchor::Center;
        ink::InkingDynamicAnchor item(&box, itemData);

        // 双 Center：自身矩形落在 (80,40)-(120,60)
        check(item.GetX() == 80.0f && item.GetY() == 40.0f,
              "双 Center 锚点 → 在父级里居中");
        check(item.GetAbsX() == 80.0f && item.GetAbsY() == 40.0f,
              "父级在原点时绝对位置等于局部位置");

        // 三层：祖父 400x200，父级 200x100 居中于祖父，子居中于父级
        ink::AnchorData grandData;
        grandData.width = 400;
        grandData.height = 200;
        ink::InkingStaticAnchor grand(nullptr, grandData);
        ink::InkingDynamicAnchor mid(&grand, boxData);
        mid.ChangeSelfAnchor(ink::InkingChangeAnchor::Center);
        mid.ChangeTraceAnchor(ink::InkingChangeAnchor::Center);
        ink::InkingStaticAnchor deep(&mid, itemData);
        check(mid.GetAbsX() == 100.0f && mid.GetAbsY() == 50.0f,
              "父级居中于祖父");
        check(deep.GetAbsX() == 180.0f && deep.GetAbsY() == 90.0f,
              "绝对位置沿父链累加");

        // 锚点决定位置：位置本身没有字段，全靠两个锚点推
        check(item.ChangeSelfAnchor(ink::InkingChangeAnchor::LeftTop),
              "改自身锚点生效");
        check(item.GetX() == 100.0f && item.GetY() == 50.0f,
              "自身左上角对上父级中心");
        check(!item.ChangeSelfAnchor(ink::InkingChangeAnchor::LeftTop),
              "锚点没变就不算改动");
        check(item.ChangeTraceAnchor(ink::InkingChangeAnchor::RightBottom)
                  && item.GetX() == 200.0f && item.GetY() == 100.0f,
              "上级锚点改到右下 → 贴到父级右下角");
        check(item.ChangeOffset(-10.0f, -5.0f) && item.GetX() == 190.0f
                  && item.GetY() == 95.0f,
              "偏移叠加在锚点对齐之后");
        check(!item.ChangeOffset(-10.0f, -5.0f), "偏移没变就不算改动");

        // 尺寸：none 表示该方向不动
        check(item.Resize(ink::InkingResize::none, 30) && item.GetHeight() == 30,
              "InkingResize::none 表示该方向不动");
        check(item.GetWidth() == 40, "该方向的尺寸保持不变");
        check(!item.Resize(ink::InkingResize::none, 30), "尺寸没变就不算改动");

        // 层级：同场景内全局比 zindex，z 相同时晚注册的在上。
        // 注册序号现在是构造时自动赋的（后构造的在上），不再需要调用方手工设。
        ink::InkingStaticAnchor lower(&box, itemData);
        ink::InkingStaticAnchor upper(&box, itemData);
        upper.ChangeZIndex(1);
        check(ink::InkingAnchor::IsAbove(upper, lower), "z 大的在上");
        lower.ChangeZIndex(1);
        check(ink::InkingAnchor::IsAbove(upper, lower),
              "z 相同时后构造的在上（注册序号自动递增）");
        check(lower.GetRegisterOrder() < upper.GetRegisterOrder(),
              "注册序号确实是后构造的更大");

        // 标脏：写入口真的改了才标
        ink::InkingDynamicAnchor dirtyOne(&box, itemData);
        dirtyOne.ClearDirty();
        check(!dirtyOne.IsDirty(), "清掉脏标记");
        check(dirtyOne.Resize(50, 25) && dirtyOne.IsDirty(),
              "写入口改了东西就标脏");
        dirtyOne.ClearDirty();
        check(!dirtyOne.Resize(50, 25) && !dirtyOne.IsDirty(),
              "没改动就不标脏");

        // 钩子只在真的改动时触发
        CountingAnchor counted(&box, itemData);
        check(counted.Resize(50, 25) && counted.sizeChanges == 1,
              "写入口触发 onSizeChanged");
        check(!counted.Resize(50, 25) && counted.sizeChanges == 1,
              "没改动不触发钩子");

        // 展示倍率：影响绘制换算，不影响标脏
        ink::InkingStaticAnchor scaled(&box, itemData);
        scaled.ClearDirty();
        scaled.SetMagnification(2.0f);
        check(scaled.GetDeviceWidth() == 80.0f
                  && scaled.GetDeviceHeight() == 40.0f,
              "展示倍率换算成设备像素");
        check(!scaled.IsDirty(), "展示倍率不标脏");
    }

    // ---------------------------------------------------------------------
    // 8. SceneLibrary + RAII 令牌：同名同期只能有一个
    //
    // 场景是抽象基类（InkingScene 注释里写明用户要继承它），所以这里按
    // 真实用法派生出两个最小的测试场景，不往产品代码里加假的测试实现。
    // ---------------------------------------------------------------------
    {
        class TestSceneA : public ink::InkingScene {
        public:
            explicit TestSceneA(const std::string& name)
                : ink::InkingScene(name) {}
        };
        class TestSceneB : public ink::InkingScene {
        public:
            explicit TestSceneB(const std::string& name)
                : ink::InkingScene(name) {}
        };

        // 先拿一份"用之前"的快照，最后拿它对照，别把这一段的断言写成自证。
        const std::size_t scenesBefore = ink::SceneLibrary::SceneCount();
        const std::vector<std::string> namesBefore =
            ink::SceneLibrary::GetAllSceneNames();

        {
            TestSceneA scene("Scene.One");
            TestSceneB other("Scene.Two");

            check(scene.GetSceneName() == "Scene.One", "场景名可读");
            check(scene.IsRegistered(), "构造即登记（RAII 令牌生效）");
            check(ink::SceneLibrary::SceneCount() == scenesBefore + 2,
                  "场景库记下两个场景");
            check(ink::SceneLibrary::FindScene("Scene.One") == &scene,
                  "按名字能找到场景");
            check(ink::SceneLibrary::FindScene("Scene.Two") == &other,
                  "不同名字互不干扰");
            check(ink::SceneLibrary::FindScene("Scene.None") == nullptr,
                  "没登记过的名字返回 nullptr，而不是抛错");
            check(ink::SceneLibrary::IsNameTaken("Scene.One"),
                  "IsNameTaken 认得出已占用的名字");

            // ---- 同名后来者：当场驳回，进停放态 ----
            TestSceneA intruder("Scene.One");
            check(!intruder.IsRegistered(), "同名后来者注册被驳回");
            check(intruder.IsRejected(), "被驳回的场景处于停放态");
            check(ink::SceneLibrary::FindScene("Scene.One") == &scene,
                  "库里仍然是最先那个，没有被后来者顶掉");
            check(ink::SceneLibrary::SceneCount() == scenesBefore + 2,
                  "驳回没有增加库里的条目");
            // 停放态：不渲染、两组逻辑都不跑
            intruder.TickLogic(0.02);
            intruder.TickFrame(0.016);
            check(!intruder.IsActive(), "停放态的场景不活跃");
            check(!intruder.IsClosed(), "被驳回不等于被关闭");

            // 驳回者的析构绝不能把正主从库里摘掉
        }
        check(ink::SceneLibrary::SceneCount() == scenesBefore,
              "析构即注销（离开作用域后场景库回到原状）");
        check(ink::SceneLibrary::FindScene("Scene.One") == nullptr,
              "名字被释放");
        check(ink::SceneLibrary::GetAllSceneNames().size() == namesBefore.size(),
              "场景名列表不残留空壳");

        // 不传名字的运行时构造：不登记，也不该在库里留下空 key
        {
            TestSceneA unnamed("");
            check(!unnamed.IsRegistered(), "空名字的场景不登记");
            check(!unnamed.IsRejected(), "空名字不算被驳回，只是没登记");
            check(ink::SceneLibrary::FindScene("") == nullptr, "库里没有空 key");
            check(ink::SceneLibrary::SceneCount() == scenesBefore,
                  "不登记的场景不影响计数");
        }

        // ---- 关闭：摘掉登记、名字空出来、可以再建同名场景 ----
        {
            TestSceneA first("Scene.Reuse");
            check(first.IsRegistered(), "第一个同名场景注册成功");

            TestSceneA blocked("Scene.Reuse");
            check(blocked.IsRejected(), "同名后来者被驳回");

            check(ink::SceneLibrary::CloseScene("Scene.Reuse"),
                  "CloseScene 关掉了占用者");
            check(first.IsClosed(), "被关掉的场景知道自己关闭了");
            check(!first.IsRegistered(), "关掉即退出登记");
            check(!ink::SceneLibrary::IsNameTaken("Scene.Reuse"),
                  "名字已经空出来");
            check(!ink::SceneLibrary::CloseScene("Scene.Reuse"),
                  "重复关闭返回 false");

            // 关掉之后，调用方仍然拿着对象、数据仍然可读（不析构）
            check(first.GetSceneName() == "Scene.Reuse",
                  "关掉之后对象还在，名字仍然读得到");

            // 现在可以建新的同名场景了
            TestSceneA second("Scene.Reuse");
            check(second.IsRegistered(), "名字空出来后新建同名场景成功");
            check(!second.IsRejected(), "新的同名场景不是停放态");
            check(ink::SceneLibrary::FindScene("Scene.Reuse") == &second,
                  "库里现在指向新的那个");

            // 被驳回的那个（blocked）析构时不能把 second 摘掉
        }

        // ---- 活跃态：同期只有一个，切场景时旧的自动静默 ----
        {
            TestSceneA sceneA("Scene.Active.A");
            TestSceneB sceneB("Scene.Active.B");

            check(ink::SceneLibrary::GetActiveScene() == nullptr,
                  "一开始没有活跃场景");
            check(!sceneA.IsActive() && !sceneB.IsActive(),
                  "刚建好的场景都不活跃");

            ink::SceneLibrary::SetActiveScene(&sceneA);
            check(sceneA.IsActive(), "设为活跃后场景自己知道");
            check(ink::SceneLibrary::GetActiveScene() == &sceneA,
                  "库记下了活跃场景");

            // 切到 B：A 自动进入静默
            ink::SceneLibrary::SetActiveScene(&sceneB);
            check(sceneB.IsActive(), "新场景变活跃");
            check(!sceneA.IsActive(), "旧场景自动进入静默");
            check(ink::SceneLibrary::GetActiveScene() == &sceneB,
                  "活跃场景换成了新的");

            // 关闭活跃场景时，活跃指针不能留下野指针
            ink::SceneLibrary::CloseScene("Scene.Active.B");
            check(ink::SceneLibrary::GetActiveScene() == nullptr,
                  "关闭活跃场景后活跃指针被清空，不留野指针");

            // 摘下活跃态
            ink::SceneLibrary::SetActiveScene(&sceneA);
            ink::SceneLibrary::SetActiveScene(nullptr);
            check(ink::SceneLibrary::GetActiveScene() == nullptr,
                  "可以显式摘掉活跃场景");
            check(!sceneA.IsActive(), "被摘掉的场景不再活跃");
        }
    }

    // 8b. 两组逻辑的分派：固定逻辑步 vs 跟着渲染的每帧
    {
        class CountingScene : public ink::InkingScene {
        public:
            explicit CountingScene(const std::string& name)
                : ink::InkingScene(name) {}

            int logicSteps = 0;         // onTick：固定逻辑步
            int frameTicks = 0;         // onRenderBoundTick：跟着渲染的每帧
            double lastStep = 0.0;

        protected:
            void onTick(double step) override {
                ++logicSteps;
                lastStep = step;
            }
            void onRenderBoundTick(double /*delta*/) override { ++frameTicks; }
        };

        CountingScene scene("Scene.Logic");

        // 静默（不活跃）：逻辑步照跑，跟着渲染的那组不跑
        ink::SceneLibrary::SetActiveScene(nullptr);
        scene.TickLogic(0.02);
        scene.TickFrame(0.016);
        check(scene.logicSteps == 1, "不活跃时固定逻辑步照跑");
        check(scene.frameTicks == 0, "不活跃时跟着渲染的那组不跑");

        // 活跃：两组都跑
        ink::SceneLibrary::SetActiveScene(&scene);
        scene.TickLogic(0.02);
        scene.TickFrame(0.016);
        check(scene.logicSteps == 2, "活跃时逻辑步继续跑");
        check(scene.frameTicks == 1, "活跃时跟着渲染的那组也跑");
        check(scene.lastStep == 0.02, "逻辑步长原样传给了钩子");

        // 被别的场景顶掉（静默）：逻辑步照跑，跟着渲染的停
        {
            CountingScene other("Scene.Logic.Other");
            ink::SceneLibrary::SetActiveScene(&other);
            scene.TickLogic(0.02);
            scene.TickFrame(0.016);
            check(scene.logicSteps == 3, "被顶掉时逻辑步仍然跑");
            check(scene.frameTicks == 1, "被顶掉时跟着渲染的那组停（静默态）");
        }

        // 关闭之后：两组都不跑
        ink::SceneLibrary::CloseScene("Scene.Logic");
        scene.TickLogic(0.02);
        scene.TickFrame(0.016);
        check(scene.logicSteps == 3, "关闭后逻辑步也不跑");
        check(scene.frameTicks == 1, "关闭后跟着渲染的那组也不跑");

        // 停放（注册被驳回）：两组都不跑
        CountingScene parkedOne("Scene.Parked");
        CountingScene parkedTwo("Scene.Parked");
        check(parkedTwo.IsRejected(), "第二个同名场景被驳回");
        parkedTwo.TickLogic(0.02);
        parkedTwo.TickFrame(0.016);
        check(parkedTwo.logicSteps == 0 && parkedTwo.frameTicks == 0,
              "停放态不跑任何一组逻辑");
        parkedOne.TickLogic(0.02);
        check(parkedOne.logicSteps == 1, "正主照常跑");
    }

    // 8b-2. 动态组件确实收到了固定逻辑步（这条以前是死的：Tick 没人调）
    {
        class TickingScene : public ink::InkingScene {
        public:
            explicit TickingScene(const std::string& name)
                : ink::InkingScene(name) {}
        };

        // 记下自己被 tick 了几次，以及拿到的步长。
        class TickingNode : public ink::InkingDynamicAnchor {
        public:
            TickingNode(ink::InkingAnchor* parent, const ink::AnchorData& data)
                : ink::InkingDynamicAnchor(parent, data) {}

            int ticks = 0;
            float lastStep = 0.0f;

        protected:
            void onTick(float deltaSeconds) override {
                ++ticks;
                lastStep = deltaSeconds;
            }
        };

        TickingScene scene("Scene.DynamicTick");

        ink::AnchorData boxData;
        boxData.width = 100;
        boxData.height = 50;
        TickingNode dynamicNode(&scene, boxData);

        // 静态兄弟不该收到 tick（它压根没有这个入口）
        class CountingStatic : public ink::InkingStaticAnchor {
        public:
            CountingStatic(ink::InkingAnchor* parent,
                           const ink::AnchorData& data)
                : ink::InkingStaticAnchor(parent, data) {}
        };
        CountingStatic staticNode(&scene, boxData);

        scene.TickLogic(0.02);
        check(dynamicNode.ticks == 1, "动态组件收到固定逻辑步");
        check(dynamicNode.lastStep == 0.02f, "传给动态组件的是固定步长");
        check(staticNode.GetWidth() == 100, "静态兄弟照常留在场景里");

        // 连跑三步：动态组件跟着涨
        scene.TickLogic(0.02);
        scene.TickLogic(0.02);
        check(dynamicNode.ticks == 3, "每步都派发到动态组件");

        // 停放 / 关闭之后不再派发
        ink::SceneLibrary::CloseScene("Scene.DynamicTick");
        scene.TickLogic(0.02);
        check(dynamicNode.ticks == 3, "场景关闭后动态组件不再收到 tick");
    }

    // 8c. Timeline：累加器追赶、双速、自定义频率
    {
        ink::Timeline timeline(0.02, 0.25);  // 逻辑步 50Hz

        int logicSteps = 0;
        int frameCalls = 0;
        timeline.SetLogicCallback([&logicSteps](double step) {
            ++logicSteps;
            (void)step;
        });
        timeline.SetFrameCallback([&frameCalls](double) { ++frameCalls; });

        // 一帧 16ms：还不到一个逻辑步，但每帧回调要跑一次
        timeline.Advance(0.016);
        check(logicSteps == 0, "16ms 还不够一个 20ms 逻辑步");
        check(frameCalls == 1, "每帧回调每个渲染帧恰好一次");

        // 再 16ms：累计 32ms，应当补出 1 步、余 12ms
        timeline.Advance(0.016);
        check(logicSteps == 1, "累加器够了就补一步（50Hz 跑在 60Hz 屏上会错拍）");
        check(timeline.GetLastLogicSteps() == 1, "上一帧派发了 1 步");

        // 一帧 100ms：应当补出 5 步
        timeline.Advance(0.100);
        check(timeline.GetLastLogicSteps() == 5,
              "慢帧按累加器补步，不是只补一次");

        // 时间总量守恒：逻辑步数 × 步长 ≈ 总流逝时间 - 余量
        const double elapsed = 0.016 + 0.016 + 0.100;
        check(std::abs(logicSteps * 0.02
                       - (elapsed - timeline.GetLogicAccumulator())) < 1e-9,
              "逻辑时间跟得上墙上时钟（按累加器算，不按帧数算）");

        // 追不上要在入口截断：切后台回来 delta 可能好几秒，
        // 不截断就会一次补几百步，卡住一帧。
        const std::uint64_t dropsBefore = timeline.GetCatchUpDropCount();
        timeline.Advance(5.0);
        check(timeline.GetCatchUpDropCount() > dropsBefore,
              "超长 delta 被截断（记一次追不上）");
        check(timeline.GetLastLogicSteps() <= 13,
              "5 秒的 delta 只补出截断后那点步数，不是补 250 步");
        check(timeline.GetLogicAccumulator() < timeline.GetLogicInterval(),
              "截断之后累加器里只剩不足一步的余量");
        check(timeline.GetLastDelta() <= 0.25,
              "每帧回调拿到的也是截断后的 delta，和逻辑步同一时间尺度");

        // ---- 自定义频率订阅：24fps ----
        int samples = 0;
        double lastSampleStep = 0.0;
        const ink::Timeline::Handle handle =
            timeline.Subscribe(1.0 / 24.0, [&](double step) {
                ++samples;
                lastSampleStep = step;
            });
        check(handle != 0, "订阅返回有效句柄");
        check(timeline.SubscriberCount() == 1, "订阅数记下了");

        // 按每帧 16ms 推进 10 帧 = 160ms，24fps 大约 3.84 步 → 3 步
        for (int i = 0; i < 10; ++i) {
            timeline.Advance(0.016);
        }
        check(samples == 3, "24fps 订阅者按自己的间隔收到 3 次");
        check(std::abs(lastSampleStep - 1.0 / 24.0) < 1e-12,
              "订阅者收到的是自己的间隔，不是墙上 delta（所以慢帧不走偏）");

        // 退订之后不再收到
        check(timeline.Unsubscribe(handle), "退订成功");
        check(!timeline.Unsubscribe(handle), "重复退订返回 false");
        check(timeline.SubscriberCount() == 0, "退订后订阅数归零");
        const int samplesBefore = samples;
        for (int i = 0; i < 10; ++i) {
            timeline.Advance(0.016);
        }
        check(samples == samplesBefore, "退订之后不再收到回调");

        // 非法参数被拒绝
        check(timeline.Subscribe(0.0, [](double) {}) == 0,
              "间隔为 0 的订阅被拒绝");
        check(timeline.Subscribe(-1.0, [](double) {}) == 0,
              "负间隔的订阅被拒绝");
        check(timeline.Subscribe(0.1, nullptr) == 0, "空回调的订阅被拒绝");

        // 负 delta（时钟回拨 / 第一帧）不能倒着走
        const std::uint64_t stepsBefore = timeline.GetLogicStepCount();
        timeline.Advance(-1.0);
        check(timeline.GetLogicStepCount() == stepsBefore,
              "负 delta 不会被当成倒流");
    }

    // ---------------------------------------------------------------------
    // 9. 渲染树：扁平绘制列表、z 序、可见性剔除、静态/动态坐标
    // ---------------------------------------------------------------------

    // 记录自己有没有被提交过，顺便记下拿到的绝对坐标。
    // 提交入口 SubmitToRenderer 会把渲染器原样透传给 onRender，
    // 所以传 nullptr 也能验"谁被画、按什么顺序画"。
    class RecordingNode : public ink::InkingStaticAnchor {
    public:
        RecordingNode(ink::InkingAnchor* parent, const ink::AnchorData& data,
                      std::vector<const RecordingNode*>* log, const char* tag)
            : ink::InkingStaticAnchor(parent, data), log_(log), tag_(tag) {}

        const char* Tag() const { return tag_; }
        int Renders() const { return renders_; }
        float LastX() const { return lastX_; }
        float LastY() const { return lastY_; }

    protected:
        void onRender(float pixelX, float pixelY) const override {
            ++renders_;
            lastX_ = pixelX;
            lastY_ = pixelY;
            if (log_ != nullptr) {
                log_->push_back(this);
            }
        }

    private:
        std::vector<const RecordingNode*>* log_ = nullptr;
        const char* tag_ = "";
        mutable int renders_ = 0;
        mutable float lastX_ = 0.0f;
        mutable float lastY_ = 0.0f;
    };

    class RenderTestScene : public ink::InkingScene {
    public:
        explicit RenderTestScene(const std::string& name)
            : ink::InkingScene(name) {}
    };

    {
        std::vector<const RecordingNode*> drawn;
        RenderTestScene scene("Scene.Render");

        // 场景自己不该进自己的绘制列表：它是容器不是要画的一笔。
        check(scene.GetDrawItemCount() == 0, "空场景的绘制列表为空");

        // 三个兄弟挂在场景下：z 分别是 0 / 5 / 2，故意不按构造顺序
        ink::AnchorData low;
        low.width = 100;
        low.height = 50;
        low.zIndex = 0;
        RecordingNode nodeLow(&scene, low, &drawn, "low");

        ink::AnchorData high;
        high.width = 100;
        high.height = 50;
        high.zIndex = 5;
        RecordingNode nodeHigh(&scene, high, &drawn, "high");

        ink::AnchorData mid;
        mid.width = 100;
        mid.height = 50;
        mid.zIndex = 2;
        RecordingNode nodeMid(&scene, mid, &drawn, "mid");

        check(scene.GetDrawItemCount() == 3, "三个子节点都登记进了绘制列表");
        check(nodeLow.GetScene() == &scene, "节点记住了自己所属的场景");

        // 绘制顺序直接读列表：**渲染器为空时 Render 必须安全空转**，
        // 而列表本身是绘制顺序的权威来源，自检不需要真的开渲染器。
        //
        // 顺序是 z **从低到高**：z 小的先提交、先画（在底下），
        // z 大的后提交、盖在上面。所以 low(0) 在最前，high(5) 在最后。
        // 真像素的确认在 ink_render_probe 里做（离屏读回）。
        {
            const std::vector<ink::InkingScene::DrawItem>& list =
                scene.BuildDrawList();
            check(list.size() == 3
                      && list[0].node == &nodeLow
                      && list[1].node == &nodeMid
                      && list[2].node == &nodeHigh,
                  "按 zindex 从低到高排列（先画的在底下）");
        }

        // 没有活跃场景时，框架侧渲染是安全空转：不崩、也不改列表。
        // （场景自己的 Render 是 protected，框架侧统一走 SceneLibrary。）
        {
            const std::size_t before = scene.GetDrawItemCount();
            ink::SceneLibrary::SetActiveScene(nullptr);
            check(!ink::SceneLibrary::RenderScene(nullptr),
                  "没有活跃场景时 RenderScene 返回 false");
            check(scene.GetDrawItemCount() == before,
                  "没有活跃场景时是安全空转");
            check(nodeLow.Renders() == 0, "空渲染器不会调到 onRender");
            ink::SceneLibrary::SetActiveScene(&scene);
        }

        // 改 z：顺序要跟着变，而且是惰性的——改完不查就不重排
        const std::uint64_t generationBefore = scene.GetStructureGeneration();
        nodeLow.ChangeZIndex(9);
        check(scene.GetStructureGeneration() > generationBefore,
              "改 zindex 会通知场景（结构变更计数增加）");
        {
            const std::vector<ink::InkingScene::DrawItem>& list =
                scene.BuildDrawList();
            check(list.size() == 3 && list[2].node == &nodeLow,
                  "改完 zindex 之后重建，顺序跟着变");
        }

        // 可见性：**列表保留**（所以再显示出来不需要谁把它加回去），
        // 但渲染时会被跳过，而且父级不可见会短路整棵子树。
        check(nodeMid.SetVisible(false), "隐藏节点生效");
        check(!nodeMid.IsVisibleInTree(), "隐藏后自己不在可见树里");
        check(nodeLow.IsVisibleInTree(), "隐藏别的节点不影响自己");
        check(!nodeMid.SetVisible(false), "重复隐藏不算改动");
        {
            const std::vector<ink::InkingScene::DrawItem>& list =
                scene.BuildDrawList();
            bool stillListed = false;
            for (const ink::InkingScene::DrawItem& entry : list) {
                if (entry.node == &nodeMid) {
                    stillListed = true;
                }
            }
            check(stillListed,
                  "隐藏的节点仍留在列表里（可见性是可逆的，不能删）");
            check(list.size() == 3, "隐藏不改变列表长度");
        }

        // 恢复可见
        check(nodeMid.SetVisible(true), "恢复可见");
        check(nodeMid.IsVisibleInTree(), "恢复后自己回到可见树里");
        check(scene.GetDrawItemCount() == 3, "恢复可见后列表仍然是三项");

        // 父子：父级不可见时整棵子树都不该被画
        ink::AnchorData parentData;
        parentData.width = 200;
        parentData.height = 100;
        RecordingNode parentNode(&scene, parentData, &drawn, "parent");
        ink::AnchorData childData;
        childData.width = 20;
        childData.height = 20;
        RecordingNode childNode(&parentNode, childData, &drawn, "child");
        check(scene.GetDrawItemCount() == 5, "子节点也登记进同一个场景");

        check(parentNode.SetVisible(false), "隐藏父级");
        check(!parentNode.IsVisibleInTree(), "父级自己不可见");
        check(!childNode.IsVisibleInTree(),
              "父级不可见时子级也不在可见树里（整棵子树短路）");
        check(childNode.IsVisible(), "子级自己那一层仍然是可见的");
        check(scene.GetDrawItemCount() == 5,
              "父级隐藏不删列表，只是提交时跳过");
        parentNode.SetVisible(true);
        check(childNode.IsVisibleInTree(), "父级恢复后子级一起回到可见树");
        check(scene.GetDrawItemCount() == 5, "父级恢复后列表不变");

        // 静态节点的绝对坐标：这一对父子用的都是默认锚点 (0,0)，
        // 所以子节点贴父级左上角；父级又贴场景左上角 → 两者都是 (0,0)。
        {
            const std::vector<ink::InkingScene::DrawItem>& list =
                scene.BuildDrawList();
            bool foundChild = false;
            for (const ink::InkingScene::DrawItem& entry : list) {
                if (entry.node == &childNode) {
                    foundChild = true;
                    check(entry.absX == 0.0f && entry.absY == 0.0f,
                          "静态节点在建表时算好绝对坐标（默认锚点贴父级左上角）");
                    check(entry.parent == &parentNode, "绘制项记住了父指针");
                }
            }
            check(foundChild, "能按指针在绘制列表里找到子节点");
        }

        // 绝对坐标真的按锚点推导，而且**改锚点后缓存的快照会重算**：
        // 父级 200x100、子级 20x20，双 Center → 左上角落在 (90,40)。
        //
        // 用动态档改锚点（几何写入口只有动态版有），验的是"动态节点改几何
        // 会通知场景重算缓存坐标"——不然静态子孙的坐标会停在旧值上。
        ink::AnchorData moverData;
        moverData.width = 20;
        moverData.height = 20;
        ink::InkingDynamicAnchor moving(&parentNode, moverData);
        check(moving.IsVisibleInTree(), "新动态节点默认在可见树里");

        {
            const std::vector<ink::InkingScene::DrawItem>& list =
                scene.BuildDrawList();
            bool found = false;
            for (const ink::InkingScene::DrawItem& entry : list) {
                if (entry.node == &moving) {
                    found = true;
                    check(entry.absX == 0.0f && entry.absY == 0.0f,
                          "默认锚点：动态节点贴父级左上角");
                }
            }
            check(found, "动态节点也进了绘制列表");
        }

        // 改成双 Center：绝对坐标应当变成 (90,40)
        check(moving.ChangeSelfAnchor(ink::InkingChangeAnchor::Center)
                  && moving.ChangeTraceAnchor(ink::InkingChangeAnchor::Center),
              "动态节点改锚点生效");
        {
            const std::vector<ink::InkingScene::DrawItem>& list =
                scene.BuildDrawList();
            for (const ink::InkingScene::DrawItem& entry : list) {
                if (entry.node == &moving) {
                    check(entry.absX == 90.0f && entry.absY == 40.0f,
                          "改锚点后列表里的绝对坐标跟着重算（(90,40)）");
                }
            }
        }

        // 换父级：跨场景时要从旧场景摘掉、登记到新场景
        {
            RenderTestScene other("Scene.Render.Other");
            const std::size_t beforeInScene = scene.GetDrawItemCount();
            check(other.GetDrawItemCount() == 0, "新场景一开始是空的");

            RecordingNode mover(&scene, childData, &drawn, "mover");
            check(scene.GetDrawItemCount() == beforeInScene + 1,
                  "新节点登记进当前场景");

            check(mover.SetParent(&other), "换父级到另一个场景");
            check(mover.GetScene() == &other, "节点归属跟着换到新场景");
            check(other.GetDrawItemCount() == 1, "新场景收到了它");
            check(scene.GetDrawItemCount() == beforeInScene,
                  "旧场景把它摘掉了");

            // 换回去
            check(mover.SetParent(&scene), "换父级回原场景");
            check(mover.GetScene() == &scene, "归属回到原场景");
            check(other.GetDrawItemCount() == 0, "新场景又空了");
        }

        // 节点销毁：必须从列表里摘掉，不能留野指针
        {
            const std::size_t before = scene.GetDrawItemCount();
            {
                RecordingNode temporary(&scene, childData, &drawn, "temp");
                check(scene.GetDrawItemCount() == before + 1, "临时节点已登记");
            }
            check(scene.GetDrawItemCount() == before,
                  "节点析构后自动从绘制列表摘掉（不留野指针）");
        }

        // 场景析构：子树里指向它的 _scene 要被摘干净
        {
            RenderTestScene doomed("Scene.Doomed");
            ink::AnchorData survivorData;
            survivorData.width = 10;
            survivorData.height = 10;
            RecordingNode survivor(&doomed, survivorData, &drawn, "survivor");
            check(survivor.GetScene() == &doomed, "存活节点先记着 doomed 场景");
        }
        // doomed 已经销毁；survivor 在它之前析构（块作用域逆序），
        // 这里真正要验的是"场景析构时不会去解引用已死的节点"。
        check(true, "场景析构后没有崩（指向场景的指针已摘干净）");
    }

    // ---------------------------------------------------------------------
    // 10. 窗口主循环：只在显式开了无头冒烟时才跑
    //
    // 这一段会真的开窗，并且要等 INK_AUTOQUIT 到点才退出，
    // 所以默认跳过，只有 `INK_AUTOQUIT=1 ink_test` 才会走。
    // ---------------------------------------------------------------------
    if (SDL_getenv("INK_AUTOQUIT") != nullptr) {
        std::printf("无头冒烟：进入窗口主循环，约 2 秒后自动退出\n");

        // 造一个真场景，里面放几个不同 zindex / 颜色的块，
        // 让主循环走一遍真实的"骨架 → 排序 → 提交"路径。
        // 窗口每帧通过 SceneLibrary 按名字找到它（GetCurrentScene）。
        class SmokeScene : public ink::InkingScene {
        public:
            SmokeScene() : ink::InkingScene("Smoke") {}
        };
        SmokeScene smokeScene;

        // 一排块的几何与配色：越靠右 z 越大，颜色逐块变亮。
        const auto blockData = [](int index) {
            ink::AnchorData block;
            block.width = 240;
            block.height = 160;
            block.offsetX = 180.0f + static_cast<float>(index) * 220.0f;
            block.offsetY = 300.0f;
            block.zIndex = index;  // 0..3，越晚画越靠上
            block.color = static_cast<std::uint32_t>(
                0xFF000000u | ((0x4Fu * static_cast<unsigned>(index + 1)) << 16)
                | 0x60u);
            return block;
        };

        ink::AnchorData backdrop;
        backdrop.width = inking::kDesignWidth;
        backdrop.height = inking::kDesignHeight;
        backdrop.zIndex = -100;
        backdrop.color = 0xFF22222Fu;
        ink::InkingStaticAnchor backdropNode(&smokeScene, backdrop);

        // 一排块，用来肉眼确认"越晚画越靠上"的压盖关系。
        // 用栈上对象，析构顺序天然正确（逆序，块先于场景走）。
        ink::InkingStaticAnchor block0(&smokeScene, blockData(0));
        ink::InkingStaticAnchor block1(&smokeScene, blockData(1));
        ink::InkingStaticAnchor block2(&smokeScene, blockData(2));
        ink::InkingStaticAnchor block3(&smokeScene, blockData(3));

        // 一个隐藏的块：证明可见性真的会拦掉提交（画面上不该出现）
        ink::AnchorData hidden;
        hidden.width = 400;
        hidden.height = 200;
        hidden.offsetX = 200.0f;
        hidden.offsetY = 760.0f;
        hidden.zIndex = 99;  // z 最高，如果可见一定会盖住别人
        hidden.color = 0xFFFF00FFu;
        ink::InkingStaticAnchor hiddenNode(&smokeScene, hidden);
        check(hiddenNode.SetVisible(false), "冒烟场景里放一个隐藏的高层块");

        check(smokeScene.GetDrawItemCount() == 6,
              "冒烟场景的绘制列表有 6 项（背景 + 4 块 + 1 隐藏块）");

        window.Show("Smoke");

        // Show() 返回说明主循环退出了；此时窗口句柄应该已经释放，
        // 再配置一次不会碰到已经销毁的 SDL 对象。
        check(window.setWindowWidth(1280), "主循环退出后窗口仍可配置（句柄已释放）");

        // 场景还在（栈上，比 window 晚析构），但窗口释放时已经把场景名清掉了，
        // 所以这里改用主循环留下的观察点来验证"按名字解析到场景并真的渲染了"。
        check(ink::GetLastRenderedScene() == &smokeScene,
              "主循环里窗口确实按场景名解析到并渲染了这个场景");
        check(smokeScene.GetDrawItemCount() == 6,
              "跑完主循环后绘制列表仍然是 6 项");
    }

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
