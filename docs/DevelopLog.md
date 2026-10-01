# 开发日志

## Step 1

> 首先，手写将来会由“代码生成器”生成的部分
> 这样之后方便定义diamagnetic生成器将会如何调用
> 例如：UI描述(将来: json)  →  inkgen(生成器)  →  inking_design.h(你现在手写)

## Step 2

> 把后端框架（InkingBackendFramework）的日志、消息队列、任务队列移植过来，
> 并让 ISDEBUG / ISLOG 成为**编译期**开关。
>
> 结构对应关系：
>
> - ShineLog → include/ink/basic/InkLog.h（HTML 日志，按天+会话分组）
> - UIMessageLibrary → include/ink/ui/MessageQueue.h（静态消息队列）
> - TaskQueueLoop → include/ink/thread/TaskQueue.h（单例任务队列）
> - ThreadPool → include/ink/thread/ThreadPool.h（管家线程池）
> - TaskStruct.h → include/ink/dataStruct/TaskStruct.h（dependOn / then）
> - MessageStruct.h → include/ink/dataStruct/MessageStruct.h

## Step 3

> 做好鼠标输入事件，并绑入InkingWindow中
> 仿照ShineBasic写InkingAnchor，奠定整个项目的定位结构

## Step 4

> 场景层落地第一步：SceneLibrary + RAII 登记令牌。
> 参考后端框架的 ShineStatusChecker / StatusRegisterToken，把场景按名字登记进库，
> 让窗口层和将来的生成器能「拿名字换 InkingScene*」，不必认识具体场景类型。
>
> 结构对应关系：
>
> - ShineStatusChecker → include/scene/SceneLibrary.h（静态注册表）
> - ShineBasicModule → include/scene/InkingScene.h（被登记的基类）
> - StatusRegisterToken → include/scene/SceneRegisterToken.h（RAII 令牌）
>
> 与后端刻意不同的三点，都是实测踩过或推演出来的：
>
> 1. **存储改成函数内静态量**（`scenes()`），不用类的静态数据成员。
>    后者的初始化发生在 main 之前、顺序由翻译单元决定，静态场景对象可能先构造；
>    更糟的是场景可能在库已经析构之后才注销。函数内静态量首次使用才初始化，
>    库一定活得比登记者久。
> 2. **不加锁**，只允许 UI 线程登记与注销。后端加锁是因为模块被多个管家线程读写；
>    场景层只在窗口主循环那一条线程构造 / 析构，加锁挡不住误用还白付一次原子操作。
> 3. **令牌模板化**（`RegisterToken<Registered, Library>`）。后端只有一种被登记类型所以写死了
>    指针；这里将来还有控件、面板，模板化省得抄重复代码。场景侧别名 `InkingScene::SceneToken`。
>
> 顺手修掉的既有问题：`include/scene/InkingScene.h` 原来 include 了一个不存在的
> `ink/scene/SceneLibrary.h`（旧路径写法和本仓库不一致），且引用了未声明的
> `_statusRegisterToken` / `moduleName()`，是编译不过的死文件；现在按本仓库的
> include 约定（`<scene/...>`）重写，并把 `src/scene/*.cpp` 接进 `ink_core`。
>
> 自检新增第 8 段：登记 / 注销 / 同名多实例 / 空名字 / 令牌移动构造，
> 并在收尾处断言场景库回到用之前的状态。**曾经被这段测试抓到一个真 bug**：
> `IsRegistered()` 只看令牌是否存在，于是空名字的令牌（空转的）也让场景自称已登记；
> 已改成同时问令牌自己。
