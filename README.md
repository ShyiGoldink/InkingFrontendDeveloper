# InkingFrontendDeveloper

一个基于 SDL3 的 C++20 UI 框架。核心思路：把能在编译期完成的决定
（控件类型、事件分发、布局展开）全部提前到**代码生成阶段**完成，
运行时只做每帧必须做的事。

- 形状层：SDF（符号距离场），用数学函数描述边界，零图片依赖
- 渲染层：DrawCall 合批 + 静态烘焙，不做 CPU 逐像素渲染
- 布局：百分比 + 对齐点（含 `Layout::Middle`，即 50%）
- 事件分发：自上而下直线命中，由代码生成器展开
- SDL3 引入：显式本地目录/桩 → 系统包 → FetchContent 源码

当前处于**工程骨架**阶段：已有顶层 CMake、SDL3 引入策略和最小开窗
示例；SDF 形状层、渲染层、代码生成器尚未实现。

## 环境要求

| 依赖 | 最低版本 | 说明 |
| --- | --- | --- |
| CMake | 3.25 | 配置与构建 |
| C++ 编译器 | C++20 支持 | 已测试 MSYS2 UCRT64 g++ 14/16；理论上支持 GCC/Clang/MSVC |
| Ninja | 1.10 | 推荐生成器；也可用 Make/VS |
| Git | 2.x | 拉取 SDL3 源码（FetchContent 时） |
| SDL3 | 3.2+ | 见下文“SDL3 引入方式” |

## 快速开始

### 方式 1：系统已安装 SDL3

项目会通过 `find_package(SDL3 CONFIG)` 自动找到系统 SDL3。

```sh
cmake --preset msys2-debug
cmake --build --preset msys2-debug
```

如果 SDL3 装在非标准位置，用 `CMAKE_PREFIX_PATH` 指定：

```sh
cmake -B build/msys2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/path/to/your/sdl3
```

### 方式 2：自动拉取 SDL3 源码（默认，需要网络）

未检测到系统 SDL3 时，CMake 会自动从官方 GitHub 拉取
`release-3.4.2` 源码一起编译：

```sh
cmake --preset msys2-debug
cmake --build --preset msys2-debug
```

想切换 SDL3 版本：

```sh
cmake -B build/msys2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DINK_SDL3_GIT_TAG=release-3.4.2
```

### 方式 3：本地 SDL3 目录（无需网络，推荐离线使用）

把 SDL3 安装/解压到一个本地目录（平铺布局或 `x86_64-w64-mingw32/`
这类三元组布局都支持），然后配置时指定：

```sh
cmake -B build/msys2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DINK_SDL3_LOCAL_DIR=C:/path/to/your/sdl3
```

这是显式选项，优先于系统 SDL3，也不联网拉源码。

### 方式 4：离线开发桩（仅验证编译链路）

没有 SDL3 也没有网络时，可用项目自带的**开发桩**验证工程能否编译、
链接、运行。它只实现了示例用到的少量 API，**不能**证明运行期行为正确。

```sh
cmake --preset offline-stub
cmake --build --preset offline-stub
```

产物在 `build/stub/examples/basic/basic_window.exe`，可用
`INK_AUTOQUIT=1` 无头冒烟运行（约 2 秒后自动退出）。

## 目录结构

```natrue
.
├─ CMakeLists.txt           顶层构建：库 + 示例；支持被 add_subdirectory
├─ CMakePresets.json        CMake 预设（msys2-debug / msys2-release / offline-stub）
├─ cmake/
│  └─ SDL3.cmake             SDL3 引入策略（系统 / FetchContent / 桩）
├─ docs/
│  ├─ README.md              本文件
│  ├─ SETUP.md               SDL3 安装与配置指南（跨平台）
│  └─ AGENTS.md              Agent 开发入口
├─ include/ink/              公共头文件（对外 API）
├─ src/                      核心库源码
├─ examples/basic/           最小开窗示例（1280×720 letterbox）
├─ third_party/sdl3_stub/    离线开发桩
└─ third_party/sdl3/         本地 SDL3 安装（可选，手动放入）
```

> 说明：`docs/README.md` 是精简入口版，只保留介绍与快速开始；
> 完整内容以本文件为准。

## 常用命令

```sh
# 配置 + 构建（真实 SDL3）
cmake --preset msys2-debug && cmake --build --preset msys2-debug

# 配置 + 构建（离线桩）
cmake --preset offline-stub && cmake --build --preset offline-stub

# Release
cmake --preset msys2-release && cmake --build --preset msys2-release
```

## 检查 SDL3 是否可用

```sh
# 系统包
cmake --find-package -DNAME=SDL3 -DCOMPILER_ID=GNU -DLANGUAGE=C \
  -DMODE=EXIST

# 或直接看 CMake 配置日志里的 "Inking: using ... SDL3"
cmake -B /tmp/ink-check -G Ninja -DCMAKE_BUILD_TYPE=Debug
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
不能，SDL3 是唯一底层依赖。离线桩只用于无 SDL3 环境验证编译链路。

**为什么离线的桩不是真 SDL3？**
桩头文件只覆盖当前示例用到的 API（初始化、窗口、渲染器、事件等）。
扩展代码前先补桩，或直接安装真实 SDL3。

**谁使用这个库？**
源码分发。使用方通过 `add_subdirectory` 或 `FetchContent` 引入本库，
SDL3 自动按“显式本地目录/桩 → 系统包 → 源码”策略解决。

**哪些系统支持？**
SDL3 支持的平台就是本项目支持的平台（Windows/Linux/macOS/等）。

**当前能跑哪些示例？**
`examples/basic`：开一个 1280×720 letterbox 窗口并绘制占位色块。

## 当前状态

- [x] 顶层 CMake（可独立构建，也可被 add_subdirectory 嵌入）
- [x] SDL3 引入：系统包 / 本地目录 / FetchContent 源码 / 离线桩
- [x] 最小开窗示例（1280×720 设计空间，letterbox）
- [x] 日志系统（HTML，按天与程序启动会话分组）、消息队列、任务队列与线程池
- [x] 编译期开关 ISDEBUG / ISLOG / ISMESSAGE：关闭后对应代码不进二进制
- [x] InkingWindow 单例（窗口尺寸运行期可调，设计尺寸是编译期常量）
- [ ] 事件泵与绘制入口
- [ ] SDF 形状层
- [ ] 样式/渲染层
- [ ] 描述文件 + 代码生成器
