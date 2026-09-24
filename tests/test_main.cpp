// 自检程序：把日志、消息队列、任务队列和窗口的基本行为过一遍。
// 退出码非 0 表示有检查项失败。

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

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}

