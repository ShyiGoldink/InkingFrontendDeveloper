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
- [../cmake/CMakeUserPaths.cmake.example](../cmake/CMakeUserPaths.cmake.example)：
  本机依赖路径覆盖模板（复制成同目录的 `CMakeUserPaths.cmake` 即生效）
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
7. **依赖来源必须显式可控**：SDL3 由 `INK_SDL3_SOURCE`（auto / system /
   fetch / local）决定，别把某个来源写死进流程。本机路径写
   `cmake/CMakeUserPaths.cmake`（gitignore），不要往仓库里塞绝对路径。

## 4. Agent 改动后的自检清单

每次改动后至少做：

1. 语法/结构检查：新增文件、CMake、JSON 是否合法；
2. 完整构建必须绿：
   `cmake --preset msys2-debug && cmake --build --preset msys2-debug`
   没装系统 SDL3 时，用仓库里预放的本地目录（`local` = 不联网也不查系统）：
   `cmake --preset msys2-debug -DINK_SDL3_SOURCE=local -DINK_SDL3_LOCAL_DIR=third_party/sdl3`
   这台机器没装 Ninja 时改用不带预设、不带 `-G` 的两行：
   `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel`
3. 跑自检 `build/msys2-debug/bin/ink_test.exe`，退出码 0 才算过；
   要连窗口主循环一起验，加环境变量 `INK_AUTOQUIT=1`（约 2 秒后自动退出）；
   想连"窗口 + 菜单栏 + 点击"一起看，跑示例：
   `INK_AUTOQUIT=1 build/msys2-debug/bin/menu_bar.exe`（点按钮只写日志；
   退出时会把「静态表重烘几次 / hover query 几次 / 重绘几帧 / 跳过几帧」
   打进 Log.html）；
   想验「悬停 → 几何变化 → 重烘」这条路，跑
   `INK_AUTOQUIT=1 build/msys2-debug/bin/hover_grow.exe`（纯白场景 +
   半透明黑按钮，悬停加重并放大 1.15 倍）；
   没有显示器、又想确认"到底画成什么样"，加 `INK_DUMPFRAME=<路径.bmp>`：
   第一帧真实画出来的画面会存成 BMP（转储在 present 之前，否则后台缓冲
   内容不保证还在）。想连悬停态一起抓，得让指针真的落在按钮上——主循环只
   转第一帧就转储，所以最容易的办法是先跑起来把鼠标放上去，或者照
   `docs/DevelopLog.md` Step 5 里那个临时抓图程序（推一个
   `SDL_EVENT_MOUSE_MOTION` 进队列）自己抓一张；
4. 更新本文档/README/SETUP 中与实现不一致的内容；
5. 不要在源码树里提交 `build/` 产物。

## 5. 当前阶段

- 已有：顶层 CMake（可 add_subdirectory）、SDL3 来源策略
  `INK_SDL3_SOURCE`（auto / system / fetch / local）+ 本机路径覆盖文件
  `cmake/CMakeUserPaths.cmake`、最小开窗示例（letterbox）；
- 已有：从后端框架移植的日志（HTML）、消息队列、任务队列、线程池，
  以及 ISDEBUG / ISLOG / ISMESSAGE 三个编译期开关；
- 已有：InkingWindow 单例（窗口尺寸是运行期属性，设计尺寸是编译期常量）；
- 已有：事件泵与鼠标输入接入——`InkingWindow::Show()` 里就是主循环
  （事件泵 → 鼠标状态 → 窗口坐标换算设计坐标 → 输入 query → 每帧更新 →
  按需重绘 → 帧末清理），鼠标状态在 `include/input/MouseInput.h`；
- 已有（**test002 原型**，`include/scene` + `src/scene` + `include/core`）：
  - `InkingScene`（继承 InkingAnchor，锚点定位、构造即注册、可见性写入口、
    三态 `Query`、静态烘焙与平铺绘制表、动态洞）；
  - 静态层：网格表 + CSR 候选兜底，等宽等距容器走纯算术专用查找；
  - 输入：`InputRouter` 的 isDirty + 帧计数 + 三态 query，点击单独查一次；
  - 重绘：`RedrawScheduler`（文档外的独立一路），约定「需要重绘的属性由写入口
    明确调 `makeDirty()`」；
  - `Button`：三档配色 + 点击回调 + **悬停按倍率放大**（走尺寸写入口，
    所以会连带把上层命中表标脏 → 重烘；这条正是 `examples/hover_grow` 要验的）；
  - SDL 后端画布 `src/window/SdlCanvas.h`；示例 `examples/menu_bar`
    （顶部 3 按钮菜单栏）、`examples/hover_grow`（纯白场景 + 半透明黑按钮，
    悬停加重并变大）。
- 未实现：SDF 形状层、样式层、描述文件 + 代码生成器、聚类索引 / 溢出桶 /
  变换通道与动画采样表；场景层的运行期位置与尺寸写入口还没按文档去掉
  （现在是手写原型，生成器接手后是编译期定死）。

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
10. **预设写死 Ninja，而这个错项目自己拦不住**：CMake 在读到任何
    `CMakeLists.txt` 之前就要解析生成器，没装 Ninja 时配置直接失败，报的是
    `CMake Error: CMake was unable to find a build program corresponding to
    "Ninja"`。所以两条路都得留着：装了 Ninja 用预设；没装就别用预设、
    也别写 `-G Ninja`（写不写都一样失败），用 `cmake -S . -B build`——
    `CMakeLists.txt` 本身与生成器无关，走系统默认生成器即可。
11. **第二次配置不会再拉源码**：FetchContent 的源码留在
    `build/<预设>/_deps/`，重配置只要 1 秒左右且不联网（已实测）。
    换 SDL3 版本或换来源，改 `INK_SDL3_SOURCE` / `INK_SDL3_GIT_TAG` 后
    重新配置就行；只有 `--fresh` 或删构建目录才会真的重新联网。
12. **等宽等距专用查找要分两份顺序存**：算术定位（`index = (x - 行首) / (槽宽 + 槽间距)`）
    必须让槽位按 **x 升序**存，查询又必须按 **z 降序**扫。一开始只用一份
    「z 降序」的数组，结果行首取到的是最右边那一槽，三个按钮全查不中。
    现在 `SlotIndex` 存两份：`_slots`（行序）+ `_queryOrder`（查询序）。
13. **专用查找只接手"规整的行"**：槽位不等宽 / 不等高 / 不等间距，或者槽位自己
    还带子节点，就别用它，退回网格表（`SlotIndex::Build` 返回 false，
    场景打一条警告日志）。生成器接手后这类判断应该在编译期做掉。
14. **动态子树移动不能标父表的脏**：洞不参与父表重烘，否则一个每帧都在动的
    组件会让整张静态表每帧重烘一次。标脏的循环遇到动态结点就停
    （它不进表），只把「命中可能过期」这个 bool 标到根上给输入层。
15. **重绘和命中是两套独立脏标记**：外观（颜色 / 文字 / 悬停 / 按下）只标重绘，
    不该标命中；反过来，输入 query 本身也不该触发重绘。混在一起就会出现
    「鼠标划过一下，静态表就重烘一次」。约定见 `include/core/RedrawScheduler.h`。
16. **`makeDirty()` 不做自动分析**：需要重绘的属性由写入口**明确**调它
    （`Button::SetFill` / 悬停 / 按下都是这么标的），框架不替写入口猜。
    几何 / 可见性 / 层级则由 `[final]` 写入口内部顺带标一次，漏不了。
17. **新实现文件记得进 `INK_CORE_SOURCES`**：窗口层的 `SdlCanvas.h` 放在
    `src/window/` 而不是 `include/`，同目录用 `#include "SdlCanvas.h"`；
    写成 `<window/SdlCanvas.h>` 会报找不到头文件（`include/` 才是搜索根）。
18. **半透明要自己开混合**：SDL 的绘制默认是不混合的，`Color.a` 会被直接丢掉
    （半透明黑看着就是纯黑）。窗口层建完渲染器就 `SDL_SetRenderDrawBlendMode(
    renderer, SDL_BLENDMODE_BLEND)`，色值本身由场景层原样给出。取色验证：
    黑 α=120 压在白底上是 `#878787`，α=200 是 `#373737`。
19. **会变宽的按钮组别留在「等宽等距」里**：`SlotIndex` 建索引时要求槽位
    等宽、等高、等间距，几何一变（比如悬停放大）就不满足了，只能退回网格表。
    所以「悬停会变大的按钮组」这种容器一开始就别声明 `UseSlotLayout()`——
    否则每悬停一次就退回一次兜底结构，白折腾。等宽等距那一档是给
    "两次配置变更之间几何不变"的静态行用的（docs/InputDesign.md §9）。
20. **悬停放大 = 真几何变化 = 真重烘**：这是有意为之的对照实验。
    docs/InputDesign.md §11 的变换通道（平移 / 缩放不改表，查表时反变换）
    就是为了免掉这次重烘，但那一档还没做——现在这条路上每次悬停进出，
    上层就是老老实实重烘一次，日志里能看到「尺寸变成 … → 命中表要重烘」
    紧跟着一次「烘焙 …」。
21. **锚点决定"怎么长"**：`Resize()` 只改尺寸，位置由锚点推。所以
    「自身 Center 对上父级 Center」是从中心四周外扩（中心点不动），
    「LeftTop 对 LeftTop」则是右下角长出去。想让放大不跑偏，锚点得配对。
