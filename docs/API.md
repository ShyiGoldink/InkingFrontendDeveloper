# InkingFrontendDeveloper API 文档

**关键词**
**| 可配置内容**
是指 设计常量（编译期定型，无 setter）
你不需要关心它在代码的哪里，只需要在文件夹CXXCSS按照需求写好配置文件即可
我们的代码生成器会在编译时在代码中准备好这些文件
**| 属性**
指的是某个类中的属性，属性A通常会提供GetA和SetA方法进行修改。如果没有提供setA，会在其之前标注[x]。
**| [final] 写入口**
指的是不可重写的写入口，例如 setPosition / setSize / setVisible / 层级。
标脏由框架在写入口内部统一完成，你不需要自己维护命中相关的脏标记。
**| [√] 钩子**
指的是可以重写的钩子，例如 onPositionChanged / onSizeChanged / onVisibleChanged。
钩子只用来挂附加逻辑，不负责标脏，也不拦截写入口。
（[√] 早前的定义是「可重写 setter」，现在语义收窄为「可重写钩子」。）
**| 标脏 / isDirty**
指的是「命中结果可能已经过期」这个标记。会标脏的只有四件事：几何（position / size）、
可见性（hide / show）、层级（zindex）、鼠标移动。颜色、文字、透明度、不影响命中的装饰动效都不标脏。
重绘不等于标脏——判断在写入口里完成，不在重绘时做。
**| X/x**
指的是屏幕横向的位置。通常以窗口的左上角为0，向右进行线性增加
**| Y/y**
指的是屏幕纵向的位置。通常以窗口的左上角为0，向下进行线性增加。
**| 自身锚点**
指的是组件内部的锚点位置，以百分比作为参考。以左上角为(0,0),右下角为(1,1)为标准参考系。任何组件在使用锚点时都会被视作“矩形”。自身锚点决定了自身以哪里作为与上级对应的方式。
**| 上级锚点**
指的是组件上级的锚点位置，以百分比作为参考。以左上角为(0,0),右下角为(1,1)为标准参考系。任何组件在使用锚点时都会被视作“矩形”。上级锚点决定了自身的锚点与上级的哪里作为对应。

## 编译期开关

指的是CMake选项，决定调试输出、日志与消息队列是否参与编译
关闭时对应的宏会整体展开成空语句，参数不会求值，对应的代码也不会进二进制
你不需要在代码里判断开关，编译器已经替你把它处理掉了
>**可配置内容**
ISLOG; #bool类型 是否让日志系统参与编译，默认打开，打开后输出可执行文件同级的Log.html
ISDEBUG; #bool类型 是否启用调试级日志与调试断言，Debug构建下默认打开；关闭时还会去掉控制台窗口（见下）
ISMESSAGE; #bool类型 是否让消息队列参与编译，默认打开，关闭后不占存储，也不会因为延迟消息拉起任务队列线程
>**如何配置**
在配置构建时传入即可，例如保留日志，关掉调试输出与消息队列
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DISDEBUG=OFF -DISMESSAGE=OFF
>**控制台窗口**
ISDEBUG为ON时，可执行文件是控制台程序，运行时会带一个黑框，调试输出直接看得到。
ISDEBUG为OFF时，凡是链接了ink_core的可执行文件都会被链成**GUI子系统**：双击运行不再弹控制台，适合交付。
这是链接期的决定，挂在ink_core的接口上，用add_subdirectory引入本库的使用方会自动继承，不需要自己写。
MinGW下用-mwindows，源码里照旧写int main()即可，不用改成WinMain；MSVC下用/SUBSYSTEM:WINDOWS配/ENTRY:mainCRTStartup。
注意：双击运行时看不到任何输出（printf、SDL_Log都一样）；从终端启动时标准句柄是继承来的，输出仍然看得到。
想在交付版里临时看输出，就从终端启动。

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
这个方法会**阻塞**：窗口开出来之后就进主循环，一直跑到窗口关闭（或按 ESC），
返回前把窗口释放掉。主循环每帧的顺序是：事件泵（退出 / 键盘 / 鼠标）→
鼠标坐标从窗口像素换算到设计坐标 → 每帧更新 → 绘制。
冷启动之外，只有场景回调里再调一次 Show() 会走到另一条分支：那时不阻塞，只换场景名。

## 鼠标输入

鼠标状态在 `include/input/MouseInput.h`，由窗口层的事件泵喂进去，它自己不做命中判定。
>**[final] MouseInput::UpdateFromSDL(const SDL_Event&);**
移动、按下、抬起都在这里变成状态。只更新状态，不派发事件——命中与投递留给下一帧的场景层，
这样才和 **isDirty + query** 的流程对得上。
>**MouseInput::GetState();**
拿鼠标状态。注意 `position` 是**窗口像素**（SDL 给的原样），`design` 才是**设计坐标**
（letterbox 换算之后），场景层只认后者。换算是渲染器的事，所以由窗口层每帧算一次写进来。
>**MouseInput::ResetFrameFlags();**
清掉瞬时状态。由窗口层在**帧末**调一次，别放进事件循环里——否则一帧中的多个移动事件
会被后一个提前清掉。

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

Scene是window之下最高的结点。**场景永远静态**：场景自身不做布局变化，没有位置与尺寸的写入口，
只负责「渲染 / 不渲染」。会动的东西压在widget层，由动态组件自己判断命中与自维护状态，
所以场景层的递归检查只需要查widget树。
**同期只有一个场景可渲染**：其它场景数据还在，但不渲染，需要手动关闭。
>**可配置内容**
DesignWidth; #int类型 设计界面宽度，你的场景将会基于该宽度进行设计
DesignHeight; #int类型 设计界面高度，你的场景将会基于该高度进行设计
SelfAnchorPointX; #float类型，决定你的自身锚点的X值
SelfAnchorPointY; #float类型，决定你的自身锚点的Y值
ParentAnchorPointX; #float类型，决定你的上级锚点的X值
ParentAnchorPointY; #float类型，决定你的上级锚点的Y值
>**锚点是初始化对齐，不是布局变化**
场景不做布局变化，不等于没有锚点：场景第一次渲染总要有对齐的锚点，决定它摆在窗口的哪里。
自身锚点决定自己以内部哪个位置去对应，上级锚点决定这个位置对上窗口的哪里。
锚点是**编译期定型**的：初始化时算一次，之后没有运行期位置写入口，
命中表也不会因为位置变化重烘——这正是场景能一直静态的前提。
>**InkingScene(string);**
构造函数，通过传入sceneName来作为你的场景名。为了保证编译期能够排查问题，推荐使用k常量进行命名。
当你使用构造函数之后，我们内部就会将其注册到场景库中以便窗口渲染。你可以在CXXCSS/Scene目录下创建场景名.json来为您的Scene做基础设置。样板可参考我们提供的Scene.example.json。
使用此构造函数后，场景的一些方法将被阻止使用以保证正确的输入响应：运行期的位置与尺寸写入口一律没有。
这是配置驱动的场景，接受代码生成器分析；生成器可以直接生成新类，在编译期就把行为和属性改掉。
>**InkingScene(SceneData);**
重载构造函数，通过传入数据结构SceneData来作为你的场景。使用该构造函数，你可以最大限度自定义场景
（例如尺寸或锚点来自运行期）。代价是不再享受生成器按配置生成的专用查找，输入响应降至普通水平。
>**[final] Scene::SetVisible(bool);**
场景层唯一的写入口。false 表示这个场景不渲染（数据还在），true 表示渲染。
几何不参与：场景没有 setPosition / setSize。
>**[√] Scene::onVisibleChanged(bool);**
可见性变化的钩子，用来挂附加逻辑。钩子不负责标脏，也不拦截写入口。
>**命中三态**
场景与组件统一走三态命中：Block（事件到此为止，半透明遮罩也属于这一类）、
PassThrough（有东西但让过）、Miss（这里没东西）。静态层查烘焙好的表，动态组件自己判断、
自己维护状态，两者按 zindex 合并。
悬停与点击共用同一次 query；点击不经过脏标记，单独查一次。
所有组件都要注册到一个场景里，设置父组件本质上不过是设置锚点或者layout。
详见 [InputDesign.md](InputDesign.md)。
