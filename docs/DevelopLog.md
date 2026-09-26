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
