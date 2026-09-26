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

## 快速开始

```sh
# 默认：来源自动挑（本地目录 → 系统包 → 拉源码）
cmake --preset msys2-debug
cmake --build --preset msys2-debug

# 离线：用仓库里预放的本地目录，不联网也不查系统
cmake --preset msys2-debug -DINK_SDL3_SOURCE=local \
  -DINK_SDL3_LOCAL_DIR=third_party/sdl3
cmake --build --preset msys2-debug

# 没装 Ninja 时：不用预设，走系统默认生成器
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build --parallel
```

完整说明见：

- [根 README](../README.md)：环境要求、目录结构、FAQ
- [SETUP.md](SETUP.md)：SDL3 跨平台安装与配置
- [AGENTS.md](AGENTS.md)：Agent 开发约定与自检清单

## 当前状态

- [x] 顶层 CMake（可独立构建，也可被 add_subdirectory 嵌入）
- [x] SDL3 来源策略：`INK_SDL3_SOURCE`（auto / system / fetch / local）
- [x] 最小开窗示例（1280×720 设计空间，letterbox）
- [x] 日志系统（HTML）、消息队列、任务队列与线程池
- [x] 编译期开关 ISDEBUG / ISLOG / ISMESSAGE（关闭后对应代码不进二进制）
- [x] InkingWindow 单例 + 事件泵 + 鼠标输入接入（绘制仍是占位）
- [x] 场景层原型（`test002`）：静态烘焙（命中索引 + 平铺绘制表，等宽等距走算术查找）
- [x] 输入：isDirty + 三态 query（Block / PassThrough / Miss）+ 显式 zindex
- [x] 重绘：与输入独立的一路，需要重绘的属性由写入口明确调 `makeDirty()`
- [x] 最小完整测试 `examples/menu_bar`（一个窗口 + 顶部 3 按钮菜单栏，点击只写日志）
- [ ] SDF 形状层
- [ ] 样式/渲染层
- [ ] 描述文件 + 代码生成器
