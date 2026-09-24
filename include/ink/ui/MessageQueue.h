#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>
#include <vector>

#include <ink/dataStruct/MessageStruct.h>

namespace ink {

/**
 * @brief UI 消息队列，结构移植自后端的 UIMessageLibrary。
 *
 * UI 线程专门负责画，那么这个静态类专门负责攒消息。它是纯队列，
 * 不认识时间：延迟消息交给 TaskQueue 排一个延迟任务，到点再按"立即"入队。
 */
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

}  // namespace ink

