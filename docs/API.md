# InkingFrontendDeveloper API 文档

**关键词**
**| 可配置内容**
是指 设计常量（编译期定型，无 setter）
你不需要关心它在代码的哪里，只需要在文件夹CXXCSS按照需求写好配置文件即可
我们的代码生成器会在编译时在代码中准备好这些文件
**| 属性**
指的是某个类中的属性，属性A通常会提供GetA和SetA方法进行修改。如果没有提供setA，会在其之前标注[x]。如果可以重写以便于你使用数据劫持，会在其之前标注[√]。

## 编译期开关

指的是CMake选项，决定调试输出、日志与消息队列是否参与编译
关闭时对应的宏会整体展开成空语句，参数不会求值，对应的代码也不会进二进制
你不需要在代码里判断开关，编译器已经替你把它处理掉了
>**可配置内容**
ISLOG; #bool类型 是否让日志系统参与编译，默认打开，打开后输出可执行文件同级的Log.html
ISDEBUG; #bool类型 是否启用调试级日志与调试断言，Debug构建下默认打开
ISMESSAGE; #bool类型 是否让消息队列参与编译，默认打开，关闭后不占存储，也不会因为延迟消息拉起任务队列线程
>**如何配置**
在配置构建时传入即可，例如保留日志，关掉调试输出与消息队列
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DISDEBUG=OFF -DISMESSAGE=OFF

## InkingWindow

InkingWindow是一个单例，应该作为整个可视化页面的入口
使用#include <InkingWindow.h>将其引入到你的代码中。
>**可配置内容**
DesignWidth; #int类型 设计界面宽度，你的页面将会基于该宽度进行设计
DesignHeight; #int类型 设计界面高度，你的页面将会基于该高度进行设计
>**InkingWindow::Instance();**
单例入口。考虑到可能你可能需要在适当的时候调用窗口，或者先对窗口进行一些调整，我将其设计为懒加载模式。
>**InkingWindow::Show(string sceneName);**
如果你准备好窗口，那么就开始展示吧。
使用sceneName决定展示窗口时，从哪一个场景开始。你的场景将会在创建时自动注册到场景库之中，详情请看**InkingScene**章节。

## InkLog

InkLog是一个静态类，负责把日志写进可执行文件同级的Log.html
页面按天和程序启动会话分组，多线程写入时加锁，旧格式的日志会先备份再重建
使用#include <ink/basic/InkLog.h>将其引入到你的代码中。
>**日志宏**
日志请用宏调用，不要直接调用类方法。只有宏才能在关闭ISLOG时整体消失，直接调方法则无论如何都会留下一次真正的函数调用
INK_LOG_INFO(string moduleName, string message); #普通日志，默认色
INK_LOG_PASS(string moduleName, string message); #自检通过，绿色
INK_LOG_WARN(string moduleName, string message); #警告，琥珀色
INK_LOG_ERROR(string moduleName, string message); #错误，红色
INK_LOG_DEBUG(string moduleName, string message); #调试，紫色，只有ISDEBUG打开时才会写入
INK_DEBUG_CHECK(bool condition, string message); #条件不成立时记一条错误日志，ISDEBUG关闭时整段消失
>**InkLog::logFilePath();**
返回Log.html的完整路径，位置是可执行文件同级目录

## MessageQueue

MessageQueue是一个静态类，专门负责攒UI消息，应该作为UI线程之外的消息入口
它是纯粹的队列，本身不认识时间：延迟消息会交给任务队列排一个延迟任务，到点再按立即重新入队
使用#include <ink/ui/MessageQueue.h>将其引入到你的代码中。
>**消息宏**
消息请用宏调用，不要直接调用类方法。只有宏才能在关闭ISMESSAGE时整体消失，直接调方法则无论如何都会留下参数求值的开销
INK_MESSAGE(MessageType type, string message); #立即入队
INK_MESSAGE_AFTER(MessageType type, float seconds, string message); #延迟seconds秒之后入队
INK_MESSAGE_PASS(string message); #立即入队，通过
INK_MESSAGE_ERROR(string message); #立即入队，错误
>**MessageQueue::addMessage(MessageType type, float delayTime, string message);**
添加一条消息。delayTime是延迟多少秒之后才输出，0表示立即；message为空时会被忽略
>**MessageQueue::quickMessage(bool success, float delayTime, string message);**
快捷添加消息，通过bool快捷决定消息类型，true是通过，false是错误
>**MessageQueue::drainMessages();**
取出当前所有待处理消息，供UI线程统一重绘/输出
>**MessageQueue::waitForMessage(chrono::milliseconds timeout);**
等待消息到达，供UI线程阻塞等待新事件
>**MessageQueue::pendingCount();**
当前队列里有多少条待处理消息，主要给自检用

## TaskQueue

TaskQueue是一个单例，内部挂着一个ThreadPool，默认4个管家线程，空闲时休眠
队列按到期时刻排序，所以延迟任务和立即任务可以混在一起，立即任务之间仍然是先进先出
使用#include <ink/thread/TaskQueue.h>将其引入到你的代码中。
>**TaskQueue::instance();**
单例入口，第一次调用时才创建
>**TaskQueue::addTask(Task task);**
添加任务（线程安全），任务会整体（含依赖与后继）在某个管家线程中执行
>**TaskQueue::addTask(Task task, chrono::milliseconds delay);**
延迟添加任务（线程安全），delay之后才会被管家线程取走执行
>**TaskQueue::isEmpty();**
返回任务队列是否为空（线程安全，含还没到期的延迟任务）
>**TaskQueue::hasDueTask();**
返回队列里是否有已经到期的任务（线程安全），这才是真正的唤醒条件
>**TaskQueue::timeUntilNextDue();**
返回距离最近一个任务到期还有多久（线程安全），队列为空时返回一个较大的值

## Task

Task是本项目的任务模式，属性为依赖、动作、后继
动作的返回值会传递给后继，依赖的返回值会按顺序作为本节点动作的入参
使用#include <ink/dataStruct/TaskStruct.h>将其引入到你的代码中。
>**Task::dependOn(function);**
添加一个依赖：该依赖的返回值会成为本节点动作的入参之一。也支持传入函数vector一次添加多个
>**Task::then(function);**
追加一个后继：本节点执行完后，把结果作为后继动作的唯一入参。也支持传入函数vector串成一条链
>**Task::execute(input);**
按左（依赖）中（动作）右（后继）的中序遍历执行

## InkingScene

Scene是window之下最高的结点。你需要设计自己的Scene界面，如果该Scene界面中没有任何动态添加的Ui
请在您的Scene将属性“isDynamic”设置为false，我们会生成对应的优化代码来优化该Scene的点击事件。
