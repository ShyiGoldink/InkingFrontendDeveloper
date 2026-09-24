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
- [SETUP.md](SETUP.md)：SDL3 安装与配置（Windows/Linux/macOS、源码、离线桩）
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
2. 先跑离线桩全绿（不依赖网络）：
   `cmake --preset offline-stub && cmake --build --preset offline-stub`
3. 本机有真实 SDL3 时，再跑 `msys2-debug` 真机验证；没有真实 SDL3
   时，可用 `-DINK_SDL3_LOCAL_DIR=...` 指向本地解压的 SDL3；
4. 更新本文档/README/SETUP 中与实现不一致的内容；
5. 不要在源码树里提交 `build/` 产物。

## 5. 当前阶段

- 已有：顶层 CMake（可 add_subdirectory）、SDL3 四层引入策略
  （显式桩 / 显式本地目录 / 系统包 / FetchContent）、
  最小开窗示例（1280×720 letterbox）、离线开发桩；
- 已有：从后端框架移植的日志（HTML）、消息队列、任务队列、线程池，
  以及 ISDEBUG / ISLOG 两个编译期开关；
- 已有：InkingWindow 单例（窗口尺寸是运行期属性，设计尺寸是编译期常量）；
- 未实现：SDF 形状层、样式/渲染层、描述文件 + 代码生成器；
  另外 InkingWindow 目前只负责开窗，没有事件泵/绘制入口（见已知坑 6）。

## 6. 已知坑（真实踩过的）

1. **SDL3 的版本 API 和 SDL2 不同**：SDL3 是 `int SDL_GetVersion(void)`，
   返回编码后的整数，用 `SDL_VERSIONNUM_MAJOR/MINOR/MICRO` 解包；
   没有 SDL2 的 `SDL_version` 结构体和出参形式。别写回 SDL2 风格。
2. **桩和真库必须同步**：`third_party/sdl3_stub` 是手写的 API 子集，
   一旦新代码用到真 SDL3 的 API，就要同时补桩。否则会出现“桩构建绿、
   真实构建红”（或反过来），而两边都绿才是真的绿。
3. **不要混用 C 运行时**：本项目工具链是 MSYS2 UCRT64，优先装
   `mingw-w64-ucrt-x86_64-sdl3`。官方 `-mingw` 预编译包是 MSVCRT
   运行时，能跑但不推荐。
4. **pacman 包名是小写** `sdl3`，写成 `SDL3` 会报 target not found。
5. **CMake 缓存会让旧选择“粘住”**：换 SDL3 来源后建议
   `cmake --preset <name> --fresh`，否则可能仍在用上次解析的结果。
6. **SDL3 大量函数返回 bool，语义和 SDL2 相反**：例如 `SDL_Init()` 是
   “true 表示成功”，写成 SDL2 的 `if (SDL_Init(...) != 0)` 会把成功
   当失败。写新代码时先看一眼真头文件的返回类型。
7. **开关必须 PUBLIC 传播**：ISDEBUG / ISLOG 决定了头文件里 inline 代码
   是“真实现”还是“空实现”。库和调用方看到不同宏就是 ODR 违规，
   编译器不报错但行为错乱。所以它们挂在 `ink_core` 的 PUBLIC 定义上，
   不要改用目录级的 `add_compile_definitions`（那还会污染第三方目标）。
