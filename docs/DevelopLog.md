# 开发日志

**step 1**

>首先，手写将来会由“代码生成器”生成的部分
>这样之后方便定义diamagnetic生成器将会如何调用
>例如：UI描述(将来: json)  →  inkgen(生成器)  →  inking_design.h(你现在手写)

**step 2**

>把后端框架（InkingBackendFramework）的日志、消息队列、任务队列移植过来，
>并让 ISDEBUG / ISLOG 成为**编译期**开关。
>
>结构对应关系：
>   ShineLog          →  include/ink/basic/InkLog.h（HTML 日志，按天+会话分组）
>   UIMessageLibrary  →  include/ink/ui/MessageQueue.h（静态消息队列）
>   TaskQueueLoop     →  include/ink/thread/TaskQueue.h（单例任务队列）
>   ThreadPool        →  include/ink/thread/ThreadPool.h（管家线程池）
>   TaskStruct.h      →  include/ink/dataStruct/TaskStruct.h（dependOn / then）
>   MessageStruct.h   →  include/ink/dataStruct/MessageStruct.h
>
>与后端的差异（有意为之）：
>   1. 没有移植 ShineBasicModule / ShineStatusChecker 那套自检体系；
>   2. 线程池的线程数从固定常量改成 init() 的参数，默认 4；
>   3. 日志多了一档 warn，消息类型多了一档 Warn。
>
>开关的落地方式：CMake 选项 ISDEBUG / ISLOG → 宏 INK_ISDEBUG / INK_ISLOG，
>以 PUBLIC 方式挂在 ink_core 上（库和调用方必须看到同一套宏，否则 ODR 违规）。
>ISLOG 关闭时 InkLog.cpp 不参与编译，头文件里的 InkLog 退化成空实现。
