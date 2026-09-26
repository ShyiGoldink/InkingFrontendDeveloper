// 自检程序：把日志、消息队列、任务队列和窗口的基本行为过一遍。
// 退出码非 0 表示有检查项失败。

#include <ink/ink.h>
#include <ink/basic/InkingAnchor.h>
#include <core/RedrawScheduler.h>
#include <input/MouseInput.h>
#include <scene/Button.h>
#include <scene/InkingScene.h>
#include <scene/InputRouter.h>
#include <scene/SceneRegistry.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <any>
#include <chrono>
#include <cstdint>
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

/// 验证写入口只在真的改动时才触发钩子。
class CountingAnchor : public ink::InkingAnchor {
public:
    CountingAnchor(ink::InkingAnchor* parent, const ink::AnchorData& data)
        : ink::InkingAnchor(parent, data) {}

    int sizeChanges = 0;

protected:
    void onSizeChanged() override { ++sizeChanges; }
};

/**
 * 自检用的最小菜单栏：根场景 + 一条等宽等距的菜单栏 + 3 个按钮。
 *
 * 布局和 examples/menu_bar 一致：菜单栏 1920×72 贴顶，按钮 200×56、间距 24，
 * 从左边 40 开始，垂直居中（自身 Center 对上父级 Center）。
 */
struct MenuBarFixture {
    ink::InkingScene page{"自检-菜单栏页"};
    ink::InkingScene* bar = nullptr;
    ink::Button* buttons[3] = {nullptr, nullptr, nullptr};
    int clicks = 0;

    MenuBarFixture() {
        page.Resize(1920, 1080);
        page.ChangeZIndex(0);

        bar = &page.Make<ink::InkingScene>("自检-菜单栏条");
        bar->UseSlotLayout();  // 静态子节点等宽等距 → 命中用算术
        bar->ChangeSelfAnchor(ink::InkingChangeAnchor::LeftTop);
        bar->ChangeTraceAnchor(ink::InkingChangeAnchor::LeftTop);
        bar->Resize(1920, 72);
        bar->ChangeZIndex(1);

        const char* labels[] = {"一", "二", "三"};
        for (int index = 0; index < 3; ++index) {
            ink::Button& button = bar->Make<ink::Button>(
                "自检-按钮" + std::to_string(index + 1), labels[index]);
            button.ChangeSelfAnchor(ink::InkingChangeAnchor::LeftCenter);
            button.ChangeTraceAnchor(ink::InkingChangeAnchor::LeftCenter);
            button.ChangeOffset(
                40.0f + static_cast<float>(index) * (200.0f + 24.0f), 0.0f);
            button.Resize(200, 56);
            button.ChangeZIndex(2);
            buttons[index] = &button;
        }

        for (ink::Button* button : buttons) {
            button->action = [this](ink::Button&) { ++clicks; };
        }
    }

    /// 三个按钮的中心（设计坐标）。
    static float slotCenterX(int index) {
        return 40.0f + static_cast<float>(index) * (200.0f + 24.0f) + 100.0f;
    }
};

/// 记账用的假画布：不接后端，只把「谁画了什么」按顺序记下来。
class RecordingCanvas : public ink::Canvas {
public:
    std::vector<ink::Color> fills;

    void fillRect(const ink::Rect&, const ink::Color& color) override {
        fills.push_back(color);
    }
    void strokeRect(const ink::Rect&, const ink::Color&, float) override {}
    void drawLine(float, float, float, float, const ink::Color&) override {}
};

/// 用颜色当标签的场景：r 通道就是它的编号，用来验证绘制顺序。
class TagScene : public ink::InkingScene {
public:
    TagScene(const std::string& name, std::uint8_t tag)
        : ink::InkingScene(name), tag(tag) {}

    void onDraw(ink::Canvas& canvas) const override {
        canvas.fillRect(GetRect(), ink::rgb(tag, 0, 0));
    }

    std::uint8_t tag = 0;
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
    check(window.GetWidth() == inking::kDesignWidth, "窗口宽度默认等于设计宽度");
    check(window.GetHeight() == inking::kDesignHeight, "窗口高度默认等于设计高度");
    check(!window.setWidth(0), "非法宽度被拒绝");
    check(!window.setHeight(-1), "非法高度被拒绝");
    check(window.setWidth(1280) && window.GetWidth() == 1280, "setWidth 生效");
    check(window.setHeight(720) && window.GetHeight() == 720, "setHeight 生效");

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
        ink::InkingAnchor box(nullptr, boxData);

        ink::AnchorData itemData;
        itemData.width = 40;
        itemData.height = 20;
        itemData.selfAnchor = ink::InkingChangeAnchor::Center;
        itemData.traceAnchor = ink::InkingChangeAnchor::Center;
        ink::InkingAnchor item(&box, itemData);

        // 双 Center：自身矩形落在 (80,40)-(120,60)
        check(item.GetX() == 80.0f && item.GetY() == 40.0f,
              "双 Center 锚点 → 在父级里居中");
        check(item.GetAbsX() == 80.0f && item.GetAbsY() == 40.0f,
              "父级在原点时绝对位置等于局部位置");

        // 三层：祖父 400x200，父级 200x100 居中于祖父，子居中于父级
        ink::AnchorData grandData;
        grandData.width = 400;
        grandData.height = 200;
        ink::InkingAnchor grand(nullptr, grandData);
        ink::InkingAnchor mid(&grand, boxData);
        mid.ChangeSelfAnchor(ink::InkingChangeAnchor::Center);
        mid.ChangeTraceAnchor(ink::InkingChangeAnchor::Center);
        ink::InkingAnchor deep(&mid, itemData);
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

        // 层级：同场景内全局比 zindex，z 相同时晚注册的在上
        ink::InkingAnchor lower(&box, itemData);
        ink::InkingAnchor upper(&box, itemData);
        upper.ChangeZIndex(1);
        check(ink::InkingAnchor::IsAbove(upper, lower), "z 大的在上");
        lower.ChangeZIndex(1);
        upper.SetRegisterOrder(2);
        lower.SetRegisterOrder(5);
        check(ink::InkingAnchor::IsAbove(lower, upper), "z 相同时晚注册的在上");

        // 标脏：写入口真的改了才标
        ink::InkingAnchor dirtyOne(&box, itemData);
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
        ink::InkingAnchor scaled(&box, itemData);
        scaled.ClearDirty();
        scaled.SetMagnification(2.0f);
        check(scaled.GetDeviceWidth() == 80.0f
                  && scaled.GetDeviceHeight() == 40.0f,
              "展示倍率换算成设备像素");
        check(!scaled.IsDirty(), "展示倍率不标脏");
    }

    // ---------------------------------------------------------------------
    // 9. 静态层：菜单栏（等宽等距）烘一次索引，之后每帧只查
    // ---------------------------------------------------------------------
    {
        MenuBarFixture fixture;
        ink::InkingScene& page = fixture.page;
        ink::InkingScene& bar = *fixture.bar;

        check(page.NeedsBake(), "刚建好的静态场景要求烘焙");
        page.Bake();
        check(page.IsBaked() && !page.NeedsBake(), "烘焙后不再要求重烘");

        // 父表里只有「根自己 + 菜单栏」两条：按钮在菜单栏自己的专用查找里。
        check(page.GetHitTable().Size() == 2, "根表只有 2 条条目（根兜底 + 菜单栏）");
        check(bar.UsesSlotIndex(), "菜单栏用等宽等距专用查找，没建表");
        check(bar.GetSlotIndex().Size() == 3, "专用查找里 3 个槽位");
        check(bar.GetSlotIndex().Gap() == 24.0f, "槽间距按槽位推出来");
        check(bar.GetHitTable().Empty(), "专用查找生效时不留网格表");

        // 算术查表：index = (x - 行首) / (槽宽 + 槽间距)
        check(bar.GetSlotIndex().IndexAt(140.0f, 36.0f) == 0, "槽位算术：第 1 个");
        check(bar.GetSlotIndex().IndexAt(364.0f, 36.0f) == 1, "槽位算术：第 2 个");
        check(bar.GetSlotIndex().IndexAt(588.0f, 36.0f) == 2, "槽位算术：第 3 个");
        check(bar.GetSlotIndex().IndexAt(250.0f, 36.0f) == -1, "落在槽间距里算不出槽位");

        // 三态 + 目标：按钮 > 菜单栏 > 根兜底
        check(page.Query(140.0f, 36.0f).target == fixture.buttons[0], "命中第 1 个按钮");
        check(page.Query(364.0f, 36.0f).target == fixture.buttons[1], "命中第 2 个按钮");
        check(page.Query(588.0f, 36.0f).target == fixture.buttons[2], "命中第 3 个按钮");
        check(page.Query(140.0f, 36.0f).state == ink::HitState::Block, "按钮是 Block");
        check(page.Query(250.0f, 36.0f).target == &bar, "槽间距里由菜单栏兜住");
        check(page.Query(1500.0f, 36.0f).target == &bar, "菜单栏空白处由菜单栏兜住");
        check(page.Query(1500.0f, 900.0f).target == &page, "页面空白处由根兜底");
        check(page.Query(1500.0f, 900.0f).state == ink::HitState::Block, "根兜底条目是 Block");
        check(page.Query(-10.0f, 900.0f).state == ink::HitState::Miss, "表外面是 Miss");

        // 静态层自己不动：查多少次都不重烘
        const std::uint64_t bakes = page.BakeCount();
        for (int i = 0; i < 100; ++i) {
            page.Query(140.0f, 36.0f);
            page.Query(1500.0f, 900.0f);
        }
        check(page.BakeCount() == bakes, "200 次查询一次都没重烘");

        // 平铺绘制表：按 z 升序画一遍，每个结点只画一次
        {
            TagScene drawPage("自检-绘制页", 1);
            drawPage.Resize(400, 400);
            drawPage.ChangeZIndex(0);

            TagScene& drawBar = drawPage.Make<TagScene>("自检-绘制条", 2);
            drawBar.UseSlotLayout();
            drawBar.Resize(400, 40);
            drawBar.ChangeZIndex(1);

            for (int index = 0; index < 3; ++index) {
                TagScene& cell = drawBar.Make<TagScene>(
                    "自检-绘制的格子" + std::to_string(index + 1), 3);
                cell.ChangeOffset(10.0f + static_cast<float>(index) * 80.0f, 10.0f);
                cell.Resize(60, 20);
                cell.ChangeZIndex(2);
            }

            drawPage.Bake();
            RecordingCanvas recording;
            drawPage.Draw(recording);

            std::vector<std::uint8_t> order;
            for (const ink::Color& color : recording.fills) {
                order.push_back(color.r);
            }
            const std::vector<std::uint8_t> expected{1, 2, 3, 3, 3};
            check(order == expected, "绘制顺序 = 根 → 菜单栏 → 三个按钮（z 升序，各一次）");
            check(drawPage.Query(40.0f, 20.0f).Blocked(), "画出来的地方也能查到");
        }
    }

    // ---------------------------------------------------------------------
    // 10. 层级：显式 zindex 说了算，z 相同时晚注册的在上
    // ---------------------------------------------------------------------
    {
        ink::InkingScene page("自检-层级页");
        page.Resize(400, 400);

        ink::InkingScene& first = page.Make<ink::InkingScene>("自检-先注册的高层");
        first.Resize(200, 200);
        first.ChangeZIndex(2);

        ink::InkingScene& second = page.Make<ink::InkingScene>("自检-后注册的低层");
        second.Resize(200, 200);
        second.ChangeZIndex(1);

        page.Bake();
        check(page.Query(10.0f, 10.0f).target == &first,
              "重叠时显式 z 大的在上面（晚注册也压不过）");

        second.ChangeZIndex(2);
        check(page.NeedsBake(), "层级变化标了脏");
        check(page.Query(10.0f, 10.0f).target == &second, "z 相同时晚注册的在上（懒烘焙）");

        first.ChangeZIndex(3);
        check(page.Query(10.0f, 10.0f).target == &first, "再把 z 抬回去就又轮到它");
    }

    // ---------------------------------------------------------------------
    // 11. 输入：isDirty + query 的生命周期；点击不走脏标记；三态
    // ---------------------------------------------------------------------
    {
        MenuBarFixture fixture;
        ink::InputRouter router;
        router.Attach(fixture.page);

        check(router.Dirty(), "刚接管就是脏的（场景树刚建好）");
        check(router.BeginFrame(MenuBarFixture::slotCenterX(0), 36.0f),
              "脏 → 这一帧做一次 hover query");
        check(router.GetHovered() == fixture.buttons[0], "hover 落在第 1 个按钮");

        // 帧计数尾巴：标脏之后连着几帧还要查（§5）
        int tail = 0;
        while (router.BeginFrame(MenuBarFixture::slotCenterX(0), 36.0f)) {
            ++tail;
        }
        check(tail == ink::InputRouter::kTailFrames, "帧计数尾巴 = kTailFrames");

        const std::uint64_t queries = router.HoverQueries();
        for (int i = 0; i < 5; ++i) {
            router.BeginFrame(MenuBarFixture::slotCenterX(0), 36.0f);
        }
        check(router.HoverQueries() == queries, "干净时每帧 0 次 query");
        check(router.IdleFrames() >= 5, "干净帧被记下来");

        // 鼠标移动 → 重新标脏
        router.MarkMouseMoved();
        check(router.Dirty(), "鼠标移动标脏");
        check(router.BeginFrame(MenuBarFixture::slotCenterX(1), 36.0f), "鼠标一动又 query");
        check(router.GetHovered() == fixture.buttons[1], "hover 换到第 2 个按钮");
        check(!fixture.buttons[0]->IsHovered() && fixture.buttons[1]->IsHovered(),
              "Leave / Enter 都投出去了");
        check(fixture.buttons[1]->AppearanceChanges() >= 1, "悬停改外观 → 明确调了 makeDirty");

        // 点击：不经过脏标记，单独查一次
        const std::uint64_t hoverBefore = router.HoverQueries();
        router.PointerDown(MenuBarFixture::slotCenterX(1), 36.0f);
        check(fixture.buttons[1]->IsPressed(), "按下改了外观");
        router.PointerUp(MenuBarFixture::slotCenterX(1), 36.0f);
        check(!fixture.buttons[1]->IsPressed(), "抬起复位");
        check(fixture.clicks == 1, "点击回调被触发一次");
        check(router.ClickQueries() == 2, "点击单独查了两次（按下 + 抬起）");
        check(router.HoverQueries() == hoverBefore, "点击不经过脏标记，不额外 hover query");

        // 在别处松手 → 取消，不算点击
        router.PointerDown(MenuBarFixture::slotCenterX(1), 36.0f);
        router.PointerUp(1500.0f, 900.0f);
        check(fixture.clicks == 1, "在别处松手不算点击");
        check(!fixture.buttons[1]->IsPressed(), "取消之后按下状态复位");

        // 三态：遮罩 Block 挡住事件；让过层 PassThrough 不挡下面的东西
        ink::InkingScene ghostPage("自检-三态页");
        ghostPage.Resize(400, 400);

        ink::InkingScene& mask = ghostPage.Make<ink::InkingScene>("自检-遮罩");
        mask.Resize(400, 400);
        mask.ChangeZIndex(5);

        ink::InkingScene& veil = ghostPage.Make<ink::InkingScene>("自检-让过层");
        veil.SetHitPolicy(ink::HitPolicy::PassThrough);
        veil.Resize(400, 400);
        veil.ChangeZIndex(9);

        ink::InkingScene& beside = ghostPage.Make<ink::InkingScene>("自检-旁边的让过层");
        beside.SetHitPolicy(ink::HitPolicy::PassThrough);
        beside.ChangeOffset(400.0f, 0.0f);
        beside.Resize(400, 400);
        beside.ChangeZIndex(9);

        check(ghostPage.Query(10.0f, 10.0f).target == &mask, "让过层不挡下面的遮罩");
        check(ghostPage.Query(10.0f, 10.0f).state == ink::HitState::Block,
              "下面的遮罩仍然是 Block");
        check(ghostPage.Query(500.0f, 10.0f).state == ink::HitState::PassThrough,
              "下面没东西时返回 PassThrough");
        check(ghostPage.Query(500.0f, 10.0f).target == &beside, "PassThrough 也带目标");
        check(ghostPage.Query(1000.0f, 10.0f).state == ink::HitState::Miss,
              "表外面是 Miss");
    }

    // ---------------------------------------------------------------------
    // 12. 重绘：和输入彼此独立的一路；需要重绘的属性明确调 makeDirty
    // ---------------------------------------------------------------------
    {
        ink::InkingScene page("自检-重绘页");
        page.Resize(400, 400);
        ink::Button& button = page.Make<ink::Button>("自检-重绘按钮", "按钮");
        button.Resize(200, 100);
        button.ChangeZIndex(1);
        page.Bake();

        ink::InputRouter router;
        router.Attach(page);

        // 先把两边的脏都清干净：输入查到帧计数尾巴结束，重绘画到帧计数尾巴结束。
        router.BeginFrame(10.0f, 10.0f);
        while (router.BeginFrame(10.0f, 10.0f)) {
        }
        ink::RedrawScheduler::Reset();
        while (ink::RedrawScheduler::BeginFrame()) {
            ink::RedrawScheduler::EndFrame();
        }
        check(!router.Dirty() && !ink::RedrawScheduler::Dirty(), "两边都干净了");

        // 1) 外观变化：只重绘，不动命中
        const std::uint64_t bakes = page.BakeCount();
        button.SetFill(ink::rgb(10, 20, 30));
        button.SetLabel("改过的字");
        check(ink::RedrawScheduler::Dirty(), "换配色 / 改文字 → 明确调了 makeDirty");
        check(!page.NeedsBake() && page.BakeCount() == bakes, "外观变化不标命中：静态表不重烘");
        check(!router.Dirty(), "外观变化不标输入：命中没变");

        // 静止下来：连着画 kTailFrames 帧就停
        int painted = 0;
        while (ink::RedrawScheduler::BeginFrame()) {
            ink::RedrawScheduler::EndFrame();
            ++painted;
        }
        check(painted == ink::RedrawScheduler::kTailFrames, "标脏后画 kTailFrames 帧就停");
        check(!ink::RedrawScheduler::BeginFrame(), "干净时再问也不画");

        // 2) 几何变化：重绘和命中都要标
        button.Resize(240, 100);
        check(ink::RedrawScheduler::Dirty(), "几何变化 → 要重绘");
        check(page.NeedsBake(), "几何变化 → 命中表要重烘");
        page.Bake();
        check(page.BakeCount() == bakes + 1, "重烘就那么一次");

        // 3) 悬停 / 按下：输入驱动，但标脏走的是重绘那一路
        const std::uint64_t hoverBakes = page.BakeCount();
        ink::PointerEvent event;
        button.onPointerEnter(event);
        check(button.IsHovered() && ink::RedrawScheduler::Dirty(), "悬停 → 重绘标脏");
        check(!page.NeedsBake() && page.BakeCount() == hoverBakes,
              "悬停不动几何，静态表不用重烘");
        button.onPointerLeave(event);
        check(!button.IsHovered(), "离开复位");
    }

    // ---------------------------------------------------------------------
    // 13. 动态层级：父表留一个洞，动态层自己判，两者按 zindex 合并
    // ---------------------------------------------------------------------
    {
        ink::InkingScene page("自检-动态页");
        page.Resize(800, 600);

        ink::InkingScene& panel = page.Make<ink::InkingScene>("自检-静态面板");
        panel.Resize(400, 300);
        panel.ChangeZIndex(1);

        ink::InkingScene& floater = page.Make<ink::InkingScene>("自检-动态层", true);
        floater.ChangeOffset(100.0f, 100.0f);
        floater.Resize(200, 200);
        floater.ChangeZIndex(2);

        page.Bake();
        check(page.GetDynamicHoles().size() == 1, "动态子树在父表里就是一个洞");
        check(page.GetHitTable().Size() == 2, "表里没有动态层的条目（只有根 + 面板）");
        check(page.Query(150.0f, 150.0f).target == &floater, "z 高 → 动态层赢");
        check(page.Query(350.0f, 250.0f).target == &panel, "动态层外面归静态面板");

        // 动态层动起来：父表一次都不用重烘
        const std::uint64_t bakes = page.BakeCount();
        floater.ChangeOffset(300.0f, 200.0f);
        check(!page.NeedsBake(), "动态子树移动不标父表的脏");
        check(page.BakeCount() == bakes, "父表没重烘");
        check(page.IsTreeChanged(), "但命中可能过期，输入层要知道");
        check(page.Query(150.0f, 150.0f).target == &panel, "旧位置换成了面板");
        check(page.Query(350.0f, 250.0f).target == &floater, "新位置上是动态层");

        // zindex：谁在上面和遍历顺序无关
        floater.ChangeZIndex(-1);
        check(page.Query(350.0f, 250.0f).target == &panel, "z 压低 → 静态面板赢");
        floater.ChangeZIndex(5);
        check(page.Query(350.0f, 250.0f).target == &floater, "z 抬高 → 动态层又赢");

        // 每帧更新只叫醒动态子树：静态结点收不到 onTick
        struct CountedScene : public ink::InkingScene {
            CountedScene(const std::string& name, bool dynamic, int& counter)
                : ink::InkingScene(name, dynamic), counter(counter) {}
            void onTick(float) override { ++counter; }
            int& counter;
        };

        int dynamicTicks = 0;
        int staticTicks = 0;
        page.Make<CountedScene>("自检-会动的", true, dynamicTicks);
        page.Make<CountedScene>("自检-不动的", false, staticTicks);
        page.Bake();
        page.Tick(0.016f);
        check(dynamicTicks == 1, "动态子树每帧被叫醒");
        check(staticTicks == 0, "静态结点收不到 onTick");
    }

    // ---------------------------------------------------------------------
    // 14. 窗口主循环：只在显式开了无头冒烟时才跑
    //
    // 这一段会真的开窗，并且要等 INK_AUTOQUIT 到点才退出，
    // 所以默认跳过，只有 `INK_AUTOQUIT=1 ink_test` 才会走。
    // ---------------------------------------------------------------------
    if (SDL_getenv("INK_AUTOQUIT") != nullptr) {
        std::printf("无头冒烟：进入窗口主循环，约 2 秒后自动退出\n");

        // 开窗要有一个场景当根：拿菜单栏那套来跑，顺便验一遍这一趟的账。
        MenuBarFixture smoke;
        smoke.page.Bake();
        const std::uint64_t bakes = smoke.page.BakeCount();
        const std::uint64_t painted = ink::RedrawScheduler::PaintedFrames();
        const std::uint64_t skipped = ink::RedrawScheduler::SkippedFrames();

        window.Show(smoke.page.GetName());

        check(smoke.page.BakeCount() == bakes, "一路没人动，静态表一次都没重烘");
        check(ink::RedrawScheduler::PaintedFrames() > painted, "主循环至少画过一帧");
        check(ink::RedrawScheduler::SkippedFrames() > skipped, "静止时跳过了绘制");
        // Show() 返回说明主循环退出了；此时窗口句柄应该已经释放，
        // 再配置一次不会碰到已经销毁的 SDL 对象。
        check(window.setWidth(1280), "主循环退出后窗口仍可配置（句柄已释放）");
    }

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
