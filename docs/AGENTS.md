# InkingFrontendDeveloper — Agent 开发指南

写给在这个仓库里工作的 Agent 的入口文档。先读这个，再动手。
遇到与本文不一致的地方，以实测为准，并回来更新本文。

## 1. 项目一句话

基于 SDL3 的 C++20 UI 框架。核心思路：把能在编译期完成的决定
（控件类型、事件分发、布局展开）全部提前到**代码生成阶段**完成，
运行时只做每帧必须做的事。形状层用 SDF，渲染层用 DrawCall 合批 +
静态烘焙，不做 CPU 逐像素渲染。

## 2. 快速导航

- [README.md](../README.md)：项目介绍、快速开始、环境要求、FAQ（人/AI 通用）
- [SETUP.md](SETUP.md)：SDL3 安装与配置（Windows/Linux/macOS、源码、本地目录）
- [API.md](API.md)：对外 API 设计稿（关键词、可配置内容、各组件）
- [InputDesign.md](InputDesign.md)：输入与命中方案设计稿（静态查表 / 动态自判 / 三态 query）
- [DevelopLog.md](DevelopLog.md)：开发日志（每一步做了什么、为什么）
- 本文档：Agent 专属的约定与自检清单

## 3. Agent 必须遵守的技术约定

1. **C++20 起**：`ink_core` 已设置 `cxx_std_20`。
2. **形状层 = SDF**：形状用 `f(x, y) -> 带符号距离` 描述，裁剪 = `f >= 0`。
3. **渲染 = DrawCall 合批 + 静态烘焙**，禁止 CPU 逐像素渲染。
4. **布局 = 百分比 + 对齐点**：提供 `Layout::Middle`（50%）以及
   “自己的 Middle 对齐父级 Middle”这类锚点对齐。
5. **事件分发 = 自上而下直线命中**，由代码生成器展开。
6. **代码生成器**：解析类 JSON 的描述文件，把控件展开成具体结构体 +
   直线 if 分发。生成代码进 `build/` 产物目录，不放进源码树。

## 4. Agent 改动后的自检清单

每次改动后至少做：

1. 语法/结构检查：新增文件、CMake、JSON 是否合法；
2. 完整构建必须绿：
   `cmake --preset msys2-debug && cmake --build --preset msys2-debug`
   没装系统 SDL3 时，用仓库里预放的本地目录：
   `cmake -B build/msys2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DINK_SDL3_LOCAL_DIR=third_party/sdl3`
3. 跑自检 `build/msys2-debug/bin/ink_test.exe`，退出码 0 才算过；
   要连窗口主循环一起验，加环境变量 `INK_AUTOQUIT=1`（约 2 秒后自动退出）；
4. 更新本文档/README/SETUP 中与实现不一致的内容；
5. 不要在源码树里提交 `build/` 产物。

## 5. 当前阶段

- 已有：顶层 CMake（可 add_subdirectory）、SDL3 三层引入策略
  （显式本地目录 / 系统包 / FetchContent 源码）、最小开窗示例（letterbox）；
- 已有：从后端框架移植的日志（HTML）、消息队列、任务队列、线程池，
  以及 ISDEBUG / ISLOG / ISMESSAGE 三个编译期开关；
- 已有：InkingWindow 单例（窗口尺寸是运行期属性，设计尺寸是编译期常量）；
- 已有：事件泵与鼠标输入接入——`InkingWindow::Show()` 里就是主循环
  （事件泵 → 鼠标状态 → 窗口坐标换算设计坐标 → 占位绘制 → 帧末清理），
  鼠标状态在 `include/input/MouseInput.h`；
- 未实现：Scene 层（`include/scene`、`src/scene` 还是空文件）、
  SDF 形状层、样式/渲染层、描述文件 + 代码生成器；
  绘制目前只有占位（`src/window/InkingWindow.cpp` 的 `drawPlaceholder`：
  清屏 + 一个跟着鼠标的小方块，用来肉眼验证坐标换算），Canvas/场景绘制接手后删掉。

## 6. 已知坑（真实踩过的）

1. **SDL3 的版本 API 和 SDL2 不同**：SDL3 是 `int SDL_GetVersion(void)`，
   返回编码后的整数，用 `SDL_VERSIONNUM_MAJOR/MINOR/MICRO` 解包；
   没有 SDL2 的 `SDL_version` 结构体和出参形式。别写回 SDL2 风格。
2. **不要混用 C 运行时**：本项目工具链是 MSYS2 UCRT64，优先装
   `mingw-w64-ucrt-x86_64-sdl3`。官方 `-mingw` 预编译包是 MSVCRT
   运行时，能跑但不推荐。
3. **pacman 包名是小写** `sdl3`，写成 `SDL3` 会报 target not found。
4. **CMake 缓存会让旧选择“粘住”**：换 SDL3 来源后建议
   `cmake --preset <name> --fresh`，否则可能仍在用上次解析的结果。
5. **SDL3 大量函数返回 bool，语义和 SDL2 相反**：例如 `SDL_Init()` 是
   “true 表示成功”，写成 SDL2 的 `if (SDL_Init(...) != 0)` 会把成功
   当失败。写新代码时先看一眼真头文件的返回类型。
6. **开关必须 PUBLIC 传播**：ISDEBUG / ISLOG 决定了头文件里 inline 代码
   是“真实现”还是“空实现”。库和调用方看到不同宏就是 ODR 违规，
   编译器不报错但行为错乱。所以它们挂在 `ink_core` 的 PUBLIC 定义上，
   不要改用目录级的 `add_compile_definitions`（那还会污染第三方目标）。
7. **日志和消息必须用宏调用**：`INK_LOG_*` / `INK_MESSAGE_*` 关闭时展开成
   `((void)0)`，连参数都不求值；直接调用类方法（`InkLog::info`、
   `MessageQueue::addMessage`）时参数照样会求值，字符串拼接和 to_string
   的开销一点都没省。新增子系统时照这个模式给宏，别只给类。
8. **公开头里只做前向声明，别包含 `<SDL3/SDL.h>`**：头文件轻一点，
   谁包含它都不用跟着拖 SDL 进来。注意 SDL3 的 `SDL_Event` 是 **union**，
   所以前向声明必须写 `union SDL_Event;`（写 `struct` 会和真头文件冲突）。
   换算是渲染器的事，坐标转换在窗口层做，公开头只需要类型名。
9. **有没有控制台是链接期决定的**：`ISDEBUG` 关闭时，`ink_core` 通过
   INTERFACE 链接选项把可执行文件链成 GUI 子系统（MinGW 用 `-mwindows`，
   MSVC 用 `/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup`），双击不再弹黑框。
   MinGW 下源码照旧写 `int main()`，mingw-w64 运行时会把它叫起来，
   别改成 `WinMain`。副作用：双击运行时 printf / SDL_Log 一律看不到
   （从终端启动能看到，标准句柄是继承来的），要看输出就用 Debug 构建。
