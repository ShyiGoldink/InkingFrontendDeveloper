# InkingFrontendDeveloper

一个基于 SDL3 的 C++20 UI 框架。核心思路：把能在编译期完成的决定
（控件类型、事件分发、布局展开）全部提前到**代码生成阶段**完成，
运行时只做每帧必须做的事。

- 形状层：SDF（符号距离场），用数学函数描述边界，零图片依赖
- 渲染层：DrawCall 合批 + 静态烘焙，不做 CPU 逐像素渲染
- 布局：百分比 + 对齐点（含 `Layout::Middle`，即 50%）
- 事件分发：自上而下直线命中，由代码生成器展开
- SDL3 来源：`INK_SDL3_SOURCE` 四选一（auto / system / fetch / local）

当前处于**工程骨架**阶段：已有顶层 CMake、SDL3 引入策略和最小开窗
示例；SDF 形状层、渲染层、代码生成器尚未实现。

## 环境要求

| 依赖 | 最低版本 | 说明 |
| --- | --- | --- |
| CMake | 3.25 | 配置与构建 |
| C++ 编译器 | C++20 支持 | 已测试 MSYS2 UCRT64 g++ 14/16；理论上支持 GCC/Clang/MSVC |
| Ninja | 1.10 | CMake 预设（`msys2-*`）用的生成器；没装也能构建，见“没有 Ninja 时” |
| Git | 2.x | 拉取 SDL3 源码（`INK_SDL3_SOURCE=fetch` 时） |
| SDL3 | 3.2+ | 见下文“SDL3 来源” |

## 快速开始

### 先说生成器

预设（`msys2-debug` / `msys2-release` / `test-debug`）用的是 Ninja：

```sh
pacman -S mingw-w64-ucrt-x86_64-ninja   # MSYS2 UCRT64
apt install ninja-build                 # Debian/Ubuntu
brew install ninja                      # macOS
```

没装 Ninja 也能构建，看下面“没有 Ninja 时”。

### 默认路径：自动挑 SDL3 来源

```sh
cmake --preset msys2-debug
cmake --build --preset msys2-debug
```

来源由 `INK_SDL3_SOURCE` 决定，默认 `auto`；配置日志会打印实际用了哪一个：

```text
Inking: SDL3 来源策略 = auto
Inking: using system SDL3 (3.4.2)              # 用了系统包
Inking: using local SDL3 (3.4.2) at ...        # 用了 INK_SDL3_LOCAL_DIR
Inking: SDL3 not found, fetching source tag release-3.4.2...
Inking: SDL3 built from source (release-3.4.2) # 拉源码一起编译
```

| `-DINK_SDL3_SOURCE=` | 行为 |
| --- | --- |
| `auto`（默认） | 设了 `INK_SDL3_LOCAL_DIR` 就用它；否则找系统包；都没有才拉源码 |
| `system` | 只用系统装的 SDL3；找不到直接报错，不联网 |
| `fetch` | 总是拉官方源码一起编译，忽略系统里装了什么 |
| `local` | 只用 `INK_SDL3_LOCAL_DIR` 指的目录，不联网也不查系统 |

### 指定本地 SDL3（离线推荐）

```sh
cmake --preset msys2-debug -DINK_SDL3_SOURCE=local \
  -DINK_SDL3_LOCAL_DIR=C:/path/to/your/sdl3
```

平铺布局和 `x86_64-w64-mingw32/` 这类三元组布局都认。想每次配置都不敲
`-D`，就把这两行写进 `cmake/CMakeUserPaths.cmake`（该文件被 gitignore，
存在即自动加载），模板见 `cmake/CMakeUserPaths.cmake.example`。

### 指定只拉源码编译（需要网络）

```sh
cmake --preset msys2-debug -DINK_SDL3_SOURCE=fetch

# 换版本
cmake --preset msys2-debug -DINK_SDL3_SOURCE=fetch \
  -DINK_SDL3_GIT_TAG=release-3.4.2
```

### 没有 Ninja 时

预设只是便利，`CMakeLists.txt` 本身与生成器无关。不用预设、不指定生成器，
走系统默认（Linux/macOS 是 Make）即可：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

注意别顺手加 `-G Ninja`：没装 Ninja 时它会在读到 `CMakeLists.txt`
之前就失败，报的是 `CMAKE_MAKE_PROGRAM is not set` 这种不好懂的错。

### 无头冒烟（可选）

设了 `INK_AUTOQUIT` 就跑约 2 秒然后自己退出，用来在没有人盯着屏幕时
确认窗口真的开得起来、主循环真的转得动：

```sh
INK_AUTOQUIT=1 build/msys2-debug/bin/basic_window.exe   # 开窗 + 占位绘制
INK_AUTOQUIT=1 build/msys2-debug/bin/ink_test.exe       # 自检 + 跑一次窗口主循环
```

## 目录结构

```natrue
.
├─ CMakeLists.txt           顶层构建：库 + 示例；支持被 add_subdirectory
├─ CMakePresets.json        CMake 预设（msys2-debug / msys2-release / test-debug）
├─ cmake/
│  ├─ SDL3.cmake             SDL3 来源策略（auto / system / fetch / local）
│  └─ CMakeUserPaths.cmake.example  本机路径覆盖模板（复制去掉 .example 即生效）
├─ docs/
│  ├─ README.md              本文件
│  ├─ SETUP.md               SDL3 安装与配置指南（跨平台）
│  └─ AGENTS.md              Agent 开发入口
├─ include/ink/              公共头文件（对外 API）
├─ src/                      核心库源码
├─ examples/basic/           最小开窗示例（1280×720 letterbox）
└─ third_party/sdl3/         本地 SDL3 安装（可选，手动放入）
```

> 说明：`docs/README.md` 是精简入口版，只保留介绍与快速开始；
> 完整内容以本文件为准。

## 常用命令

```sh
# 配置 + 构建（来源自动挑，默认路径）
cmake --preset msys2-debug && cmake --build --preset msys2-debug

# 配置 + 构建（用仓库里预放的本地 SDL3，不联网）
cmake --preset msys2-debug -DINK_SDL3_SOURCE=local \
  -DINK_SDL3_LOCAL_DIR=third_party/sdl3 && cmake --build --preset msys2-debug

# 配置 + 构建（这台机器没装 Ninja 时）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel

# Release
cmake --preset msys2-release && cmake --build --preset msys2-release
```

## 检查 SDL3 是否可用

```sh
# 系统包
cmake --find-package -DNAME=SDL3 -DCOMPILER_ID=GNU -DLANGUAGE=C \
  -DMODE=EXIST

# 或让配置自己说清楚：日志里会打印用了哪个来源
cmake -S . -B /tmp/ink-check -DCMAKE_BUILD_TYPE=Debug

# 只允许用系统包，找不到就报错（不偷偷联网）
cmake -S . -B /tmp/ink-check-system -DINK_SDL3_SOURCE=system
```

## SDL3 安装指引

不同系统安装 SDL3 开发包的方式不同，详见
[docs/SETUP.md](docs/SETUP.md)。快速参考：

| 系统 | 命令 |
| --- | --- |
| Windows (MSYS2 UCRT64) | `pacman -S mingw-w64-ucrt-x86_64-sdl3` |
| Windows (MSYS2 MINGW64) | `pacman -S mingw-w64-x86_64-sdl3` |
| Debian/Ubuntu | `sudo apt install libsdl3-dev` |
| Fedora | `sudo dnf install SDL3-devel` |
| Arch | `sudo pacman -S sdl3` |
| 源码（任意平台） | 见 SETUP.md 的 FetchContent / 手动编译 |

## FAQ

**这个库能不能不用 SDL3？**
不能，SDL3 是唯一底层依赖。要离线构建，就用 `INK_SDL3_SOURCE=local`
指向本地 SDL3 目录。

**谁使用这个库？**
源码分发。使用方通过 `add_subdirectory` 或 `FetchContent` 引入本库，
SDL3 由 `INK_SDL3_SOURCE` 决定从哪来（默认 auto：本地目录 → 系统包 → 源码）。

**哪些系统支持？**
SDL3 支持的平台就是本项目支持的平台（Windows/Linux/macOS/等）。

**当前能跑哪些示例？**
`examples/basic`：开一个 1280×720 letterbox 窗口并绘制占位色块。

**Release 构建双击运行，为什么什么都看不到？**
`ISDEBUG` 关闭时可执行文件是 GUI 子系统，本来就没有控制台窗口。
想临时看输出就从终端启动（`build/msys2-release/bin/basic_window.exe`），
或者改用 Debug 构建。

## 当前状态

- [x] 顶层 CMake（可独立构建，也可被 add_subdirectory 嵌入）
- [x] SDL3 引入：本地目录 / 系统包 / FetchContent 源码
- [x] 最小开窗示例（1280×720 设计空间，letterbox）
- [x] 日志系统（HTML，按天与程序启动会话分组）、消息队列、任务队列与线程池
- [x] 编译期开关 ISDEBUG / ISLOG / ISMESSAGE：关闭后对应代码不进二进制
      （ISDEBUG 关闭时连控制台窗口一起去掉）
- [x] InkingWindow 单例（窗口尺寸运行期可调，设计尺寸是编译期常量）
- [x] 事件泵 + 鼠标输入接入（`Show()` 内主循环；绘制仍是占位）
- [ ] SDF 形状层
- [ ] 样式/渲染层
- [ ] 描述文件 + 代码生成器
