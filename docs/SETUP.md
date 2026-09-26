# SDL3 安装与配置指南

本文档说明如何为 InkingFrontendDeveloper 准备 SDL3 环境，覆盖
Windows（MSYS2）、Linux、macOS，以及“没有 SDL3 时”的做法。

> 提示：SDL3 从哪来由 `INK_SDL3_SOURCE` 决定，四选一：
> `auto`（默认，本地目录 → 系统包 → 拉源码）/ `system`（只用系统包）/
> `fetch`（总是拉源码）/ `local`（只用 `INK_SDL3_LOCAL_DIR`）。
> 显式指定的来源不会被静默替换：说好用哪个，用不了就直接报错。
>
> 本机路径不想每次敲 `-D`，写进 `cmake/CMakeUserPaths.cmake`（被
> gitignore，存在即自动加载），模板见 `cmake/CMakeUserPaths.cmake.example`。

## 0. 先确认有没有 SDL3

```sh
# Linux / macOS：包管理器查询
pkg-config --modversion sdl3                    # 期望输出 3.x.y

# Windows (MSYS2)：注意包名是小写的 sdl3
pacman -Q mingw-w64-ucrt-x86_64-sdl3

# 通用：让 CMake 找一下
cmake --find-package -DNAME=SDL3 -DCOMPILER_ID=GNU -DLANGUAGE=C -DMODE=EXIST
```

也可以直接跑一次 `cmake --preset msys2-debug`，看配置日志开头：

```text
Inking: SDL3 来源策略 = auto
Inking: using local SDL3 (3.x.y) at ...   # 用了 INK_SDL3_LOCAL_DIR
Inking: using system SDL3 (3.x.y)         # 用了系统包
Inking: SDL3 not found, fetching ...      # 都没有，去拉源码
Inking: SDL3 built from source (3.x.y)    # 源码编译完成
```

### 生成器：Ninja（预设需要）

`CMakePresets.json` 里的预设（`msys2-debug` / `msys2-release` /
`test-debug`）用的是 Ninja，装一条命令就够：

```sh
pacman -S mingw-w64-ucrt-x86_64-ninja   # MSYS2 UCRT64
apt install ninja-build                 # Debian/Ubuntu
dnf install ninja-build                 # Fedora
brew install ninja                      # macOS
```

没装 Ninja 也能构建，只是别用预设，也别写 `-G Ninja`（没装时它会在
读到 `CMakeLists.txt` 之前就失败，报 `CMAKE_MAKE_PROGRAM is not set`）：

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

## 1. Windows（推荐 MSYS2）

### 方案 A：用 MSYS2 包管理器安装（要联网）

在 **MSYS2 shell** 中执行。注意包名是**小写**的 `sdl3`，不是 `SDL3`：

```sh
# UCRT64（推荐，与本项目默认的 UCRT64 工具链匹配）
pacman -S mingw-w64-ucrt-x86_64-sdl3

# 或 MINGW64（MSVCRT 运行时）
pacman -S mingw-w64-x86_64-sdl3
```

这个包会顺带装上 `vulkan-loader` 依赖。如果 pacman 报依赖冲突，
先做一次完整系统更新（`pacman -Syu`）再装。

装完后把对应环境的 `bin` 目录加入 PATH（SDL3.dll 才能被找到）：

```sh
export PATH="/c/msys64/ucrt64/bin:$PATH"
```

> **运行时匹配**：UCRT64 工具链优先配 UCRT64 版 SDL3 包。MSVCRT 版
> （MINGW64 包或官方 `-mingw` 预编译包）也能编译链接、简单场景能跑，
> 但会同时加载两套 C 运行时，属于不推荐组合。

### 方案 B：用官方 SDL3 预编译包

1. 去 libsdl.org 下载 `SDL3-devel-3.x.x-...-mingw.zip`；
2. 解压到如 `C:\libs\SDL3`；
3. 配置时告诉 CMake 位置：

```sh
cmake -B build/msys2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DCMAKE_PREFIX_PATH=C:/libs/SDL3
```

### 方案 C：自动拉取源码（FetchContent，要联网）

不预装任何东西，直接：

```sh
cmake --preset msys2-debug
cmake --build --preset msys2-debug
```

### 方案 D：本地 SDL3 目录（不需要联网）

如果你手头有 SDL3 的源码/压缩包（比如 `SDL3-devel-*.zip`），或者像
MSYS2 那样装在 `<dir>/x86_64-w64-mingw32/lib/cmake/SDL3/` 的三元组
布局里，都可以直接指过来：

```sh
cmake -B build/msys2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug `
  -DINK_SDL3_SOURCE=local `
  -DINK_SDL3_LOCAL_DIR=D:/InkingFrontendDeveloper/third_party/sdl3
```

`local` 的含义是“只用这里，不联网也不查系统”。脚本会自动挑与本机位数
匹配的三元组目录（64 位优先 `x86_64-*`）。

如果不想每次配置都带这两个 `-D`，把同样两行写进
`cmake/CMakeUserPaths.cmake`（被 gitignore，存在即自动加载），之后直接
`cmake --preset msys2-debug` 就走本地目录。模板见
`cmake/CMakeUserPaths.cmake.example`；用 `set(变量 值 CACHE 类型 "说明")`
的写法，命令行上的 `-D` 仍然可以临时覆盖它。

## 2. Linux

### Debian / Ubuntu

```sh
sudo apt update
sudo apt install libsdl3-dev cmake ninja-build g++
```

### Fedora

```sh
sudo dnf install SDL3-devel cmake ninja-build gcc-c++
```

### Arch

```sh
sudo pacman -S sdl3 cmake ninja gcc
```

### 其他发行版

包名一般是 `sdl3` 或 `libsdl3-dev`/`SDL3-devel`。也可以用
FetchContent 自动拉源码，无需系统包。

## 3. macOS

### Homebrew

```sh
brew install sdl3 cmake ninja
```

### 手动源码构建（没有包时）

```sh
git clone --depth 1 --branch release-3.4.2 https://github.com/libsdl-org/SDL.git
cmake -S SDL -B SDL/build -DCMAKE_BUILD_TYPE=Release
cmake --build SDL/build
cmake --install SDL/build
```

## 4. 源码分发 / 作为依赖被引入

使用方通过 `add_subdirectory` 或 `FetchContent` 引入本库即可：

```cmake
add_subdirectory(third_party/InkingFrontendDeveloper)
```

或

```cmake
include(FetchContent)
FetchContent_Declare(ink
    GIT_REPOSITORY <你的 ink 仓库地址>
    GIT_TAG <版本/分支>)
FetchContent_MakeAvailable(ink)
```

本库会自动带上 SDL3 引入逻辑；使用者只需保证 SDL3 有网络可拉取，
或已装系统包。这与 SDL3 官方的源码分发方式保持一致，不额外维护
预编译二进制。

## 5. 常见问题

**Q：FetchContent 一直失败，说连不上 GitHub？**
网络受限。改用系统包或本地目录，并顺手把来源钉死，免得它再偷偷联网：
`-DINK_SDL3_SOURCE=system` 或 `-DINK_SDL3_SOURCE=local -DINK_SDL3_LOCAL_DIR=<目录>`
（见第 1 节）。

**Q：装了包但 CMake 还是拉源码？**
确认 `SDL3Config.cmake` 所在的目录在 `CMAKE_PREFIX_PATH` 里，
或 `find_package(SDL3 CONFIG)` 能找到：

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_PREFIX_PATH=/path/to/your/sdl3
```

也可以直接用 `-DINK_SDL3_SOURCE=system` 强制只用系统包：找到了就用，
没找到会直接把上面的排查步骤打进错误信息里，不会再退回拉源码。

**Q：UCRT64 工具链能用官方 MSVCRT 版 SDL3 吗？**
能编译、能链接、简单场景也能跑，但会同时加载 `ucrtbase` 和 `msvcrt`
两套 C 运行时。生产建议用与工具链匹配的包（UCRT64 → 带 `ucrt` 的包）。

**Q：系统装了 SDL3，同时指定了本地目录，用哪个？**
看 `INK_SDL3_SOURCE`。默认 `auto` 时本地目录优先，设了
`INK_SDL3_LOCAL_DIR` 就不会再走系统检测；`system` 则反过来，只认系统包，
此时本地目录不参与。

**Q：运行时报找不到 SDL3.dll？**
Windows 上把 `SDL3.dll` 所在目录加入 PATH，或拷贝到可执行文件旁边。

**Q：不想联网，也不想装系统包？**
用本地目录（方案 D）：`-DINK_SDL3_SOURCE=local -DINK_SDL3_LOCAL_DIR=...`。
仓库里预放了一份 SDL3 在 `third_party/sdl3`，没有的话照第 6 节手动
下载一份即可。

## 6. 手动下载 SDL3（离线机器）

如果机器无法访问 GitHub，但有浏览器/网盘/其他机器可以下载，按下面
准备一个本地 SDL3 目录：

### 推荐：官方预编译开发包（Windows）

1. 打开 [SDL Releases](https://github.com/libsdl-org/SDL/releases)
2. 下载 `SDL3-devel-3.4.2-...-mingw.zip`（MSVC 版是 `-VC.zip`）
3. 解压后目录结构类似：

```text
SDL3-devel-3.4.2-mingw/
├─ include/SDL3/...
├─ lib/cmake/SDL3/SDL3Config.cmake
├─ lib/x64/SDL3.lib / lib/libSDL3.dll.a
└─ bin/SDL3.dll
```

1. 配置时指向该目录（见方案 D）。

### 备选：从源码自己编译

在能联网的机器上：

```sh
git clone --depth 1 --branch release-3.4.2 https://github.com/libsdl-org/SDL.git
cmake -S SDL -B SDL/build -DCMAKE_BUILD_TYPE=Release
cmake --build SDL/build
cmake --install SDL/build --prefix D:/InkingFrontendDeveloper/third_party/sdl3
```

然后把 `third_party/sdl3` 拷到离线机器，用方案 D 配置即可。
