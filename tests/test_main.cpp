// 自检程序：把日志、消息队列、任务队列和窗口的基本行为过一遍。
// 退出码非 0 表示有检查项失败。

#include <ink/ink.h>
#include <input/MouseInput.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <any>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
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

}  // namespace

int main() {
    std::printf("InkingFrontendDeveloper %s（链接 SDL3 %s）\n",
                ink::kVersion, ink::sdl3_version().c_str());
    std::printf("编译期开关：日志=%s，调试=%s\n",
                ink::kLogEnabled ? "开" : "关",
                ink::kDebugEnabled ? "开" : "关");

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
    // ---------------------------------------------------------------------
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
    // 7. 窗口主循环：只在显式开了无头冒烟时才跑
    //
    // 这一段会真的开窗，并且要等 INK_AUTOQUIT 到点才退出，
    // 所以默认跳过，只有 `INK_AUTOQUIT=1 ink_test` 才会走。
    // ---------------------------------------------------------------------
    if (SDL_getenv("INK_AUTOQUIT") != nullptr) {
        std::printf("无头冒烟：进入窗口主循环，约 2 秒后自动退出\n");
        window.Show();
        // Show() 返回说明主循环退出了；此时窗口句柄应该已经释放，
        // 再配置一次不会碰到已经销毁的 SDL 对象。
        check(window.setWidth(1280), "主循环退出后窗口仍可配置（句柄已释放）");
    }

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}

