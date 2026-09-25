// 自检程序：把日志、消息队列、任务队列和窗口的基本行为过一遍。
// 退出码非 0 表示有检查项失败。
//
// 三个编译期开关（ISLOG / ISDEBUG / ISMESSAGE）关掉时，对应的检查会整体
// 跳过或改成"验证空操作"，这样同一份自检能在任何开关组合下跑通。

#include <ink/ink.h>
#include <window/InkingWindow.h>

#include <any>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

int gFailed = 0;

/// 测试用场景：只数自己被点了几次，用来验证事件冒泡。
class ClickCounterScene : public ink::Scene {
public:
    using Scene::Scene;

    bool onClick(ink::PointerEvent& event) override {
        ++clicks;
        event.handled = true;
        return true;
    }

    int clicks = 0;
};

/// 测试用场景：动态场景，位置随便挪，用来验证"动态不进表"。
class FloaterProbe : public ink::Scene {
public:
    explicit FloaterProbe(std::string name) : Scene(std::move(name), true) {}
};

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

}  // namespace

int main() {
    std::printf("InkingFrontendDeveloper %s（链接 SDL3 %s）\n",
                ink::kVersion, ink::sdl3_version().c_str());
    std::printf("编译期开关：日志=%s，调试=%s，消息队列=%s\n",
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
    } else {
        check(ink::InkLog::logFilePath().empty(), "日志关闭时不提供日志路径");
    }

    // ---------------------------------------------------------------------
    // 2. 消息队列（关闭 ISMESSAGE 时整节退化成空操作检查）
    // ---------------------------------------------------------------------
    if (ink::kMessageEnabled) {
        INK_MESSAGE(ink::MessageType::Normal, "立即消息");
        {
            const std::vector<ink::Message> messages =
                ink::MessageQueue::drainMessages();
            check(messages.size() == 1 && messages.front().message == "立即消息",
                  "立即消息入队并被取出");
            check(ink::MessageQueue::pendingCount() == 0, "取出后队列为空");
        }

        INK_MESSAGE_ERROR("错误消息");
        check(ink::MessageQueue::pendingCount() == 1, "快捷宏入队一条错误消息");
        ink::MessageQueue::drainMessages();

        // 延迟消息靠任务队列定时，到点再按"立即"重新入队
        INK_MESSAGE_AFTER(ink::MessageType::Warn, 0.05f, "延迟消息");
        check(ink::MessageQueue::pendingCount() == 0, "延迟消息不会立刻进队列");
        check(waitFor([] { return ink::MessageQueue::pendingCount() > 0; }, 2000),
              "延迟消息到点后重新入队");
        ink::MessageQueue::drainMessages();
    } else {
        // 关闭状态下这些调用整条消失，连字符串都不会构造
        INK_MESSAGE(ink::MessageType::Normal, "这条消息不该存在");
        INK_MESSAGE_PASS("这条也不该存在");
        check(ink::MessageQueue::pendingCount() == 0, "消息队列关闭时入队是空操作");
        check(ink::MessageQueue::drainMessages().empty(), "消息队列关闭时取不到消息");
    }

    // ---------------------------------------------------------------------
    // 3. 任务队列：提交一个任务，等管家线程执行
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
    // 4. 窗口：设计尺寸是编译期常量，窗口尺寸是运行期属性
    // ---------------------------------------------------------------------
    ink::InkingWindow& window = ink::InkingWindow::Instance();
    check(window.GetWidth() == inking::kDesignWidth, "窗口宽度默认等于设计宽度");
    check(window.GetHeight() == inking::kDesignHeight, "窗口高度默认等于设计高度");
    check(!window.setWidth(0), "非法宽度被拒绝");
    check(!window.setHeight(-1), "非法高度被拒绝");
    check(window.setWidth(1280) && window.GetWidth() == 1280, "setWidth 生效");
    check(window.setHeight(720) && window.GetHeight() == 720, "setHeight 生效");

    // ---------------------------------------------------------------------
    // 5. 场景 / 按钮 / 坐标映射表
    // ---------------------------------------------------------------------
    {
        using ink::Button;
        using ink::HitEntry;
        using ink::HitTable;
        using ink::Rect;
        using ink::Scene;

        // 5.1 映射表本身：重叠时谁在上面谁赢，边界是半开区间
        {
            HitTable table;
            Scene lower("表-下层");
            Scene upper("表-上层");
            table.build({HitEntry{&lower, Rect{0.0f, 0.0f, 100.0f, 100.0f}, 1},
                         HitEntry{&upper, Rect{50.0f, 50.0f, 100.0f, 100.0f}, 2}});

            check(table.size() == 2, "映射表收下两条条目");
            check(table.hit(10.0f, 10.0f).target == &lower, "只落在下层时命中的是下层");
            check(table.hit(60.0f, 60.0f).target == &upper, "重叠处命中的是上层");
            check(table.hit(140.0f, 140.0f).target == &upper, "只落在上层时命中的是上层");
            check(!table.hit(150.0f, 10.0f).hit, "右边界是开区间，不算命中");
            check(!table.hit(-1.0f, 10.0f).hit, "表外的点不命中");
            check(table.candidateCount() > table.size(), "跨格条目被登记进多个格子");
        }

        // 5.2 场景树：静态场景第一次命中时懒烘焙成表
        Scene root("测试根");
        root.SetRect(Rect{0.0f, 0.0f, 400.0f, 300.0f});
        Scene& panel = root.make<Scene>("测试面板");
        panel.SetRect(Rect{20.0f, 20.0f, 200.0f, 200.0f});
        Button& ok = panel.make<Button>("测试按钮", "确定");
        ok.SetRect(Rect{40.0f, 40.0f, 80.0f, 40.0f});

        check(root.needsBake(), "静态场景一开始就标着待烘焙");
        check(root.hitTest(60.0f, 60.0f).target == &ok, "点按钮命中按钮");
        check(root.isBaked(), "命中一次之后场景已经烘焙");
        check(root.GetHitTable().size() == 3, "映射表里是根 + 面板 + 按钮三条");
        check(root.GetHitTable().hit(300.0f, 250.0f).target == &root, "面板之外落回根场景");
        check(!root.hitTest(900.0f, 900.0f).hit, "场景外的点没有命中");

        // 5.3 输入管理：悬停 / 按下 / 抬起 / 取消
        ink::InputRouter router;
        router.attach(root);
        int clicks = 0;
        ok.action = [&clicks](Button&) { ++clicks; };

        check(router.pointerMove(60.0f, 60.0f) && ok.IsHovered(), "指针悬停到按钮上");
        check(router.pointerDown(60.0f, 60.0f) && ok.IsPressed(), "按钮进入按下态");
        check(router.pointerUp(60.0f, 60.0f) && clicks == 1, "原地抬起算一次点击");
        check(!ok.IsPressed(), "抬起之后不再按下");

        router.pointerDown(60.0f, 60.0f);
        router.pointerUp(10.0f, 280.0f);
        check(clicks == 1, "挪开再松手只算取消，不算点击");
        check(router.pointerMove(380.0f, 280.0f) && !ok.IsHovered(), "移开之后取消悬停");

        // 5.4 冒泡：子场景不接，事件交给父场景
        ClickCounterScene& catcher = root.make<ClickCounterScene>("吞点击的面板");
        catcher.SetRect(Rect{240.0f, 20.0f, 140.0f, 140.0f});
        Scene& child = catcher.make<Scene>("不接手的孩子");
        child.SetRect(Rect{260.0f, 40.0f, 60.0f, 60.0f});

        router.pointerDown(280.0f, 60.0f);
        router.pointerUp(280.0f, 60.0f);
        check(catcher.clicks == 1, "子场景不接的点击冒到了父场景");

        // 5.5 静态 + 动态：动态子场景不进表，却能压住静态兄弟
        Scene& card = root.make<Scene>("静态卡片");
        card.SetRect(Rect{240.0f, 200.0f, 140.0f, 80.0f});
        FloaterProbe& floater = root.make<FloaterProbe>("动态探针");
        floater.SetRect(Rect{260.0f, 210.0f, 60.0f, 60.0f});

        check(root.hitTest(280.0f, 230.0f).target == &floater, "动态子场景压住静态兄弟");
        check(!root.needsBake(), "动态场景沉在下面，映射表已经烘焙好");
        floater.SetRect(Rect{10.0f, 210.0f, 60.0f, 60.0f});
        check(!root.needsBake(), "动态场景挪窝不会弄脏父场景的映射表");
        check(root.hitTest(280.0f, 230.0f).target == &card, "动态场景挪走后露出静态兄弟");
        check(root.hitTest(30.0f, 230.0f).target == &floater, "动态场景在新位置照样命中");

        // 5.6 窗口命令：场景只提要求，根场景排队，等窗口层来取
        ok.requestWindowCommand(ink::WindowCommand::Close);
        check(root.takeWindowCommand() == ink::WindowCommand::Close, "窗口命令冒泡到根场景");
        check(root.takeWindowCommand() == ink::WindowCommand::None, "取完之后队列是空的");

        // 5.7 场景库：构造即登记，按名字能找回来
        check(ink::SceneRegistry::find("测试按钮") == &ok, "按名字找得到场景");
        check(ink::SceneRegistry::find("不存在的场景") == nullptr, "找不到的名字返回空");

        // 5.8 几何一变就得重烘焙
        ok.SetRect(Rect{40.0f, 40.0f, 60.0f, 40.0f});
        check(root.needsBake(), "按钮改了几何，根场景跟着变脏");
        check(root.hitTest(110.0f, 60.0f).target == &panel, "重烘焙之后旧范围不再命中按钮");
        check(root.hitTest(60.0f, 60.0f).target == &ok, "重烘焙之后新范围照样命中按钮");
    }

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
