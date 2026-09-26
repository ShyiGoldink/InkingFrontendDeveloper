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

## Step 4（分支：test002）

> 按 docs 做**最小的完整测试**：一个窗口，顶部一条 3 个按钮的菜单栏，
> 点下去只写日志与调试输出。验三件事：
>
> 1. **静态层级方案**：`InkingScene` 把静态子树压平成
>    「命中索引 + 平铺绘制表」，几何 / 可见性 / 层级一变才重烘；
>    声明成「等宽等距」的容器（菜单栏）用纯算术查槽位，连表都不建
>    （InputDesign §10.4）。
> 2. **重绘方案（文档里没有，这一版新增）**：`core/RedrawScheduler.h`
>    用和输入 isDirty **同样的形状、但独立实现**——标脏 → 重画 →
>    帧计数尾巴 → 静止时整帧不画也不 present。
>    约定一句话：**需要重绘的属性，写入口自己明确调 `makeDirty()`**，
>    框架不做自动分析（颜色、文字、悬停、按下都是这么标的）。
> 3. **动态层级与 zindex**：会动的子树不进表，父表只记一个「洞」，
>    静态查表 / 动态自判，两边按 `(zindex, 注册序号)` 合并。
>
> 落地清单（对应 InputDesign §14 的待办）：
>
> - 已有：三态（Block / PassThrough / Miss）+ 目标；isDirty + 帧计数；
>   `[final]` 写入口 + `onXxx` 钩子；显式 zindex；场景静态化；
>   标脏到最近的表边界；组件反向注册到场景；等宽等距专用查找。
> - 还没做：聚类索引、溢出桶、旋转容器 AABB、变换通道与动画采样表、
>   「点不落在洞里就不问动态层」的预筛、`SceneData` 构造路线。
>
> 自检：`tests/test_main.cpp` 第 9~13 节；示例：`examples/menu_bar`。
