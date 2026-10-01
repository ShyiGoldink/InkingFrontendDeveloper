# InkingFrontendDeveloper API 文档

**关键词**
**| 可配置内容**
是指 设计常量（编译期定型，无 setter）
你不需要关心它在代码的哪里，只需要在文件夹CXXCSS按照需求写好配置文件即可
我们的代码生成器会在编译时在代码中准备好这些文件
**格式以 [../CXXCSS/CXXCSS.md](../CXXCSS/CXXCSS.md) 为准**——那份写了目录与命名、
公共骨架、形状层词汇表、Button 的完整字段，以及"哪些是纸面约定、哪些已落地"。
**| 属性**
指的是某个类中的属性，属性A通常会提供GetA和SetA方法进行修改。如果没有提供setA，会在其之前标注[x]。
**| [final] 写入口**
指的是不可重写的写入口：结构性变化（ChangeZIndex / SetParent）两版共用，
几何变化（Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset）只有动态版有。
一律不加 virtual，子类不可重写。
标脏由框架在写入口内部统一完成，你不需要自己维护命中相关的脏标记。
**| [√] 钩子**
指的是可以重写的钩子，例如 onSizeChanged / onSelfAnchorChanged / onTraceAnchorChanged /
onOffsetChanged / onZIndexChanged / onParentChanged / onDirty / onTick（只有动态组件收得到 tick）。
钩子只用来挂附加逻辑，不负责标脏，也不拦截写入口。
（[√] 早前的定义是「可重写 setter」，现在语义收窄为「可重写钩子」。）
**| 标脏 / isDirty**
指的是「命中结果可能已经过期」这个标记。会标脏的只有四件事：几何（position / size）、
可见性（hide / show）、层级（zindex）、鼠标移动。颜色、文字、透明度、展示倍率、不影响命中的
装饰动效都不标脏。
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
>**继承**
InkingWindow继承InkingStaticAnchor——它是整个项目的根锚点，所以窗口自己也不做布局变化。
两套尺寸要分清：
基类的GetWidth / GetHeight是"根锚点的尺寸"，也就是设计画布大小（可配置内容里的DesignWidth / DesignHeight）；
自己的GetWindowWidth / GetWindowHeight才是窗口像素尺寸，运行期可调（setWindowWidth / setWindowHeight）。
窗口怎么拉都不改设计坐标里的几何，只会改展示倍率（Magnification）。
>**可配置内容**
DesignWidth; #int类型 设计界面宽度，你的页面将会基于该宽度进行设计
DesignHeight; #int类型 设计界面高度，你的页面将会基于该高度进行设计
>**InkingWindow::Instance();**
单例入口。考虑到可能你可能需要在适当的时候调用窗口，或者先对窗口进行一些调整，我将其设计为懒加载模式。
>**InkingWindow::GetWindowWidth / GetWindowHeight / setWindowWidth / setWindowHeight;**
窗口像素尺寸，运行期可调。窗口已经开出来时会立刻跟随；还没开就只是记下来，等Show()时用。
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
这个换算里含黑边偏移，必须走渲染器，不能拿展示倍率乘除。好处是：整体缩放（窗口尺寸、
DPI 变化）不会让命中表失效——表建在设计坐标里，缩放只改展示倍率。
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
>**继承**
InkingScene继承InkingStaticAnchor（见下文「InkingAnchor」章节）：场景永远静态，
所以几何写入口在类型上就不存在，只有可见性这一档写入口。
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
**构造即登记**：构造函数内部通过 RAII 令牌把 this 登记进 SceneLibrary，析构时自动注销，
你不需要（也不应该）自己写注销，详见下文「SceneLibrary」章节。
**名字被占用时注册被驳回**：场景进入**停放态**——对象在、数据在，但永远不渲染也不 tick，
`IsRejected()` 为 true。要复用这个名字，先 `SceneLibrary::CloseScene(名字)`。
你可以在CXXCSS/Scene目录下创建场景名.json来为您的Scene做基础设置。样板可参考我们提供的Scene.example.json。
使用此构造函数后，场景的一些方法将被阻止使用以保证正确的输入响应：运行期的位置与尺寸写入口一律没有。
这是配置驱动的场景，接受代码生成器分析；生成器可以直接生成新类，在编译期就把行为和属性改掉。
>**InkingScene(); / InkingScene(string, AnchorData);**
无参构造：不绑定场景名，不登记进库。
带 AnchorData 的构造：自己给尺寸与锚点，读 CXXCSS 那条路之外的自定义方式。
>**Scene::GetSceneName();**
取场景名。构造时定下，没有写入口。
>**Scene::IsRegistered() / IsRejected() / IsClosed();**
在库里吗 / 注册被驳回了（停放态）吗 / 被 CloseScene 关掉过吗。
>**Scene::IsActive();**
是不是"正在渲染"的那一个。由 SceneLibrary::SetActiveScene 维护，同期最多一个为 true。
>**两组逻辑**
场景的回调按会不会"跟着渲染"分成两组，由框架按状态分别开关：

| 回调 | 谁驱动 | 什么时候跑 |
| --- | --- | --- |
| `onTick(double fixedStep)` | 时间线的**固定逻辑步**（默认 50Hz） | 只要场景没被关闭就一直跑 |
| `onRenderBoundTick(double delta)` | 每个**渲染帧**一次 | 只在**活跃**（正在渲染）时跑 |

于是三种状态就是这两组的组合：

- **活跃**：渲染 + 两组逻辑都跑；
- **静默**（被别的场景顶掉）：不渲染，跟着渲染的那组停；**固定逻辑步照跑**
  ——后台的事情不该因为切场景就停摆。数据能读，所以可以先把数据拿完再决定去留；
- **停放**：注册被驳回的场景，什么都没开始，永远不会跑。

"需要不渲染但逻辑动"的场景，用 `onTick` 就行——它不依赖活跃态；
反过来"不渲染就彻底别动"，把逻辑放 `onRenderBoundTick`。
>**驱动方式：TickLogic(fixedStep) / TickFrame(delta);**
由框架每帧调（窗口主循环 + `ink::Timeline`）。前者派发固定逻辑步，
并且会顺带遍历绘制列表里的**动态组件**逐个 `Tick(fixedStep)`——
它们才是"几何会自己变"的那一档，静态组件压根没有这个入口。
后者只在活跃时调 `onRenderBoundTick`。
>**更细的频率（例如 24fps 的动画）不走这两个钩子**
去订阅 `ink::Timeline`（见上文「Timeline」章节），那里按需采样、
回调收到实际步长，慢帧也不会走偏。
>**[final] Scene::SetVisible(bool);**
可见性的写入口在 `InkingAnchor` 上（场景与组件共用，组件也要能藏起来），
不在场景独有的接口里。场景继承它，所以场景能藏；组件同理。
>**为什么登记只允许在 UI 线程**
场景的构造与析构都发生在窗口主循环那一条线程上，所以 SceneLibrary 有意不加锁
（后端 ShineStatusChecker 加锁是因为模块会被线程池里的多个管家线程读写）。
真出现跨线程场景，再加锁。
>**渲染树：层级是真相，绘制列表是索引**
子节点在构造时沿父链找到自己所属的场景并登记进来，场景这边只维护一份
**按 z 序排好的扁平数组**。父子关系（`_parent`）才是唯一真相源，绘制列表只是它的
派生视图——结构一变就重建，绝不反过来拿列表当真相（docs/InputDesign.md §8）。
>**为什么扁平而不是递归遍历**
绝对坐标必须走父链，而 `GetAbsX()` 是 O(深度)；扁平列表里父指针是现成的，取绝对坐标不用额外结构。
命中侧（docs/InputDesign.md §10.2）已经定了「压平成 HitTable + 平铺 _drawList」，
渲染跟着扁平，两边遍历才不会越走越远。线性遍历对缓存友好，也没有深递归的栈风险。
>**顺序怎么维护：标脏 + 帧末惰性重建**
骨架变化（构造 / 析构 / SetParent / ChangeZIndex / SetVisible / 动态几何写入口）
只把场景标脏，不立刻排序；重建推迟到 `Render()` 开头做一次。
一帧里连改十个 zindex 也就重建一次。
>**提交顺序是 z 从小到大**
z 小的先提交、先画（在底下），z 大的后提交、盖在上面。
**注意这和 `IsAbove` 的答案相反**：IsAbove 回答「谁在上面」（命中要从高到低问），
绘制只是把它反过来排，规则本身是同一条。
>**可见性在提交时判断，不从列表里删**
隐藏的节点**留在列表里**（否则"再显示出来"没有谁会把它加回去），提交时跳过。
不可见的父级会把自己和整棵子树一起短路（`IsVisibleInTree`）。
>**绝对坐标：静态用快照，动态每帧现算**
静态组件几何构造即定型，列表里存建表时算好的绝对坐标；动态组件每帧都在动，必须现算，
否则会「画在这儿、点在别处」。动态节点的几何写入口会通知场景重算缓存，
这样它的**静态子孙**的坐标才不会停在旧值上。
>**zindex 下限 = 背景层**
`InkingZIndex::background`（-128）。绘制按 z 从小到大做，所以铺满整屏、又想待在一切之下的
背景板必须有一个比其它组件都低的 z；允许任意低会让"谁盖谁"取决于注册序号。
低于下限的值一律夹到下限。
>**指针输入（今天的最小闭环，不是完整命中方案）**
`SetPointerState(designX, designY, down, inside);` 由窗口层每帧喂一次
（坐标是**设计坐标**；换算走渲染器，因为 letterbox 有黑边偏移，场景层不认识渲染器）。
`DispatchPointer();` 每帧派发一次：悬停进出 → 按下 → 抬起触发点击。
实现是**线性扫绘制列表、从列表尾往前取最上面那个**（列表按 z 升序，尾在最上），
命中问节点的 `HitTest`。规则：按下时抓住命中的那个（之后拖出去也还是它），
抬起时**指针还在它身上**才算点击，否则算取消（用户唯一能表达"我反悔了"的方式）。
`GetHoveredNode() / GetPressedNode();` 是给自检用的观测口。
**刻意没做**：静态烘焙的命中表、三态 `Block / PassThrough / Miss`、`isDirty` 帧计数
——那是 `InputDesign.md` 的完整方案。接口按那时候的形状定了，换实现时调用方不用改。
>**场景构造期建的子节点会被补登记**
场景是"构造完成才盖章成渲染树的根"的，所以 `InkingScene` 构造函数体里建的按钮
沿父链找不到场景。`finishConstruction()` 会回头把绘制列表里 `_scene == nullptr`
的节点补登记一遍。不补这一趟的后果是"按钮既不渲染也点不到，而且一声不响"。
>**Scene::Render(SDL_Renderer*);**
渲染入口，按 z 序把每个节点提交给渲染器。约定**只有 InkingWindow 该调它**，
业务代码别调。渲染器为空时安全空转（无头环境下不崩）。
>**Scene::GetDrawList() / GetDrawItemCount();**
把绘制顺序重建到最新，返回只读的扁平列表 / 项数。没脏时直接返回现成的，脏了才重建一次。
主要给窗口层与自检用。
>**[√] Scene::onVisibleChanged(bool);**
可见性变化的钩子，用来挂附加逻辑。钩子不负责标脏，也不拦截写入口。
>**命中三态**
场景与组件统一走三态命中：Block（事件到此为止，半透明遮罩也属于这一类）、
PassThrough（有东西但让过）、Miss（这里没东西）。静态层查烘焙好的表，动态组件自己判断、
自己维护状态，两者按 zindex 合并。
悬停与点击共用同一次 query；点击不经过脏标记，单独查一次。
所有组件都要注册到一个场景里，设置父组件本质上不过是设置锚点或者layout。
详见 [InputDesign.md](InputDesign.md)。

## SceneLibrary

场景库：登记与查找场景的静态类，结构对照后端 InkingBackendFramework 的 ShineStatusChecker。
场景构造时把自己登记进来，窗口层 / 生成器据此把场景名换成 InkingScene\*，全程不用认识具体场景类型。
使用#include <scene/SceneLibrary.h>将其引入到你的代码中。
>**同名同期只能有一个**
名字被占了，后来者注册**当场驳回**——对象还是构造出来了，但它永远不进库、
永远不会被播放（停放态）。想重新用这个名字，得先把占用者
`SceneLibrary::CloseScene(名字)` 关掉。
这条规矩让"按名字找场景"永远只有一个答案，窗口层和生成器都不用再处理"同名多个怎么办"。
>**你通常不需要直接调用它**
登记与注销由 InkingScene 内部的 RAII 令牌完成；窗口层调的是
`FindScene` / `SetActiveScene` / `RenderScene`。
>**SceneLibrary::RegisterScene(string sceneName, InkingScene\* scene);**
登记一个场景，返回 `RegisterResult`：`Ok` / `NameTaken`（同名已占用，驳回）/
`Invalid`（名字或指针为空）。**不覆盖、不排队**。
>**SceneLibrary::UnregisterScene(string sceneName, InkingScene\* scene);**
注销。只在该名字当前正指向这个场景时才真的摘掉（指针不匹配就什么都不做），
所以"库里已经换人了"不会误删别人的登记。
>**SceneLibrary::FindScene(string sceneName);**
按名字找场景；没注册过返回 nullptr。
>**SceneLibrary::CloseScene(string sceneName);**
关掉一个场景：从库里摘掉并标记为已关闭，**不析构对象**——生命周期始终归调用方。
名字随即空出来，可以再建一个新的同名场景。
被关掉的场景不渲染、不再活跃、两组逻辑都不跑，**但数据全部保留、仍然可读**；
"先把重要数据 move 出来再关、回头传回新场景"就是这么用的。
这也是避免野指针的正路：库先放手，调用方再决定何时析构。
>**SceneLibrary::SetActiveScene / GetActiveScene();**
设置 / 读取当前**活跃**（正在渲染）的场景。同一时刻最多一个；
切场景时**旧的自动进入静默**（不渲染、跟着渲染的逻辑也停）。
传 nullptr 表示谁都不活跃。
>**SceneLibrary::RenderScene(SDL_Renderer\*);**
渲染当前活跃场景。**窗口层每帧调这个**，不要直接去碰场景的 `Render`——
那是 protected，只有框架该调。从库这里走一道，"只有活跃场景会被渲染、
每个场景一帧只渲染一次"这条规矩就有了唯一落点；
将来上合批（DrawCall 合批 + 静态烘焙）时，提交层也正好落在这里。
>**SceneLibrary::GetAllSceneNames() / SceneCount() / IsNameTaken(string);**
枚举与计数。一个名字算一个场景。
>**存储用函数内静态量，不用类的静态数据成员**
后端的表是类的静态数据成员，在 main 之前按翻译单元顺序初始化；静态场景对象可能先构造，
于是会出现「场景活了库还没起来」，更糟的是「场景在库已经析构之后才注销」——静态初始化 /
析构顺序问题。函数内静态量首次使用时才初始化，谁先登记就把库先拉起来，库一定活得比登记者久。
>**为什么这里不加锁**
后端加锁是因为模块会被线程池里的多个管家线程读写。本项目的场景层只在 UI 线程构造与析构，
加锁既挡不住误用，又白付一次原子操作。

## SceneRegisterToken

基于 RAII 思想的登记令牌，对照后端 InkingBackendFramework 的 StatusRegisterToken。
「构造即登记、析构即注销」，让调用方不可能忘记写注销：令牌是被登记对象的成员，
对象析构时令牌一定先析构（成员先于基类、也先于本翻译单元的其它静态量），所以注销不会漏、也不会晚。
使用#include <scene/SceneRegisterToken.h>将其引入到你的代码中。
>**ink::RegisterToken<Registered, Library>**
模板化的令牌：后端只有 ShineBasicModule 一种被登记类型，所以把指针类型写死了；
这里将来被登记的类型不止场景一种（控件、面板……），做成模板就不必每来一种类型抄一遍相同的 RAII 代码。
场景侧的类型别名是 InkingScene::SceneToken。
>**RegisterToken(string name, Registered\* object);**
构造即登记。名字为空或对象为空时**不登记**：这两种情况登记进去也查不到，
还只会在库里留下一个空 key。这也是「无参构造的场景不登记」的实现方式。
>**~RegisterToken();**
析构即注销。可移动、不可拷贝：令牌必须和被登记的对象一一对应，
允许拷贝就会出现两份令牌抢着注销同一条登记。
>**InkingScene 里的用法**
令牌以 std::optional 形式延迟构造（构造函数体内 emplace，不在初始化列表里），
对应后端 ShineBasicModule::registerToStatusChecker() 的写法。
成员声明顺序是 `_sceneName` → `_sceneRegisterToken`：成员按声明顺序构造、逆序析构，
所以注销用的 key 在整个析构过程中都有效。

## InkingAnchor

整个项目的锚点：每个UI和窗口都以它为基类。**位置不作为字段存在**，而是由锚点推导——
自身矩形里的「自身锚点」落到父级矩形里的「上级锚点」上，再叠偏移。
使用#include <ink/basic/InkingAnchor.h>将其引入到你的代码中。
>**两种锚点**
判据只有一条：几何会不会在构造之后自己变。
InkingStaticAnchor; #静态组件：构造即定型，之后几何不动，进命中表。几何写入口一个都没有，类型上就调不到
InkingDynamicAnchor; #动态组件：几何会自己变，不进表、自己维护命中状态，每帧收到一次Tick
两版共用InkingAnchor的只读查询、层级、父级、展示倍率、标脏、IsAbove。也就是说「静态」少的只有几何写入口，
可见性 / 层级 / 增删这些都属于结构性变化，两个版本都有。
InkingAnchor本身不可直接实例化——每个组件声明时必须选一种；场景和窗口都继承静态版。
分流不靠运行期标记：谁属于哪一档，在登记那一刻就是明确的（静态的登记进表、动态的登记进动态档）。
>**可配置内容**
Width; #int类型 自身宽度，设计坐标
Height; #int类型 自身高度，设计坐标
SelfAnchorPointX; #float类型 自身锚点的X
SelfAnchorPointY; #float类型 自身锚点的Y
TraceAnchorPointX; #float类型 上级锚点（追踪的父级锚点）的X
TraceAnchorPointY; #float类型 上级锚点（追踪的父级锚点）的Y
OffsetX; #float类型 对齐之后再挪一点，设计坐标
OffsetY; #float类型 对齐之后再挪一点，设计坐标
ZIndex; #int类型 越大越靠上，同场景内全局比较；下限是 InkingZIndex::background
Visible; #bool类型 自己这一层的可见性，默认true
Color; #0xAARRGGBB 占位填充色，默认0xFF569CD6；**颜色不标脏**
>**InkingAnchor(InkingAnchor* parent, AnchorData data);**
静态初始化：一次把尺寸、两个锚点、偏移、层级给全。根（窗口 / 场景）传nullptr当parent。
>**InkingAnchor(InkingAnchor* parent, string name);**
配置驱动：只给parent和名字，具体怎么读CXXCSS由子类自己实现。
>**[final] 写入口：骨架变化（两版共用）**
SetVisible / ChangeZIndex / SetParent。标脏与钩子都在内部完成，所以不存在「哪次改动忘了标脏」；
返回bool表示这次是否真的改了，没变就不标脏、不触发钩子。
可见性算骨架变化：它改变绘制列表的内容，所以除标脏外还会通知场景重排。
>**[final] 写入口：几何变化（只有动态版有）**
Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset。
静态组件里没有这几个名字，**基类里也没有**——所以连命中表里存的基类指针都改不了几何。
>**[√] 钩子**
onSizeChanged / onSelfAnchorChanged / onTraceAnchorChanged / onOffsetChanged / onZIndexChanged /
onParentChanged / onVisibleChanged / onDirty / onRender / onTick（只有动态组件收得到）。
钩子只挂附加逻辑，不标脏、不拦截写入口。
>**IsVisible 与 IsVisibleInTree**
前者只回答「自己这一层」，后者沿父链问到根。**渲染只认后者**：父级不可见，整棵子树都不提交。
>**IsAbove 与绘制顺序的关系**
IsAbove 回答「谁在上面」（z 大者、z 相同时注册序号大者），是命中要问的问题。
绘制提交的顺序**恰好相反**（按 z 从小到大，先画的在底下），但用的规则完全一致。
两边规则必须一致，否则会出现「画的是 A、点到的是 B」。
>**注册序号是构造时自动赋的**
`_registerOrder` 由进程内计数器在构造时递增赋值，表示「后构造的在上」。
它是 z 相同时唯一的先后依据，所以顺序是确定的，不看遍历顺序。
>**每帧的Tick**
InkingDynamicAnchor::Tick(float deltaSeconds); 由场景调，每帧一次；静态组件压根没有这个入口。
>**[x] 展示倍率**
Magnification / DeviceWidth / DeviceHeight。设计单位 → 设备像素的倍率（letterbox整幅等比缩放 × 系统DPI），
由窗口层写入，只影响绘制换算，不标脏。它不参与布局：所有组件一视同仁，没有「某个组件不跟着缩放」这回事。
>**第0层渲染：默认画一块实心矩形**
`InkingStaticAnchor` / `InkingDynamicAnchor` 都重写了 `onRender`，默认按 Color 填一块矩形
（尺寸 × 展示倍率）。子类不重写就有东西可看；SDF 形状层接手后换掉这一处即可。
基类 `InkingAnchor::onRender` 是空的——它本身不可实例化，不负责画。
>**颜色的 alpha 会真的参与合成**
窗口在拿到渲染器之后会打开绘制混合（`SDL_SetRenderDrawBlendMode(renderer,
SDL_BLENDMODE_BLEND)`）。SDL 的**绘制**混合默认是关的，那时颜色的 alpha
会被原样写进像素，"半透明"根本不存在——所以现在 `AnchorData::color` /
`ButtonAppearance::color` 的 alpha 才真的有意义。
写自检时注意：离屏断言要把底色清成**不透明**色，否则"混合到 alpha=0 的目标上"
结果恰好等于"原样写入"，混合开没开读出同一个值，那条断言等于没写
（`docs/AGENTS.md §6` 第 32 条）。
>**关于尺寸的边界**
尺寸是设计坐标里的固定值，不随父级等倍扩展（「填充父级」这类是layout通道的事，尚未实现）。
所以静态组件的几何真的可以一次定型。
>**[√] HitTest(worldX, worldY);**
指针在这个位置上吗，坐标是**世界坐标（设计空间）**。基类默认返回 false——
"怎么算在里面"是具体组件的知识（按钮问自己的 SDF 形状）。
**z 序不在这里判**：那由调用方按绘制列表从高到低问
（`InkingScene::DispatchPointer`），两条顺序规则只有一份。
>**RebindScene(InkingScene*);**
重新认定"我属于哪个场景"并补登记进它的绘制列表。只给场景在构造末尾用：
场景构造函数体里建出来的子节点那时还没有场景可认，需要回头补一趟。
已经在列表里的节点是空操作（`AddDrawNode` 自己查重）。别在业务代码里手工调。

## 代码生成器（inkgen）

把 `CXXCSS/Button/*.json` 在**构建期**变成 C++ 代码：校验配置 → 生成
"名字常量 + 登记入口"的头，以及"配置数据 + 启动期自动登记"的源文件。
使用方式很简单——配置列表交给 CMake，代码里 include 生成的头。
（源码在 `tools/inkgen/`，只依赖标准库；改生成规则动那里。）
>**产物**
```text
build/<预设>/inkgen/<名字>/button_service.h     常量 + RegisterAllButtons() 声明
build/<预设>/inkgen/<名字>/button_register.cpp  配置 + 启动期静态登记
```
头文件里有：
- `inline constexpr const char* k<按钮名>Name`（名字是合法标识符时才生成）；
- `inline constexpr std::size_t kButtonCount`；
- `void RegisterAllButtons();`（幂等：先撤下上一批、再重新登记）。
源文件里有一个**启动期就位**的静态对象自动调它——调用方不需要记得手调任何东西。
>**产物只落 build 目录**（`docs/AGENTS.md §3` 第 6 条），源码树保持干净。
>**CMake 一行接入**
```cmake
file(GLOB MY_CONFIGS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/CXXCSS/Button/*.json")
add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ink::core)
ink_add_generated(my_app
    OUT_DIR   "${CMAKE_BINARY_DIR}"           # 加进 include 路径的根
    NAMESPACE "ink::cxxcss"                   # 生成代码的命名空间
    HEADER    "inkgen/my_app/button_service.h"# 产物相对 OUT_DIR 的路径
    SOURCES   ${MY_CONFIGS})
```
之后在代码里直接 `#include <inkgen/my_app/button_service.h>`，
用 `ink::cxxcss::k<名字>Name` 当查找键。
`*.example.json` 会被自动跳过（`CXXCSS.md §1`）；不传 SOURCES 会当场报错，
省得"生成了一个空表"这种静默失败。
>**inkgen 的命令行**
```text
inkgen --out-dir <目录> [--namespace <ns>] [--header <相对路径>] <file.json>...
inkgen --validate <file.json>...        # 只校验不写文件（CI / 自检用）
```
退出码 0 / 1（有错误时**逐条**打印，每条带文件与行号）。手写脚本或接别的构建系统时用它。
>**校验的调子：报错停下**
按 `CXXCSS.md §3.6`：未知字段、`name` 缺失或与文件名不符、尺寸缺失 / 非正、
颜色段数不对、负半径、不认识的 `type` / `shape.type` 一律**报错**并列出已知取值；
第二 / 第三档形状报"尚未实现"（区别于"不认识"）；
`img` / `svg` 两种外观能过，但打一条"还没落地"的提醒。
唯一自动修正的是形状半径的**上限**（`min(w,h)/2`，那是数学要求）。
>**重名在生成期就被拦下**
两个文件认领同一个 `name` 时，后到者报"已经被 xxx 用了"。
运行期 `ButtonLibrary` 也会驳回后来者，但那是**静默**的错（按钮长得不对、却没人报错），
所以在生成期拦更值钱。

## 形状层（InkingShapeSpec）

形状用 **SDF（带符号距离场）** 描述，一个形状只有一份定义，**渲染 / 命中 / AABB
三者都从它派生**——各写一遍必然出现"画的是圆角、点到的是直角"，两边单独看都对、
合起来才错（见 [../CXXCSS/CXXCSS.md](../CXXCSS/CXXCSS.md) §3）。
使用 `#include <ink/dataStruct/InkingShapeSpec.h>` 将其引入到你的代码中。
>**符号约定：负 = 内**
`SignedDistance` 返回 `f`：`f < 0` 在内部、`f = 0` 在边界、`f > 0` 在外部；
裁剪 = 保留 `f <= 0` 的区域。这是公开 SDF 参考实现的通用约定，照抄不用翻符号。
>**ShapeSpec;**
一个形状的定义：种类 + 只属于它的参数（不把参数编进字符串）。用工厂函数造：
`ShapeSpec::Rect()` / `RoundedRect(radius)` / `Circle()` / `Ellipse()`。
四种语义：
- `rect` 用满 `width × height`；
- `roundedRect` 用满 `width × height`，四角半径 `radius`；
- `circle` **居中**、直径取 `min(width,height)`——100×60 的组件画圆得到的是
  居中直径 60 的圆，要椭圆请写 `ellipse`；
- `ellipse` 用满 `width × height`。
>**ShapeSpec::RoundedRect(radius) 的半径会夹到 min(w,h)/2**
这是形状本身的数学要求（超过一半就画不成圆角矩形），属于"语义上无歧义的夹取"，
不报错。负半径是配置错误，会触发 `assert`（Debug 构建当场停）。
>**SignedDistance(shape, x, y, width, height);**
点的带符号距离，单位是设计坐标，`x`/`y` 相对组件左上角。
>**ShapeContains(shape, x, y, width, height);**
这个点在不在形状里（判据就是 `f <= 0`）。命中判定用的就是它，
所以命中区域和形状定义永远一致。
>**GetShapeBounds(shape, width, height);**
形状的包围盒（AABB），相对组件左上角。将来命中表按格子索引时问这个——
`circle` 只占中间一块，不能一律拿 `width × height` 顶上。
>**落地状态**
`SignedDistance` / `ShapeContains` / `GetShapeBounds` 都已实现且自检覆盖。
**但填充还没走 SDF**：现在画的是形状包围盒那个直角矩形
（`ink::detail::fillShapeBounds`），所以圆角按钮现在"命中按圆角、画出来是直角"。
抗锯齿填充要换 SDL3 GPU API 或走 `SDL_RenderGeometry` 三角化，方案待定
（见 `docs/TempTask.md` 第 2 步）；定下来之后只换那一个函数，调用方不用改。

## InkingColor

颜色在本项目内部统一用 **0xAARRGGBB 的 32 位整数**（A 在高 8 位），
于是 `0xFF569CD6` 一眼能读出"不透明的 #569CD6"。
使用 `#include <ink/dataStruct/InkingColor.h>` 将其引入到你的代码中。
>**为什么不是浮点 RGBA 结构体**
渲染器最终要的就是 8 位通道，锚点的 `AnchorData::color` 也已经是这个形式；
再存一份浮点表示只会多一个"两份真相要同步"的地方。
CXXCSS 里的 `"0|0|0|0.75"`（0~1 浮点、`|` 分隔）是**文件格式**，
由生成器在编译期换成整数，运行期不解析字符串。
>**MakeColor(r,g,b,a) / FromUnitRgba(r,g,b,a);**
打包成 0xAARRGGBB；后者接收 CXXCSS 那种 0~1 的写法，
超出 `[0,1]` 的通道会被**夹住**而不是溢出成另一个颜色。
>**ColorAlpha / ColorRed / ColorGreen / ColorBlue(color);**
取单个通道（0~255）。`ToUnitRgba` 反向换算成 0~1，主要给自检和日志用。

## InkingStaticButton

本项目**第一个完整组件**，也是"一个组件该怎么写"的样板。
使用 `#include <button/InkingStaticButton.h>` 将其引入到你的代码中。
>**它是静态组件（这个选择不能回头改）**
判据是"几何会不会在构造之后自己变"：常 / 悬 / 按三态**只换颜色**，
而颜色不标脏；按下真要做"下沉"效果，那是 `onRender` 里的**绘制偏移**，
不是几何变化。所以静态档就够，能进命中表。
静态组件没有几何写入口——连命中表里存的 `InkingAnchor*` 都改不了它的几何。
`src/ink.cpp` 里有编译期断言钉住这条（出现 `Resize` / `ChangeSelfAnchor` /
`ChangeTraceAnchor` / `ChangeOffset` 之一就当场构建失败）。
真要"按下会撑开布局"的按钮，那是另一个类型，不要在这里加几何写入口。
>**可配置内容**
`name`（与 json 文件名、代码里的名字三处一致）、`width` / `height`、
`normal` / `hover` / `onclicked` 三态外观、`text`、`selfAnchorX/Y`、
`traceAnchorX/Y`、`shape`。字段与 `CXXCSS/Button/button.example.json` 一一对应，
运行期形态见 `ButtonData`。
>**三态外观与"省略即继承"**
三态结构相同：`{ type, source }`，`type` 是 `color` / `img` / `svg`。
省略 `hover` / `onclicked` 时**继承 `normal`**——这条收在 `ButtonData::Normalize()`
一处，不会在渲染、命中、自检各判一遍。
`img` / `svg` 两种类型**还没落地**（要纹理层），但类型与字段先留着：
配置里能写的东西，数据结构就得能表达，否则生成器落地时要改接口。
>**InkingStaticButton(parent, ButtonData);**
手写数据构造：不走配置，直接给一份数据。
>**InkingStaticButton(parent, "name");**
配置驱动构造：只给 parent + 名字，配置从 `ButtonLibrary` 里按名字取
（生成器将来生成的代码就是"往表里登记 + 用这个名字构造"）。
名字没登记过会打一条警告并退回一份默认数据——按名字构造却什么都没登记，
十有八九是名字拼错了，静默给个空按钮比报错更难查。
**数据在构造时就拷进来了**：构造之后再登记不会生效。
>**[final] MouseHover(bool) / MousePress(bool) / MouseRelease();**
三态状态机，**幂等**：只换颜色，不标脏、也不通知场景。
优先级是**按下 > 悬停 > 通常**，`MousePress(false)` 按当前指针位置回到
Hover 还是 Normal 由按钮自己判——框架层不需要记住"抬起时该回哪一态"。
返回值表示三态是否真的变了。
`MouseHover` 的标志**一直更新**（即使正在按下）：拖拽时按着滑出去再松开，
本来就该回到 Normal。
>**TriggerClick();**
触发一次点击回调；没有回调时是空操作、返回 false。
框架应在"抬起且指针仍在按钮内"时调它；也可以直接调来模拟一次点击。
>**[√] onRender(pixelX, pixelY);**
按当前状态取外观、按形状填色，有文字再画文字。
重写的是**钩子**，不是写入口。
>**GetState / GetShape / GetAppearance / GetData / GetText;**
读当前三态 / 形状定义（渲染、命中、包围盒都从它派生）/ 外观（可传状态取指定那一份）
/ 构造时那份配置 / 按钮上的文字。
>**SetText(string);**
设置文字。和"不建议在按钮里配文字"那条不冲突：**多语言就该走这里**——
配置里的文字最终会被烘成纹理，换语言就废了。文字不参与命中，所以**不标脏**。
>**SetOnClicked(function<void()>);**
挂点击回调。原来骨架里的 `setOnClicked` 按 CodeStyleRule §5 改成 PascalCase；
返回值也从 `std::any` 收窄成 `void`——"按下要算出一个值"不是按钮的职责。
>**文字渲染尚未落地**
`text` 的字段（`fontSize` / `leftSpace` / `topSpace` / `path` / `content`）
都已经在数据结构里，但现在画的是一块**同尺寸的占位方块**（还没有字形图集 /
纹理层）。这样"文字区域在哪儿"今天就能被像素钉住；等图集接上，
只换 `renderText` 一个函数体。
>**鼠标事件还没接过来**
`MouseHover` / `MousePress` / `MouseRelease` 现在**没人喂**——把鼠标接到场景上是
「命中与三态」那一步（`docs/InputDesign.md`）的活。接口已经按那时候的形状定好了，
接线时调的就是这三个。

## ButtonData

按钮的**纯数据**定义：对应 `CXXCSS/Button/<名字>.json` 的全部字段，零成员函数
（只有 `Normalize()` 一个收尾）。数据与逻辑分开，生成器将来生成的就是"填这个
结构的代码"。使用 `#include <button/ButtonData.h>` 将其引入到你的代码中。
>**字段**
`name` / `width` / `height` / `normal` / `hover` / `onclicked`（`ButtonAppearance`）/
`text`（`ButtonTextData`）/ `label` / `textColor` / `selfAnchor` / `traceAnchor` /
`offsetX` / `offsetY` / `zIndex` / `visible` / `shape`（`ShapeSpec`）。
`textColor` 是**配置里没有的新增字段**（json 的 `text` 段没定义颜色），
默认浅色，为了文字真的看得见；text 段定稿后它要么被配置覆盖、要么删掉。
>**Normalize();**
把"省略即继承"补齐（`hover` / `onclicked` 继承 `normal`），幂等。
**必须在入库前调一次**——`ButtonLibrary::Register` 内部已经调了，
所以 `Find` 拿到的数据永远是补齐过的。
>**为什么要 `hoverInheritsNormal` 这两个标志**
因为"字段是不是空的"判不出"用户是不是故意写了和 normal 一样的外观"。
示例里三态是分别写的（0.75 / 0.85 / 0.75），靠标志才能不被覆盖。
>**ButtonAppearance / AppearanceType;**
一种状态下的外观：`type`（`Color` / `Image` / `Svg`）+ `color`（0xAARRGGBB）
+ `source`（原样的配置串，留给纹理层）。渲染直接用 `color`，不在每帧解析字符串。

## ButtonLibrary

**名字 → ButtonData** 的登记表：`InkingStaticButton(parent, "name")` 这条路要有
东西可查，生成器生成的登记代码就往这里塞。
使用 `#include <button/ButtonLibrary.h>` 将其引入到你的代码中。
>**与 SceneLibrary 的刻意不同**
场景是**对象**登记（同名只能有一个活的场景、有生命周期要管），
这里登记的是**配置数据**：没有 RAII 令牌，也不需要 Close，表建起来就是只读的。
>**与 SceneLibrary 的刻意相同**
存储用**函数内静态量**（不用类的静态数据成员——那是静态初始化 / 析构顺序问题），
同名**驳回**、不覆盖。名字是查找键，让后来者顶掉先来者等于
"按钮长什么样取决于初始化顺序"。
>**Register(ButtonData) → RegisterResult;**
`Ok` / `NameTaken`（同名已登记，驳回）/ `Invalid`（名字为空）。
登记时顺手做一次 `Normalize()`。
>**BeginRegistrationBatch() / EndRegistrationBatch();**
生成器生成的 `RegisterAllButtons()` 用的一对入口，让"重新登记"成为正路：
开始时把**上一批**登记过的名字撤下，结束时把本批标记成"上一批"。
于是同一份生成代码重复运行（自检、运行期重建）不会撞 `NameTaken`，
而且**只撤自己那一批**——自检或业务代码自己塞的条目不碰。
（`ClearForTest` 是"清空一切"，拿它做重新登记会把别人的条目一起清掉，
那是比重复登记更难查的错。）
>**Find(name) → const ButtonData\*;**
查不到返回 nullptr。返回的指针由库持有、**内容只读**且地址稳定
（存储用 `std::map`，插入不会让已有元素的地址失效），可以长期保存。
>**IsNameTaken / GetAllNames / Count;**
查询与枚举。
>**ClearForTest();**
清空登记表，**只给自检用**。库本身是只增不减的——它存在的意义就是
"启动期建好、之后只读"，但自检需要从空表开始。

## Timeline

整个 UI 的**唯一时间源**，做**双速驱动**：逻辑固定步长、动画按需采样。
使用#include <ink/basic/Timeline.h>将其引入到你的代码中。
>**为什么需要它**
逻辑要固定步长（50Hz），动画要按自己的频率（24fps 的动画就该按 1/24s 走），
而显示器是第三个频率（60/120/144Hz）。三者互不整除，所以**时间不能按"多少次 tick"来数，
只能按真实流逝时间累加**——错拍只影响"这一帧算几次"，不影响时间本身。
>**三条通道**
1. **逻辑步**（`SetLogicCallback`，固定 interval，默认 1/50s）：累加器追赶，
   一帧可能跑 0 次、1 次或多次。回调收到的是**固定的 interval**，不是真实 delta——
   物理和状态机要的就是确定的步长。
2. **每帧**（`SetFrameCallback`）：每个渲染帧恰好一次，回调收到**真实 delta**（已截断）。
   插值、和画面同步的东西放这里。
3. **自定义频率订阅**（`Subscribe(interval, callback)`）：自己声明"我多久要一次"
   （例如 1/24s），同样累加器追赶，回调收到的是**实际步长**（= 声明的间隔），
   所以慢帧也不会让动画走偏。
>**Timeline::Advance(double deltaSeconds);**
主循环每帧调一次，传**真实流逝时间**。内部依次派发：逻辑步 → 每帧回调 → 到期的订阅。
>**为什么驱动源是主循环，不是任务队列**
- 任务队列的到点是 `condition_variable::wait_for` 撑的，Windows 上定时器粒度 1~15.6ms；
  50Hz 逻辑步是 20ms，抖动占比太大。更关键的是**回调线程**：任务队列在管家线程上执行，
  而 UI 状态（坐标、标脏、命中）只能在 UI 线程动，否则就是数据竞争。
  时间可以在别处算，**交付必须在 UI 线程**——主循环天然满足。
  任务队列仍然可以留作无窗口时的外部时钟源或跨线程任务，但不做节拍器。
>**追不上就截断（入口截断）**
切后台 / 睡眠回来时 delta 可能是几秒，不截断就会一次补几百步逻辑，卡住一帧。
超过 `maxCatchUpSeconds`（默认 0.25s）的部分在**入口**直接丢掉：
帧数恢复常速，模拟里的时间少走一段。宁可这样，也不能卡死。
>**Subscribe / Unsubscribe / SubscriberCount();**
订阅返回 `Handle`（0 表示无效）。间隔 <= 0 或回调为空会被拒绝。
退订在派发期间是安全的（内部用墓碑标记，不移动元素），
回调里退订自己或别人都不会让遍历错位。
>**自检用的观测口**
`GetLogicStepCount` / `GetLastLogicSteps` / `GetLastDelta` /
`GetLogicAccumulator` / `GetCatchUpDropCount`。

