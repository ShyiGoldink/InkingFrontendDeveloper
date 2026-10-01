# InkingFrontendDeveloper API 文档

**关键词**
**| 可配置内容**
是指 设计常量（编译期定型，无 setter）
你不需要关心它在代码的哪里，只需要在文件夹CXXCSS按照需求写好配置文件即可
我们的代码生成器会在编译时在代码中准备好这些文件
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
你可以在CXXCSS/Scene目录下创建场景名.json来为您的Scene做基础设置。样板可参考我们提供的Scene.example.json。
使用此构造函数后，场景的一些方法将被阻止使用以保证正确的输入响应：运行期的位置与尺寸写入口一律没有。
这是配置驱动的场景，接受代码生成器分析；生成器可以直接生成新类，在编译期就把行为和属性改掉。
>**InkingScene();**
无参构造函数，运行时构造：不绑定场景名，也不登记进场景库（登记进去也查不到，只会留下空 key）。
场景的尺寸先按设计画布定型，之后再接 SceneData 那条自定义路径。
>**Scene::GetSceneName();**
取场景名。构造时定下，没有写入口。
>**[final] Scene::SetVisible(bool);**
场景层唯一的写入口。false 表示这个场景不渲染（数据还在），true 表示渲染。
几何不参与：场景没有 setPosition / setSize。
>**Scene::IsRegistered();**
这个场景当前是否登记在场景库里。空名字的场景会返回 false——它有令牌，但令牌是空转的。
>**为什么登记只允许在 UI 线程**
场景的构造与析构都发生在窗口主循环那一条线程上，所以 SceneLibrary 有意不加锁
（后端 ShineStatusChecker 加锁是因为模块会被线程池里的多个管家线程读写）。
真出现跨线程场景，再加锁。
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
>**你通常不需要直接调用它**
登记与注销由 InkingScene 内部的 RAII 令牌完成，这一节主要是给窗口层、生成器和自检用的。
>**SceneLibrary::RegisterScene(string sceneName, InkingScene\* scene);**
登记一个场景指针。指针为空会被忽略；同一个指针重复登记只算一次。
同一个名字下可以存多个场景实例（同期只有一个可渲染，见上文「InkingScene」）。
>**SceneLibrary::UnregisterScene(string sceneName, InkingScene\* scene);**
注销一个场景指针。该名字下没有实例之后，名字本身也会被移除，
免得 GetAllSceneNames / SceneCount 把空壳算进去。
>**SceneLibrary::GetScenes(string sceneName);**
按名字取该名字下的全部场景实例；没有登记过就返回空 vector，不抛错。
>**SceneLibrary::GetAllSceneNames();**
取当前登记过的所有场景名，主要用于自检与枚举。
>**SceneLibrary::SceneCount();**
当前登记的场景实例总数，主要给自检用。
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
ZIndex; #int类型 越大越靠上，同场景内全局比较
>**InkingAnchor(InkingAnchor* parent, AnchorData data);**
静态初始化：一次把尺寸、两个锚点、偏移、层级给全。根（窗口 / 场景）传nullptr当parent。
>**InkingAnchor(InkingAnchor* parent, string name);**
配置驱动：只给parent和名字，具体怎么读CXXCSS由子类自己实现。
>**[final] 写入口：结构性变化（两版共用）**
ChangeZIndex / SetParent。标脏与钩子都在内部完成，所以不存在「哪次改动忘了标脏」；
返回bool表示这次是否真的改了，没变就不标脏、不触发钩子。
>**[final] 写入口：几何变化（只有动态版有）**
Resize / ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset。
静态组件里没有这几个名字，**基类里也没有**——所以连命中表里存的基类指针都改不了几何。
>**[√] 钩子**
onSizeChanged / onSelfAnchorChanged / onTraceAnchorChanged / onOffsetChanged / onZIndexChanged /
onParentChanged / onDirty / onTick（只有动态组件收得到）。钩子只挂附加逻辑，不标脏、不拦截写入口。
>**每帧的Tick**
InkingDynamicAnchor::Tick(float deltaSeconds); 由场景调，每帧一次；静态组件压根没有这个入口。
>**[x] 展示倍率**
Magnification / DeviceWidth / DeviceHeight。设计单位 → 设备像素的倍率（letterbox整幅等比缩放 × 系统DPI），
由窗口层写入，只影响绘制换算，不标脏。它不参与布局：所有组件一视同仁，没有「某个组件不跟着缩放」这回事。
>**关于尺寸的边界**
尺寸是设计坐标里的固定值，不随父级等倍扩展（「填充父级」这类是layout通道的事，尚未实现）。
所以静态组件的几何真的可以一次定型。
