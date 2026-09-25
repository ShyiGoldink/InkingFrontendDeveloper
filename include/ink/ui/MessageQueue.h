#pragma once

// UI 消息队列，结构移植自后端框架的 UIMessageLibrary。
//
// 编译期开关：CMake 选项 ISMESSAGE → 宏 INK_ISMESSAGE
//   打开（默认）：消息真的进队列，延迟消息交给任务队列定时
//   关闭：本类退化成空实现，MessageQueue.cpp 不参与编译，
//         队列、互斥量、条件变量，以及由此拉起的任务队列线程统统不存在
//
// 调用请用 INK_MESSAGE_* 宏而不是直接调类方法：只有宏才能在关闭时整条消失，
// 直接调方法时参数照样会求值（字符串拼接、std::to_string 都躲不掉）。

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include <ink/dataStruct/MessageStruct.h>

#if defined(INK_ISMESSAGE)
#include <condition_variable>
#include <mutex>
#include <queue>
#endif

namespace ink {

#if defined(INK_ISMESSAGE)

class MessageQueue {
public:
    /**
     * 添加一条消息。
     * @param messageType 消息类型。
     * @param delayTime   延迟多少秒之后才输出，0 表示立即。
     * @param message     消息内容；空字符串会被忽略。
     */
    static void addMessage(MessageType messageType,
                           float delayTime,
                           const std::string& message);

    /** 快捷添加：按 bool 决定消息是通过还是错误。 */
    static void quickMessage(bool success,
                             float delayTime,
                             const std::string& message);

    /** 取出当前所有待处理消息，供 UI 线程统一重绘/输出。 */
    static std::vector<Message> drainMessages();

    /** 等待消息到达，供 UI 线程阻塞等待新事件。 */
    static void waitForMessage(std::chrono::milliseconds timeout);

    /** 当前队列里有多少条待处理消息，主要给自检用。 */
    static std::size_t pendingCount();

private:
    MessageQueue() = delete;
    ~MessageQueue() = delete;

    static std::mutex _mutex;                  /** 保护消息队列 */
    static std::condition_variable _condition; /** 通知 UI 线程有新消息 */
    static std::queue<Message> _messageQueue;  /** 待处理消息 */
};

#else  // !INK_ISMESSAGE

/**
 * 消息队列关闭时的空实现：不占存储，也没有锁和线程。
 *
 * 注意 waitForMessage() 会立即返回，所以关闭之后不要再拿它当帧节流，
 * 否则会变成空转；帧节奏应该来自 vsync 或渲染循环自己的计时。
 */
class MessageQueue {
public:
    static void addMessage(MessageType, float, const std::string&) noexcept {}
    static void quickMessage(bool, float, const std::string&) noexcept {}
    static std::vector<Message> drainMessages() { return {}; }
    static void waitForMessage(std::chrono::milliseconds) noexcept {}
    static std::size_t pendingCount() noexcept { return 0; }
};

#endif  // INK_ISMESSAGE

/** 消息队列是否在编译期启用。 */
#if defined(INK_ISMESSAGE)
inline constexpr bool kMessageEnabled = true;
#else
inline constexpr bool kMessageEnabled = false;
#endif

}  // namespace ink

// ---------------------------------------------------------------------------
// 调用宏：关闭时整条语句消失，参数不会求值，连字符串拼接都不会发生。
// ---------------------------------------------------------------------------
#if defined(INK_ISMESSAGE)
#define INK_MESSAGE(type, message) \
    ::ink::MessageQueue::addMessage((type), 0.0f, (message))
#define INK_MESSAGE_AFTER(type, seconds, message) \
    ::ink::MessageQueue::addMessage((type), (seconds), (message))
#define INK_MESSAGE_PASS(message) \
    ::ink::MessageQueue::quickMessage(true, 0.0f, (message))
#define INK_MESSAGE_ERROR(message) \
    ::ink::MessageQueue::quickMessage(false, 0.0f, (message))
#else
#define INK_MESSAGE(type, message) ((void)0)
#define INK_MESSAGE_AFTER(type, seconds, message) ((void)0)
#define INK_MESSAGE_PASS(message) ((void)0)
#define INK_MESSAGE_ERROR(message) ((void)0)
#endif

