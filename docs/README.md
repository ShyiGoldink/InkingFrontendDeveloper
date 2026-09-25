# InkingFrontendDeveloper

一个基于 SDL3 的 C++20 UI 框架。核心思路：把能在编译期完成的决定
（控件类型、事件分发、布局展开）全部提前到**代码生成阶段**完成，
运行时只做每帧必须做的事。

- 形状层：SDF（符号距离场），用数学函数描述边界，零图片依赖
- 渲染层：DrawCall 合批 + 静态烘焙，不做 CPU 逐像素渲染
- 布局：百分比 + 对齐点（含 `Layout::Middle`，即 50%）
- 事件分发：自上而下直线命中，由代码生成器展开
- SDL3 引入：显式本地目录 → 系统包 → FetchContent 源码

当前处于**工程骨架**阶段：已有顶层 CMake、SDL3 引入策略和最小开窗
示例；SDF 形状层、渲染层、代码生成器尚未实现。

## 快速开始

```sh
# 方式 1：系统已装 SDL3，或允许联网自动拉取源码
cmake --preset msys2-debug
cmake --build --preset msys2-debug

# 方式 2：没装 SDL3 也不联网，用仓库里预放的本地目录
cmake -B build/msys2-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DINK_SDL3_LOCAL_DIR=third_party/sdl3
cmake --build build/msys2-debug
```

完整说明见：

- [根 README](../README.md)：环境要求、目录结构、FAQ
- [SETUP.md](SETUP.md)：SDL3 跨平台安装与配置
- [AGENTS.md](AGENTS.md)：Agent 开发约定与自检清单

## 当前状态

- [x] 顶层 CMake（可独立构建，也可被 add_subdirectory 嵌入）
- [x] SDL3 引入：本地目录 / 系统包 / FetchContent 源码
- [x] 最小开窗示例（1280×720 设计空间，letterbox）
- [ ] SDF 形状层
- [ ] 样式/渲染层
- [ ] 描述文件 + 代码生成器
