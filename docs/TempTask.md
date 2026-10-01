# 接下来做什么

> 这份是**跨会话交接**：上一个对话上下文太长，换新对话时先读这里。
> 内容分三块：**已经有什么**（别重复踩）、**接下来做什么**（按顺序）、
> **约定与坑**（别重复踩）。更细的实现细节在 `docs/AGENTS.md` 与 `docs/DevelopLog.md`；
> "现在怎么用"（逐项能力状态、上手代码、字段速查、排错表）在 `docs/UsageReport.md`。

---

## 一、当前状态速览

**项目**：`InkingFrontendDeveloper` — 基于 SDL3 的 C++20 UI 框架。
核心思路：能在编译期定的决定（控件类型、事件分发、布局展开）全部提前到
代码生成阶段，运行时只做每帧必须做的事。

**构建**：三套预设全绿（`msys2-debug` / `msys2-release` / `test-debug`），零警告。

**依赖**：SDL3（必需）+ SDL3_image、SDL3_ttf（可选，缺失只降级不报错）。
三者都是四选一来源策略（`auto` / `system` / `fetch` / `local`）。

**八个自检**（改完必须全跑，退出码 0 才算过；`ctest` 里都挂着）：

| 程序 | 验什么 |
| --- | --- |
| `build/<预设>/bin/ink_test.exe` | 主自检，169 项断言（日志 / 队列 / 线程 / 锚点 / 场景 / 渲染树 / 时间线） |
| 同上 + `INK_AUTOQUIT=1` | 真开窗跑一遍主循环，约 2 秒自退 |
| `build/<预设>/bin/ink_render_probe.exe` | **离屏像素校验**：z 序压盖、背景板、可见性（不依赖桌面） |
| `build/<预设>/bin/ink_button_test.exe` | Button：形状 SDF / 命中 / 颜色换算 / 三态 / 回调 / 批次登记 / **离屏三态像素** |
| `build/<预设>/bin/inkgen_test.exe` | 生成器：JSON 解析、字段映射、**每一条报错**、产物内容 |
| `build/<预设>/bin/ink_generated_test.exe` | 生成代码的运行期效果：静态登记 → 库里查得到 → 按名字构造 |
| `build/<预设>/bin/buttons_demo.exe`（+`INK_AUTOQUIT=1`） | 端到端：配置 → 库 → 渲染树 → 模拟点击 → 回调；然后真开窗 |
| `build/<预设>/bin/ink_image_test.exe` / `ink_ttf_test.exe` | 两个可选依赖的接入探针 |

**已落地的能力**（都能用，有自检）：

- 锚点层：位置由「自身锚点 + 上级锚点 + 偏移」推导，静态 / 动态两档
- 渲染树：扁平绘制列表 + 惰性排序，按 **z 从小到大**提交；可见性、层级、父子隐藏
- 场景：`SceneLibrary` 登记（**同名同期只能有一个**）、活跃态、静默 / 停放 /
  `CloseScene`（只摘登记不析构）
- 时间线：固定 50Hz 逻辑步 + 每帧通道 + 自定义频率订阅，累加器追赶，截断防卡死
- 窗口：vsync 节流、真实 delta、letterbox 坐标换算
- 基础设施：HTML 日志、消息队列、任务队列、线程池、ISDEBUG/ISLOG/ISMESSAGE 开关
- **Button 组件框架**（`include/button/`）：数据与逻辑分开、名字 → 配置的登记表、
  三态状态机（只换颜色）、点击回调；`ink_button_test.exe` 全绿
- **形状层第一档**（`include/ink/dataStruct/InkingShapeSpec.h`）：
  `rect` / `roundedRect` / `circle` / `ellipse` 的 SDF + 命中 + AABB，
  三者同源；**填充还没走 SDF**（仍是直角矩形，已知且自检有记录）
- **代码生成器 `inkgen`**（`tools/inkgen/`）：读 `CXXCSS/Button/*.json` →
  校验 → 生成到 `build/`；CMake 一行接入（`ink_add_generated`）。
  配置写错在**构建期**就红，产物带名字常量与启动期自动登记
- **点击真的能到按钮回调**（最小闭环）：`InkingAnchor::HitTest` +
  `InkingScene::DispatchPointer` + 窗口主循环每帧喂指针。
  **不是**完整命中方案（没有命中表、没有三态、没有 isDirty 帧计数）
- **绘制的 alpha 混合是开的**：窗口打开渲染器时设一次
  `SDL_BLENDMODE_BLEND`。SDL 的绘制混合**默认是关的**，不设的话颜色的 alpha
  会被原样写进像素——"半透明"根本不存在，按钮三态在屏幕上完全一样
  （踩过，详见 `AGENTS.md §6` 第 32 条）。所以 `color` 里的 alpha 现在真的有意义
- **示例 `examples/buttons_demo/`**：一个 json + 一个场景类 + 一句 `Show()`
  = 一个能点的窗口，`INK_AUTOQUIT=1` 下自己模拟一次点击、
  **并离屏采三态的像素**验证"悬停比通常亮"

**未实现**：形状层的**抗锯齿填充**、完整的命中三态 query 与命中表、
文字渲染（字形图集 / 纹理层）、`img` / `svg` 外观、Scene 的 CXXCSS 生成、
DrawCall 合批。

---

## 二、接下来做什么（按顺序）

### 第 1 步：以 Button 为样板，把组件框架搭全 ← **框架已落地，下一步是形状填充**

**目标**：做一个完整组件，把"一个组件应该怎么写"这套框架定下来。
之后其它组件（Label / Panel / Input…）照抄结构，只实现自己的独特逻辑。

**现状：静态按钮的框架已经完整落地**（自检 `ink_button_test.exe` 全绿）：

```text
include/button/InkingStaticButton.h   组件：三态状态机 + 点击回调 + onRender
include/button/ButtonData.h           纯数据：对应 button.example.json 的每个字段
include/button/ButtonLibrary.h        名字 → ButtonData 的登记表（生成器的落点）
src/button/*.cpp

include/ink/dataStruct/InkingColor.h      颜色：0xAARRGGBB + 从 "0|0|0|0.75" 换算
include/ink/dataStruct/InkingShapeSpec.h  形状：rect / roundedRect / circle / ellipse
include/ink/basic/InkingDraw.h            渲染助手（渲染器槽 + 按形状填色）
                 ^ 这个头 include 了 SDL，是**唯一**破了"公开头不拖 SDL"纪律的地方
                   （它是渲染层内部件，业务代码应该重写 onRender 而不是调它）
```

**已经拍板的四件事**（理由都写在 `InkingStaticButton.h` 顶上，别回头改）：

1. **Button 是静态组件**。三态只换颜色 → 颜色不标脏 → 进命中表。
   按下要做"下沉"就用绘制偏移（`onRender` 里挪几像素），不是几何变化。
   `src/ink.cpp` 里有编译期断言：静态组件不得出现 Resize /
   ChangeSelfAnchor / ChangeTraceAnchor / ChangeOffset。
2. **形状层第一档已经和 Button 一起落地**：`SignedDistance` / `ShapeContains` /
   `GetShapeBounds` 三个派生量来自同一份定义，命中已经按圆角在判。
3. **渲染后端没换**，所以填充还是直角矩形（下一件事就是这个）。
4. **`text` 只实现到"能表达"**：字段齐全，但画的是同尺寸占位方块
   （要字形图集 + 纹理层才是真文字）。
   ⚠️ `CXXCSS.md` §5 的 `text` 字段**仍待核对**：那是从原示例注释推断的，
   没有示例文件对照。这次按它实现了，等于把它固化了一次——
   要改的话现在改，代价最小。

**下一步（第 2 步的直接入口）**：定下圆角**填充**方案（TempTask 第 2 步的 a/b/c），
然后换掉 `include/ink/basic/InkingDraw.h` 里的 `fillShapeBounds` 一个函数即可。
自检里那条"填充仍是直角矩形"的 `[记录]` 就是这项工作的验收点：
做完之后它应该能从"记录"改成断言。

**注意事项**：

- 组件要复用已有基座：`InkingStaticAnchor` / `InkingDynamicAnchor`、
  `InkingScene` 的登记、`SceneLibrary::RenderScene` 的提交层
- 新增公开头后，**必须让它被至少一个 .cpp 或自检 include 一次**
  （否则可能是永远编译不过的死文件，而构建照样全绿——踩过）
- `MouseHover` / `MousePress` / `MouseRelease` 现在**没人喂**：
  等第 3 步把鼠标事件接到场景层，由命中结果推它们（接口已经按那时候的形状定好了）

### 第 2 步：形状层（SDF）—— 与 Button 紧耦合，几乎是同一个会话的活

**为什么排这么前**：Button 要圆角，圆角必须形状层。两者不能分开做。

**格式已经定型**，见 `CXXCSS/CXXCSS.md` §3：

- 写法：`{"type":"roundedRect","radius":10}` —— **对象 + 参数，不把参数编进字符串**
- 符号约定：**负 = 内**（`f<0` 在内部，裁剪保留 `f<=0`）。这是通用约定，
  照抄公开 SDF 实现不用翻符号。AGENTS §3.2 已同步
- **形状同时决定渲染 / 命中 / AABB，三者从同一份定义派生**（最重要的一条）
- 第一档先做：`rect`、`roundedRect`、`circle`、`ellipse`
- 第二档：`capsule`、`ring`、`line`、`polygon`
- **明确不支持**：虚线（非单一距离场）、任意贝塞尔路径（要数值最近点求解）、
  文字（走 `text`）、纹理填充（纹理层的事）

**待定的关键选择**：圆角的**抗锯齿填充**需要 GPU（`SDL_GPUShader` 或预烘距离场），
现在只有 `SDL_Renderer` + `SDL_RenderFillRect`。三个选项：

- (a) 换 SDL3 GPU API → 圆角/抗锯齿能做，但渲染层要重写
- (b) 用 `SDL_RenderGeometry` 三角化圆角（CPU 分段，不改后端）→ 够用且便宜
- (c) 形状层先只做裁剪 / 命中，填充暂时还是直角矩形 → 半成品

**(b) 可能是性价比最高的**，因为它不需要换后端。

### 第 3 步：命中与三态 ← **只落了最小闭环，完整方案还没做**

**设计稿已在 `docs/InputDesign.md`**（很完整，先读它）。

**现在已经有的（够用，但别当成方案落地了）**：

- `InkingAnchor::HitTest(worldX, worldY)`：`[√]` 钩子，默认 false；
  Button 用 `ShapeContains` 按形状判（与渲染同一份定义）。
  **z 序不在这里判**，由调用方按绘制列表从高到低问。
- `InkingScene::SetPointerState(...)` + `DispatchPointer()`：**线性扫**绘制列表，
  从列表尾往前取最上面那个；悬停进出、按下、抬起触发点击、按着滑出再松开算取消。
- `InkingWindow` 主循环每帧喂一次（`pumpEvents` 之后、推进时间线之前）。

**还差的（这才是这一步的正文）**：

- 三态：`Block` / `PassThrough` / `Miss`（现在只有"命中/没命中"）
- 命中要**带目标**（只给三态没地方投事件）——建议"三态 + 目标"一个结构
- 冒泡 ≠ 穿透：目标返回 true 不再上冒；`PassThrough` 是继续往下找
- **点击和 hover 共用同一个 query**，只是 `isClick` 不同；点击不经过脏标记，单独查
- `isDirty` 生命周期：鼠标移动 / 几何 / 层级 / 可见性 → 脏；每帧 `if (isDirty) query()`，
  连续 2~3 帧无标脏才清
- 命中表四条硬规则（`InputDesign.md` §10.5）：兜底条目单独存、溢出桶、
  动态组件的"洞"、跨簇条目登记规则
- 静态烘焙的命中表（现在是 O(节点数) 线性扫，几十个组件够用，成百上千就该换）

**已经为它准备好的**：`InkingAnchor::IsAbove`（z 优先、z 相同看注册序号）已经写好
并测过；渲染提交顺序和它是**反向同源**的。`_dirty` + `MarkDirty` 已实现，
只差帧计数那一段。`DispatchPointer` 的调用点就是将来查表的地方，换实现不动调用方。

### 第 4 步：代码生成器 `inkgen` ← **已落地，还剩两件事**

**已经能用了**（`tools/inkgen/`，构建期生成到 `build/`）：

- 读 `CXXCSS/Button/*.json` → 校验 → 生成"名字常量 + 登记入口"的头 +
  "配置数据 + 启动期自动登记"的源文件；
- CMake 一行接入：`ink_add_generated(目标 OUT_DIR … SOURCES …)`；
- 配置写错在**构建期**报错（未知字段 / 名字不符 / 参数非法 / 重名），
  每条带文件与行号；`--validate` 只校验不写文件；
- 自检：`inkgen_test.exe`（生成器本身）+ `ink_generated_test.exe`（产物的运行期效果）。

**还剩**：

1. **每个名字生成一个独立类**（`CXXCSS.md` §6 第 1 条）——
   现在所有按钮还是同一个 `InkingStaticButton`，只生成了常量与配置；
2. **按布局类型生成专用查找**（§6 第 3 条）——命中还是线性扫。

### 第 5 步：Scene 的 CXXCSS 生成

`CXXCSS/Scene/<场景名>.json` 目前是**纸面约定**：场景名还是代码里的常量，
`InkingScene` 的尺寸与锚点由 `sceneRootAnchorData()` 按设计画布定型。
要做的话照 Button 那条路走一遍即可（生成器 + 一个 `k<场景名>Name` 常量）。

### 第 6 步（最后）：DrawCall 合批

用户明确说"最头疼，放最后优化"。
**提交层已经占好位**：`SceneLibrary::RenderScene` 是唯一落点，换合批器只改这一处。

---

## 三、约定与坑（别重复踩）

### 硬约定

| 约定 | 内容 |
| --- | --- |
| C++20 | `ink_core` 已设 `cxx_std_20` |
| 形状层 | SDF，**负 = 内**，裁剪保留 `f<=0` |
| 渲染 | DrawCall 合批 + 静态烘焙，**禁止 CPU 逐像素渲染** |
| 布局 | 百分比 + 对齐点（`Layout::Middle` = 50%） |
| 事件 | 自上而下直线命中，由生成器展开 |
| 公开头 | **不包含 `<SDL3/SDL.h>`**，只前向声明；`SDL_Event` 是 `union` |
| 依赖来源 | 必须显式可控，别写死；本机路径写 `cmake/CMakeUserPaths.cmake` |
| 开关 | ISDEBUG/ISLOG/ISMESSAGE 及 `INK_HAS_SDL3_*` 都挂 PUBLIC（否则 ODR 违规） |
| 命名 | 写入口 `PascalCase`，钩子 `on` 开头；成员 `_camelCase`（**别用 `m_`**） |

### 最容易踩的坑（完整 28 条在 `AGENTS.md` §6）

1. **别用 PowerShell 读写源码**：`Set-Content` / `-replace | Set-Content` 会按控制台
   编码回写，中文注释变乱码、文件不再是合法 UTF-8。**已踩两次**
   （`InkingAnchor.cpp` 靠 git checkout 救回，`render_probe.cpp` 是新文件只能整个重写）。
   改源码一律用编辑工具。
2. **绘制提交是 z 从小到大**，和 `IsAbove` 的答案**相反**（画家算法先画底下的）。
   第一版按降序排 → 铺满整屏的背景板最后画，把画面全盖了，**而所有断言都是绿的**。
   这类"顺序对但方向错"只有真像素能钉死。
3. **可见性不能靠"从绘制列表里删掉"**：删除是单向的（只有构造函数会加回来），
   会导致"隐藏过的节点永远回不来"。正确做法是**列表保留、提交时跳过**。
4. **动态节点的几何写入口也要通知场景**：列表里存的是绝对坐标快照，
   不通知则它的**静态子孙**坐标停在旧值。
5. **追不上就丢的限制必须写在累加器循环的入口**：写在循环后面永远触发不了
   （循环一定会把累加器排干）。
6. **别按帧数算时间**：逻辑 50Hz / 显示器 60~144Hz / 动画 24fps 互不整除，
   只能按真实流逝时间累加。
7. **`find_package` 结果会进 cache**：探测前必须清 `<Pkg>_DIR`，
   否则来源开关形同虚设。探测结果用普通 CACHE，别用 `CACHE INTERNAL`。
8. **`CMAKE_PREFIX_PATH` 排除不掉系统包**：MSYS2 工具链前缀本来就在隐式搜索路径里。
   `local` 策略要自己算配置目录 + `NO_DEFAULT_PATH`；`system` 则**不要**加。
9. **静默初始化顺序**：注册表用**函数内静态量**，不用类的静态数据成员
   （后者有静态初始化 / 析构顺序问题）。
10. **被驳回的登记令牌绝不能去注销**：否则后来者一析构，正主从库里凭空消失。

### 验证方式的选择（重要）

- **能变成数值判据的**（像素颜色、坐标、数量、顺序）→ 自己写断言验；
  渲染类用**离屏读回**（`tests/render_probe.cpp` 那条路）。
- **需要"看图"才能判断的**（字体、颜色观感、间距别扭、动画顺眼）→
  **停下来交给用户**，把示例跑起来、说清该看什么。**别试图截图**——
  这台机器上别的窗口会遮挡，实测折腾很久只捕到别人家的窗口。

### 开发日志要维护

`docs/DevelopLog.md` 记到 Step 10。每做完一块就补一条，
写"做了什么 + **为什么这么选**"，后者比前者值钱。

---

## 四、开工前先跑的检查

```sh
# 三套预设构建 + 自检
cmake --preset msys2-debug   && cmake --build --preset msys2-debug
cmake --preset msys2-release && cmake --build --preset msys2-release
cmake --preset test-debug    && cmake --build --preset test-debug

# 一把全跑（推荐）：9 个用例，含两个 CXXCSS 校验
cd build/test-debug && ctest --output-on-failure

# 或者逐个跑
build/test-debug/bin/ink_test.exe
build/test-debug/bin/ink_render_probe.exe
build/test-debug/bin/ink_button_test.exe
build/test-debug/bin/inkgen_test.exe
build/test-debug/bin/ink_generated_test.exe
build/test-debug/bin/ink_image_test.exe
build/test-debug/bin/ink_ttf_test.exe
INK_AUTOQUIT=1 build/test-debug/bin/ink_test.exe        # 开窗冒烟
INK_AUTOQUIT=1 build/test-debug/bin/buttons_demo.exe    # 端到端：生成→点击回调
```

构建时会自动跑 `inkgen`（读 `CXXCSS/Button/*.json` 生成到 `build/`），
所以**配置写错在构建期就会红**——看到 `[错误] …路径…:行: 说明` 就直接改 json。

没装系统 SDL3 时加 `-DINK_SDL3_SOURCE=local -DINK_SDL3_LOCAL_DIR=third_party/sdl3`。
没装 Ninja 时用 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`（**别写 `-G Ninja`**）。

> **别把 `LIB` 环境变量带进命令**（这台机器上它是 `;C:\mingw-64-sdl3\lib\x64;C:\SDL3\lib\x64`，
> 两个目录都不存在）。某些 PowerShell 脚本用 `Add-Type` 时会因为这条无效搜索路径
> 直接报 `Exception=0x80131500/win32=5376`，看起来像权限或编译错误，其实只是环境变量脏。
> 本次就撞过一次（ACL 诊断脚本）。跑 cmake / 编译前 `Remove-Item Env:LIB` 最省事。

---

## 五、跨会话时要交代的一句话

> 读 `docs/TempTask.md`（这份）、`docs/AGENTS.md`（约定 + 已知坑）、
> `docs/CodeStyleRule.md`（编码与解耦规范）、`CXXCSS/CXXCSS.md`（配置格式），
> 然后按「二、接下来做什么」的顺序推进。
> **当前状态**：Button 从 json 到"窗口里一个能点的按钮"这条链路已经全通
> （生成器 + 组件 + 指针派发 + 端到端示例，`ctest` 9 个用例全绿）。
> 下一步二选一：**形状层的抗锯齿填充**（换掉
> `include/ink/basic/InkingDraw.h` 的 `fillShapeBounds` 一个函数，圆角就真的圆了），
> 或者**第 3 步的完整命中与三态**（现在是线性扫的最小闭环）。
