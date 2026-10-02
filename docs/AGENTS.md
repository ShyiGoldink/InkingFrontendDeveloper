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
- [UsageReport.md](UsageReport.md)：**使用报告**——现在能用什么（逐项状态）、
  五分钟上手、配置字段速查、行为约定、排错表。想"用起来"先读它
- [InputDesign.md](InputDesign.md)：输入与命中方案设计稿（静态查表 / 动态自判 / 三态 query）
- [DevelopLog.md](DevelopLog.md)：开发日志（每一步做了什么、为什么）
- [../CXXCSS/CXXCSS.md](../CXXCSS/CXXCSS.md)：**CXXCSS 配置格式**（形状层 / 锚点 /
  Button 字段 / 生成器会拿它做什么）。写配置或改形状词汇之前先读它
- [../tools/inkgen/](../tools/inkgen/)：**代码生成器**（CXXCSS → C++）。
  改生成规则动这里；产物只落 `build/`，不进源码树
- 本文档：Agent 专属的约定与自检清单

## 3. Agent 必须遵守的技术约定

1. **C++20 起**：`ink_core` 已设置 `cxx_std_20`。
2. **形状层 = SDF**：形状用 `f(x, y) -> 带符号距离` 描述。
   符号用**通用约定**：`f < 0` 在内部、`f = 0` 在边界、`f > 0` 在外部；
   裁剪 = 保留 `f <= 0` 的区域。（早先这里写的是 `f >= 0`，方向是反的，
   已于形状层落地前纠正——理由见 `CXXCSS/CXXCSS.md` §3.4：
   能直接照抄公开参考实现、抗锯齿是标准 `smoothstep(-aa, aa, d)` 写法、
   `abs(d)` 就是到边界的距离。）
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
   改了渲染相关的代码，还要跑 `build/<预设>/bin/ink_render_probe.exe`
   （离屏渲染 + 读回像素，确定性验证 z 序与可见性，不依赖桌面状态）；
4. **需要"看图"才能判断的验证，停下来交给用户，不要自己硬扛**：
   字体 / 字形渲染、颜色观感、间距与对齐的"看起来对不对"、动画是否顺眼、
   渐变与抗锯齿质量、任何"数值都对但就是别扭"的问题——这些没有确定的
   数值判据，Agent 也看不到屏幕。截图在这台机器上会被别的窗口遮挡
   （实测只能捕到别人家的窗口，见 §6 第 20 条），继续折腾只会烧时间。
   做法：把状态准备好（示例能跑起来、日志写清怎么看、需要看哪几个点），
   说明"要看什么、预期是什么样"，然后**暂停并交给用户确认**。
   能变成数值判据的（像素颜色、坐标、数量、顺序）仍然自己用
   `ink_render_probe` 那条路验，不要交出去；
5. 更新本文档/README/SETUP 中与实现不一致的内容；
6. 不要在源码树里提交 `build/` 产物。

## 5. 当前阶段

- 已有：顶层 CMake（可 add_subdirectory）、SDL3 来源策略
  `INK_SDL3_SOURCE`（auto / system / fetch / local）+ 本机路径覆盖文件
  `cmake/CMakeUserPaths.cmake`、最小开窗示例（letterbox）；
- 已有：从后端框架移植的日志（HTML）、消息队列、任务队列、线程池，
  以及 ISDEBUG / ISLOG / ISMESSAGE 三个编译期开关；
- 已有：**SDL3_image 接入**（`cmake/SDL3_image.cmake`），与 SDL3 同一套四选一
  来源策略（`INK_SDL3_IMAGE_SOURCE` = auto / system / fetch / local）。
  它是**可选**依赖：接不上只打 WARNING、关掉图片功能，其余照常构建；
  接上后 `ink_core` 会 PUBLIC 传播 `INK_HAS_SDL3_IMAGE=1`，代码用
  `#if defined(INK_HAS_SDL3_IMAGE)` 做条件编译。自检 `tests/test_image.cpp`
  （内存 PNG → 解码 → 校验像素）；
- 已有：**SDL3_ttf 接入**（`cmake/SDL3_ttf.cmake`），同样四选一
  （`INK_SDL3_TTF_SOURCE`）、同样可选、同样 PUBLIC 传播
  `INK_HAS_SDL3_TTF=1`。自检 `tests/test_ttf.cpp`（打开系统字体 → 量中英文尺寸 →
  查汉字字形；**不涉及渲染**，文字好不好看属于"交给用户看"那一档）。
  注意三个库各自独立发版，版本号不需要对齐——`SDL3_ttfConfig` 只要求
  `SDL3 >= 3.2.6`，实测 SDL3 3.4.16 + SDL3_ttf 3.2.2 正常；
- 已有：InkingWindow 单例（窗口尺寸是运行期属性，设计尺寸是编译期常量）；
- 已有：事件泵与鼠标输入接入——`InkingWindow::Show()` 里就是主循环
  （事件泵 → 鼠标状态 → 窗口坐标换算设计坐标 → 场景渲染 → 帧末清理），
  鼠标状态在 `include/input/MouseInput.h`；
- 已有：场景层的**登记底座**——`include/scene/SceneLibrary.h`（场景库，对照后端
  ShineStatusChecker）、`include/scene/SceneRegisterToken.h`（RAII 登记令牌，
  对照后端 StatusRegisterToken，模板化为 `ink::RegisterToken<Registered, Library>`）、
  `include/scene/InkingScene.h`（场景基类，构造即登记、析构即注销）。
  登记表用函数内静态量（避开静态初始化 / 析构顺序问题），且**有意不加锁**：
  场景只在 UI 线程构造与析构；
- 已有：**场景登记语义**——**同名同期只能有一个**：名字被占就当场驳回，后来者进
  「停放态」（对象在、数据在，永不渲染也不 tick）。`SceneLibrary::CloseScene(名字)`
  只摘登记、**不析构对象**（生命周期归调用方），名字随即空出来可再建同名场景；
  被关掉的场景数据仍可读，方便"先把数据 move 出来再转场"。
  **活跃态**由库维护（`SetActiveScene` / `GetActiveScene`），同期最多一个，
  切场景时旧的自动进入**静默**；
- 已有：**三组逻辑**——渲染 / 跟着渲染的逻辑（`onRenderBoundTick`，每渲染帧一次、
  只在活跃时） / 可以自己跑的逻辑（`onTick`，时间线的固定逻辑步，一直跑）。
  `InkingScene::TickLogic(fixedStep)` 派发固定步并顺带遍历绘制列表里的**动态组件**
  逐个 `Tick`；`TickFrame(delta)` 只在活跃时派发。静默 / 停放 / 已关闭的场景按上表停；
  "不渲染但逻辑要动"用 `onTick`。`Render` 是 **protected**，
  窗口层走 `SceneLibrary::RenderScene()`；
- 已有：**时间线**（`include/ink/basic/Timeline.h`）——一条时间线 + 累加器追赶，
  驱动源在主循环。三条通道：固定逻辑步（默认 50Hz，回调收到固定 interval）/
  每帧（真实 delta）/ 自定义频率订阅（`Subscribe(1/24.0, cb)`，回调收到实际步长）。
  追不上的 delta 在**入口**截断（默认 0.25s）。节流用 `SDL_SetRenderVSync`，
  接不上时退回 `sleep_for(16ms)`；
- 已有：**渲染树**——锚点构造时沿父链找到所属场景并登记，场景维护一份按 z 排好的
  扁平绘制列表（`InkingScene::DrawItem`），`InkingScene::Render` 按 **z 从小到大**
  提交（先画的在底下）。骨架变化只标脏，重建推迟到 `Render()` 开头做一次；
  可见性在提交时判断（隐藏节点留在列表里，否则再也回不来）；
  静态节点用建表时算好的绝对坐标快照，动态节点每帧现算。
  第 0 层渲染是"按 color 填一块实心矩形"
  （`src/ink/basic/InkingAnchor.cpp` 的 `drawDefaultRect`，现在等价于
  `detail::fillShape(..., ShapeSpec::Rect(), ...)`，所以它和按钮共用同一套
  抗锯齿填充）；
- 已有：**第一个完整组件：静态 Button**（`include/button/InkingStaticButton.h`、
  `include/button/ButtonData.h`、`include/button/ButtonLibrary.h`，实现
  `src/button/*.cpp`）。它是"一个组件该怎么写"的样板：
  数据（`ButtonData`）与逻辑（组件）分开；配置由生成器烘成**每个名字一个类**；
  三态**只换颜色**，而颜色不标脏，所以它落在**静态档**（能进命中表）——
  这条在 `src/ink.cpp` 里用编译期断言钉住（静态组件不得出现几何写入口）。
  三态状态机是幂等的写入口（`MouseHover` / `MousePress` / `MouseRelease` /
  `TriggerClick`）；**指针由场景层喂**（`DispatchPointer`），组件自己不管。
  自检 `tests/test_button.cpp`（`ink_button_test.exe`）；
- 已有：**鼠标点击真的能到按钮回调**（最小闭环，**不是**完整命中方案）：
  `InkingAnchor::HitTest`（`[√]` 钩子，默认 false；Button 用 `ShapeContains`
  按形状判）+ `InkingScene::SetPointerState` / `DispatchPointer`（线性扫绘制列表、
  从列表尾往前取最上面那个；悬停进出、按下、抬起触发点击；按着滑出再松开算取消）
  + `InkingWindow` 主循环每帧喂一次。
  **刻意没做**的：静态烘焙的命中表、三态 `Block / PassThrough / Miss`、
  `isDirty` 帧计数——那是 `docs/InputDesign.md` 的完整方案，接口按那时候的形状
  定了，换实现时调用方不用改。自检 `tests/test_button_gen.cpp` + 示例
  `examples/buttons_demo/`（`INK_AUTOQUIT=1` 下自己模拟一次点击、并**离屏采三态的像素**
  验证透明度真的参与合成）；
- 已有：**绘制的 alpha 混合是打开的**（`InkingWindow::Show` 里
  `SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND)`）。
  不开的话颜色的 alpha 会被原样写进像素，"半透明"根本不存在——
  详见 §6 第 32 条。**所有画出来的颜色现在都按 alpha 合成**，
  `AnchorData::color` / `ButtonAppearance::color` 的 alpha 开始真的有意义；
- 已有：**形状层（SDF）第一档**（`include/ink/dataStruct/InkingShapeSpec.h`）：
  `rect` / `roundedRect` / `circle` / `ellipse`，符号用通用约定（负 = 内），
  并从同一份定义派生三个量——`SignedDistance`（渲染）、`ShapeContains`（命中）、
  `GetShapeBounds`（AABB）；
- 已有：**形状填充（含抗锯齿）**——`include/ink/basic/InkingDraw.h` 的
  `detail::fillShape`，实现 `src/ink/basic/InkingDraw.cpp`，是**唯一**的填充入口
  （原先那个只填包围盒的 `fillShapeBounds` 已删除）。画法是
  **轮廓解析采样 → `SDL_RenderGeometry` 三角化 → 边缘 1 像素羽化带**
  （外圈顶点 alpha=0、内圈 alpha=源色，带子对称跨在边界上，等价于标准
  `alpha = clamp(0.5 - f, 0, 1)`）；**不是 CPU 逐像素**，也没换渲染后端。
  于是"命中按圆角、画出来是直角"这条已知不一致已经消掉：Button
  （`src/button/ButtonPaint.cpp`）与基类默认矩形
  （`InkingAnchor.cpp` 的 `drawDefaultRect` = `ShapeSpec::Rect()`）共用同一份。
  第二档形状（`capsule` / `ring` / `line` / `polygon`）**仍未实现**；
- 已有：`include/ink/dataStruct/InkingColor.h`——颜色统一为 **0xAARRGGBB 整数**，
  并把 CXXCSS 里 `"0|0|0|0.75"`（0~1 浮点）那套换算收在一处。
  那串是**文件格式**，生成器在编译期转换，运行期不解析字符串；
- 已有：**代码生成器 `inkgen`**（`tools/inkgen/`，构建期工具，只依赖标准库）。
  读 `CXXCSS/Button/*.json` → 校验 → **为每个名字生成一个独立的按钮类**
  （颜色 / 尺寸 / 形状 / 锚点作为字面量烘在类里）+ 一个登记入口。
  `NormalButton::Data()` 用函数内静态量（避开静态初始化顺序问题），
  构造按钮**不查任何表**——"配置取不到"这条静默失败路径在类型上不存在。
  产物只落 `build/`；`.example.json` 按约定跳过。
  CMake 侧一个函数搞定：`ink_add_generated(目标 OUT_DIR … SOURCES …)`，
  调用方只要 `#include <inkgen/xxx/button_service.h>` 就能用生成的类。
  `ButtonLibrary` / `RegisterAllButtons()` 保留但**降级**：给"按名字查"的用法
  （自检、工具、将来运行期换皮肤），**不是按钮能用的前提**。
  手写字符串名字那条路仍在，但查不到时写**错误**日志 + 给空按钮 +
  `InkingStaticButton::UnknownNameCount()` 加一（可断言，不静默）。
  自检 `tests/test_inkgen.cpp`（解析 / 映射 / 每一条报错 / 产物内容）+
  `tests/test_button_gen.cpp`（产物的运行期效果）；
  CLI 支持 `--validate` 只校验不写文件（真实配置在 ctest 里也过一遍）；
- **场景不生成**（`TaskGuide.md` T-7 做过一版、评估后撤销）：场景手写，
  5 行左右——
  ```cpp
  class MainScene : public ink::InkingScene {
  public:
      static constexpr const char* kName = "mainScene";
      MainScene() : ink::InkingScene(kName) {}
      ink::cxxcss::NormalButton normalButton{this};       // 成员 + {this}
      ink::cxxcss::DynamicTestButton dynamicTestButton{this};
  };
  ```
  撤销的理由：生成场景的两处收益（组件不传 `parent`、名字有编译期保险）
  手写都能拿到——前者靠"成员 + 默认成员初始化器 `{this}`"这个写法，
  后者靠生成的**按钮类**（类型名拼错即编译错误）与一行 `kName` 常量；
  而代价是固定的：一个界面要看两组文件、表达力受限（不能放非按钮组件 /
  不能覆盖位置 / 不能嵌套 / **不能按条件建组件**）、多一类输入与跨文件校验。
  它真正的价值在将来（`InputDesign.md` §10.4 的按布局生成专用查找、
  或按名字切场景），到那时该重新设计。示例见 `examples/scene_demo/`；
- 已有：**第二个完整组件：动态 Button**（`include/button/InkingDynamicButton.h`，
  实现 `src/button/InkingDynamicButton.cpp`）。它和静态按钮是**同一个组件的两档**，
  判据只有"几何会不会在构造之后自己变"：
  继承 `InkingDynamicAnchor`（几何写入口 `Resize` / `ChangeSelfAnchor` /
  `ChangeTraceAnchor` / `ChangeOffset` 直接来自基类），所以**不进命中表**；
  **输入方式与静态版不同**——静态版由 `DispatchPointer` **推**
  （`MouseHover` / `MousePress` / `MouseRelease`），动态版只有 `JudgePointer()`，
  在自己的 `onTick` 里**读**场景的指针状态（`InkingScene::GetPointerX/Y` /
  `IsPointerDown` / `IsPointerInside`），自己判命中、自己推三态、自己触发点击
  （`DispatchPointer` 本来就跳过动态节点，见 `InputDesign.md` §2 / §7）。
  静态 / 动态**共用两份实现**，不许各写一遍：`include/button/ButtonLook.h`
  （三态外观 + 状态机 + `ButtonState` + `ButtonClickCallback`）与
  `src/button/ButtonPaint.cpp`（形状填充 + 文字占位块）。
  两档的类型约束都由 `src/ink.cpp` 的编译期断言钉住（静态档**不得**有几何写入口、
  动态档**必须**有）；
- 已有：**三态之间的过渡（transition）：颜色 + 变换**——配置里给状态写
  `"transition": { "duration": 0.15 }`（生成期校验：只认 `duration`、必须 > 0）
  与可选的 `"transform": { translateX/Y, rotate, scale }`，烘进
  `ButtonAppearance`。运行期由 `ButtonLook` 起一段过渡、`Advance(delta)` 把
  **颜色与变换按同一段时长一起插值**（`ink::MixColor` + `ink::LerpTransform`）。
  驱动通道是**每渲染帧**：`InkingAnchor::TickAnimation` →
  `InkingScene::TickFrame` 分发给绘制列表里的**所有**节点（静态也要；
  动态档那条 `Tick` 是固定逻辑步，两回事）。缓动曲线**还没做**，一律 linear，
  配置里也**没有** `easing` 字段（不给半成品开关）；
- 已有：**命中表 + 三态（T-4）**。`include/ink/ui/HitTable.h`（实现
  `src/ink/ui/HitTable.cpp`）：**簇（粗块 256px）→ 簇内均匀格子（cellSize 取该簇
  条目平均短边，clamp [16,128]）→ CSR 候选**，候选按全局 z 降序，格子只缩小范围、
  命中由节点 `HitTest` 精判。建表正比于 UI 数量（空白区没有簇）。
  三态是 `HitKind{Block, PassThrough, Miss}` + `HitResult{kind, target}`；
  "让不让过"是策略，放基类（`InkingAnchor::SetHitPassThrough`），
  "在不在形状里"才是组件知识（`HitTest`）。
  查询 `InkingScene::QueryHit(x, y)`：查表 → 点在**动态洞**里时再问动态层 →
  **按全局 z 序合并**（洞只是性能开关，漏登也只是多问一次）。
  表与 `_drawList` **同生同灭**（`sortDrawList()` 真的重建时顺手重建表）；
  **变换不触发重烘**——组件上报的是"所有可能变换"的**保守包络**
  （`InkingAnchor::GetHitEnvelope`，按钮按三态并集算），精判用当前变换。
  **没有**溢出桶与跨簇仲裁（理由写在 `InkingScene.h` 那一节：前者被"构造即登记"
  消掉、后者要等局部层级），§10.4 的"按布局生成专用查找"也还没做；
- 已有：**标脏分三种 + 帧末统一消费（T-3）**。三种标记各有各的消费点，
  **别合并**（合并的后果就是"动画每帧改颜色 → 每帧白重建一次绘制列表"）：

  | 标记 | 触发源 | 消费点 | 动作 |
  | --- | --- | --- | --- |
  | `_dirty`（命中脏） | 几何 / 可见性 / 层级 / 父子 | **帧首** | `sortDrawList()` 重建列表 + 重算坐标快照（将来：重烘命中表） |
  | `_repaintDirty`（重绘脏） | 颜色 / 文字 / 变换 / 几何 | **帧末** | 记账 + 连续 `kRepaintQuietFrames`(5) 帧无新脏即清（将来：重算顶点） |
  | `_pointerDirty`（指针脏，场景级） | **指针移动** | **帧末** | 记账（T-4：每帧 query 悬停的开关） |

  写入口统一走 `MarkStructureChanged()`（命中脏 + 重绘脏 + 通知场景重排）；
  帧末由 `InkingScene::ConsumeFrameDirty()` 收口（**只对活跃场景**，
  挂在 `InkingWindow::runLoop` 的收尾那一步，紧挨 `mouse.ResetFrameFlags()`）。
  动画那边：`ButtonLook::Advance` 返回 `AdvanceResult{repaint, hit}`，
  颜色变只标重绘脏，**变换变还要标命中脏**（同一个点可能落到别的组件上），
  但两者都不碰结构。**顶点缓存 / 合批仍未落地**（消费目前只是记账 + 清脏）；
- 已有：**变换通道**（`include/ink/dataStruct/InkingTransform.h`）——
  平移 / 旋转 / 缩放的**正变换**（`ApplyTransform`，绘制用，在 `fillShape` 里过顶点）
  与**逆变换**（`InverseTransform`，命中用，在 `detail::HitTestButton` 里过点）。
  它属于 `InputDesign.md` §11 的**变换通道**：**本地几何与命中表都不动**，
  所以**静态组件也能移动 / 旋转，而且仍然进命中表**（这是 D-1 措辞从
  "绝不碰几何"精确成"不碰**本地**几何"的根据）。原点取组件中心，
  `rotate` 单位是度。会改表的**布局通道**（尺寸 / 会重排的定位）**仍未实现**；
- 已有：**生成器按 json 的 `"dynamic"` 选基类**（`tools/inkgen/`）：不写 / `false` →
  `ink::InkingStaticButton`，`true` → `ink::InkingDynamicButton`。它是**生成期**开关，
  不进运行期的 `ButtonData`（类型已经说明了档位）。参考配置
  `CXXCSS/Button/dynamicTestButton.json` → `ink::cxxcss::DynamicTestButton`；
- **还没做**：`img` / `svg` 两种外观的资源落地（图片 = 生成器给数据 +
  运行期用 SDL3_image 解码纹理；SVG 要先定支持子集）、
  **几何动画**（颜色过渡已经有了，缺的是"每帧把几何插值到哪儿"的配置字段与驱动；
  `Resize` 那类写入口本身能用）、**缓动曲线**（现在一律 linear），
  以及 `CXXCSS.md` §6 第 3 条"按布局生成专用查找"；
- 已有：**场景构造期建出来的子节点会被补登记**。场景是"构造完成才盖章成根"的，
  所以 `InkingScene` 构造函数体里建的按钮沿父链找不到场景；`finishConstruction()`
  里会回头把 `_scene == nullptr` 的节点补登记一遍（`rebindEarlyChildren`）。
  不补这一趟的后果是"按钮既不渲染也点不到，而且一声不响"；
- 未实现：场景与 widget 的树形子表（「整块在动」那档要的变换通道）、
  **第二档形状**（`capsule` / `ring` / `line` / `polygon`）、
  文字渲染（字形图集 / 纹理层）、
  `img` / `svg` 两种外观、样式/渲染层；
  **合批尚未落地**，目前是一节点一次 `SDL_RenderGeometry`
  （形状那一笔；文字占位块还是 `SDL_RenderFillRect`）。

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
12. **注册表别用类的静态数据成员，用函数内静态量**：类的静态数据成员在
    main 之前按翻译单元顺序初始化，而静态场景对象可能先构造——轻则
    「场景活了库还没起来」，重则「场景在库已经析构之后才注销」。
    `SceneLibrary` 的 `scenes()` 是函数内静态量，首次使用才初始化，
    谁先登记就把库先拉起来，库一定活得比登记者久。照抄后端
    `ShineStatusChecker` 的静态成员写法会带进这个坑。
13. **`std::optional<RegisterToken>` 当成员不会因「类型不完整」报错**：
    `RegisterToken<InkingScene, SceneLibrary>` 的模板实参在类体内还是
    不完整类型，靠的是「类模板的成员函数体延迟实例化」——只要不在类体里
    定义成员函数或静态成员，就合法。想调 `RegisterToken` 的方法（例如
    `IsRegistered()`）必须挪到 `.cpp` 里定义。
14. **include 写「相对 include/ 根」的路径，别自己加前缀**：现有两种前缀并存
    （`<ink/basic/InkingAnchor.h>` 对 `<scene/...>` 只写一段），因为
    `include/` 本身就是搜索根，前缀写到与真实目录一致就行，多写一段就找不到。
    `include/scene/InkingScene.h` 曾经写成 `"ink/scene/SceneLibrary.h"`——
    文件实际在 `include/scene/` 下，于是成了永远编译不过的死文件；又因为它
    不在任何 target 的源文件列表里、没人 include，构建照样全绿，问题藏了很久。
    **新增公开头后，至少让它被某个 .cpp 或自检 include 一次**，别只靠
    「构建绿」当验证。
15. **空转的令牌也算「有令牌」**：`RegisterToken` 在名字为空或对象为空时
    不登记，但对象仍然构造出来了。所以 `InkingScene::IsRegistered()` 不能
    只判 `_sceneRegisterToken.has_value()`，得同时问令牌自己——这个 bug
    被自检第 8 段抓到过（空名字的场景自称已登记）。
16. **绘制提交是 z 从小到大，和 `IsAbove` 的答案相反**：`IsAbove` 回答
    「谁在上面」（命中要从高到低问），而画家算法要求**先画底下的**，
    所以绘制列表按 z 升序排。两者规则必须一致（z 优先、z 相同看注册序号），
    只是方向相反。第一版按降序排，结果铺满整屏的背景板（z 最低）排在最后画，
    把画面全盖了——**画面上只剩背景色**，而所有断言都还是绿的，因为列表内容
    本身没错。这类"顺序对但方向错"的 bug，只有真像素能钉死。
17. **zindex 必须有下限**（`InkingZIndex::background` = -128）：绘制按 z 升序做，
    想待在一切之下的背景板只要有"比所有组件都低"的 z 就够；允许任意低的话，
    两块都在下限之下时先后顺序就退化成注册序号，"谁盖谁"变得不可预期。
    低于下限的值一律夹到下限。
18. **可见性不能靠"从绘制列表里删掉"来实现**：删除是单向的——只有节点自己的
    构造函数会把它加回列表，而构造只跑一次，于是"隐藏过的节点永远回不来"。
    正确做法是**列表保留、提交时跳过**（`IsVisibleInTree` 顺带短路整棵子树）。
    这个 bug 被自检第 9 段抓到。
19. **动态节点的几何写入口也要通知场景**：绘制列表里存着每个节点的绝对坐标快照。
    动态节点自己每帧现算没问题，但它的**静态子孙**用的就是那份快照，
    不通知就会"父级动了、子级坐标停在旧值"。
20. **能变成数值判据的渲染验证别靠截图**：开窗截图会被别的窗口遮挡，结果随桌面状态
    漂移（实测折腾了很久都只能捕到别人家的窗口）。用
    `tests/render_probe.cpp` 那条路：渲染到**离屏**渲染目标 + `SDL_RenderReadPixels`
    读回像素逐个采样点比对——它走的是同一条提交路径，但结果完全由自己掌控。
    注意 SDL3 的 `SDL_RenderReadPixels` **返回一张 surface**（不是往调用方缓冲写），
    而且读回时渲染目标必须仍然指向那块离屏纹理。
    至于**没法变成数值**的观感问题（字体、颜色好不好看、间距别扭不别扭），
    不要试图截图，按 §4 第 4 条**停下来交给用户看**。
21. **别用 PowerShell 读写源码文件**：`Set-Content` / `-replace | Set-Content`
    会按控制台编码回写，中文注释直接变成乱码、文件不再是合法 UTF-8
    （实测把 `InkingAnchor.cpp` 和 `render_probe.cpp` 各写坏一次，前者靠
    git checkout 救回、后者是新文件只能整个重写）。改源码一律用编辑器工具，
    别图省事走 shell 管道。
    **第五次是 Agent 自己踩的**（T-2 的变换扩展那轮）：起因只是想批量改一个
    常量名（`kPi` → `kTransformPi`），一句 `-replace | Set-Content` 就把
    `InkingTransform.h` 的中文注释全写成了乱码。教训有两条，都要记住：
    - **"只改一个名字"也不例外**——只要文件里有非 ASCII 字符，这条路就是雷。
      要改名就用编辑器工具逐处 `edit`，或者干脆重写整个文件；
    - **新文件被写坏之后恢复会有个死循环**：文件不是合法 UTF-8 时 `read` 直接
      拒绝（`invalid UTF-8 text`），而 `write` 要求"先读过"。解法是
      `Remove-Item` 掉它（或 `New-Item` 建个 0 字节空文件）→ `read` 一次 →
      再用 `write` 写回完整内容。**别试着重试 write，它只会一直要你 read。**
22. **被驳回的登记令牌绝不能去注销**：库是"同名只能有一个"的，注册会失败。
    如果令牌不记住"我到底进没进库"，析构时照样去 `UnregisterScene`，
    就会**把正主从库里摘掉**——后来者一析构，正在跑的那个场景就凭空消失了。
    `RegisterToken` 因此有 `_active` 标志，只有 `Ok` 才为真；
    `RegisterToken::IsRegistered()`、`InkingScene::IsRegistered()` 问的都是它。
    自检里"驳回者的析构不能影响正主"这条就是钉这个的。
23. **场景关闭 ≠ 销毁**：`SceneLibrary::CloseScene(名字)` 只摘登记、把场景标记为
    已关闭，**不 delete**——生命周期始终归调用方。所以关掉之后对象还能用、
    数据还能读（转场时"先 move 数据出来再关"靠的就是这条）。
    库那边同时会把活跃指针清成 nullptr，避免留下指向已关闭场景的野指针。
24. **"追不上就丢掉"的限制必须写在累加器循环的入口**：第一版把判断写在循环
    **后面**，而循环一定会把累加器排干（`while (acc >= step) acc -= step`），
    所以那个限制永远触发不了——5 秒的 delta 会老老实实补 250 步。
    写在入口才发现它真的有用。这类"守卫写在永远不会满足的位置"的错，
    光看代码很难发现，是自检里"5 秒的 delta 只补出截断后那点步数"这条钉出来的。
25. **别按"帧数"算时间**：逻辑 50Hz、显示器 60/120/144Hz、动画还可能是 24fps，
    三者互不整除。按帧数数就必然漂移，只能按真实流逝时间累加。
    这也是为什么时间线的逻辑步回调收到的是**固定 interval**、
    而自定义订阅收到的是**自己的间隔**——都是"确定的值"，
    真实 delta 只喂给每帧通道（插值用）。
26. **`sleep_for(16ms)` 不是 60fps**：它不扣本帧耗时，实际是 16ms + 工作时间，
    帧率会低于 60 且抖动。接上 `SDL_SetRenderVSync(renderer, 1)` 之后由
    `SDL_RenderPresent` 阻塞到垂直回扫，才真的锁在显示器刷新率上；
    无头 / 无显示环境下 vsync 可能接不上，那时再退回 sleep。
    实测开窗时日志会打"vsync 已接上"，无头冒烟里则不一定。
27. **`find_package` 的结果会进 cache，下次配置直接复用**：给 SDL3_image 写了
    四选一来源策略后，"先跑通系统包、再改成 local 指向一个没有 image 的目录"
    照样报"using local SDL3_image"——其实是捡走了上一次缓存的
    `SDL3_image_DIR`。**每条策略在探测前必须先清 `<Pkg>_DIR` 之类的 cache**，
    否则来源开关形同虚设。同理，探测结果本身要用**普通 CACHE**而不是
    `CACHE INTERNAL`（INTERNAL 隐含 FORCE，会把值永久钉死）。
28. **光靠 `CMAKE_PREFIX_PATH` 没法把来源限制成"只用本地"**：MSYS2 的工具链
    前缀（`C:/msys64/ucrt64`）本来就在 CMake 的隐式搜索路径里，prepend 一个目录
    只能提高优先级、**排除不掉**系统那份。实测
    `-DINK_SDL3_IMAGE_SOURCE=local -DINK_SDL3_IMAGE_LOCAL_DIR=D:/nonexistent-dir`
    照样"找到"了系统包。所以 local 分支要**自己算出配置目录**
    （`lib/cmake/<Pkg>/<Pkg>Config.cmake`，含三元组布局），指定给 `<Pkg>_DIR`
    并配 `NO_DEFAULT_PATH`，不走搜索。而 system 分支**不要**加
    `NO_DEFAULT_PATH`——工具链前缀正是"系统包"的含义。
29. **`enable_testing()` 必须在任何 `add_subdirectory` 之前调**：它只对**调用之后
    被处理到的目录**生效。写在文件末尾（挨着那些 `add_test` 看着挺顺）会让子目录
    里的 `add_test` **静默失效**：测试照样编出来、手动跑也全过，就是 `ctest` 列表
    里没有——`examples/buttons_demo` 的两个集成自检就这么漏了一轮（ctest 只报 7 个，
    实际有 9 个）。判断依据很简单：`build/<预设>/<子目录>/CTestTestfile.cmake`
    不存在，就说明那个子目录的测试没注册上。
30. **生成器里"名字"有两份，别只填一份**：`ButtonConfig::name`（生成器自己用）和
    `ButtonData::name`（运行期 `ButtonLibrary` 的查找键）是两个字段。只填前者的
    后果是运行期按名字查不到配置——而且库那边只会说"没登记过"，看起来像登记代码
    的锅。同类坑还有一个：**别用文件名给 name 预填默认值**，否则"配置里压根没写
    name"会被悄悄抹平（两个值恰好相等），只能靠"报错和文件名不一致"这种绕圈子的话
    提示用户。缺字段就要报缺字段。
31. **一份一份校验完再合并，会让"跨文件"的检查永远看不见对方**：第一版的
    `LoadButtonConfigs` 是"每个文件各加载成一个 `LoadResult`，再逐个合并"。
    重名检查查的是"这一批到目前为止谁认领了哪些名字"，而每份文件只看得见自己
    那份结果，于是两个文件写同一个 name 时报的是"与文件名不一致"，
    重名永远检不出来。改成**往同一个 result 上追加**（`LoadButtonConfigInto`）
    才对。教训：跨条目 / 跨文件的规则，必须在**共同的累积视图**上做。
32. **SDL 的绘制混合模式默认是关的，颜色的 alpha 会被原样写进像素**：
    `SDL_GetRenderDrawBlendMode` 默认 `SDL_BLENDMODE_NONE`（这是**绘制**混合，
    和 `SDL_SetTextureBlendMode` 是两套东西）。后果：Button hover 把 0.75 改成
    0.85，`onRender` 也确实选了 hover 那份外观，但画出来只是把底色整块换成另一个
    不透明的黑——屏幕上一模一样，而状态机、颜色、断言全是绿的。
    修法是在拿到渲染器之后一次性 `SDL_SetRenderDrawBlendMode(renderer,
    SDL_BLENDMODE_BLEND)`（`InkingWindow::Show` 里），组件不要各设一次。
    实测：`0xBF000000` 叠在 `0x1A1A22` 上，混合关着读出 `0xBF000000`、
    打开后读出 `0xFF060608`。
    **顺带一个能把人骗过去的测试陷阱**：离屏断言如果"清成透明黑再画"，
    混合到 `alpha=0` 的目标上时结果**恰好等于原样写入**（rgb 都被乘成 0），
    于是**混合开没开都读出同一个值**，测试全绿却什么都没验到。
    离屏采样的底色必须是**不透明**的，否则这条断言等于没写。
33. **离屏采样要先满足和窗口一样的三个前提**：逻辑分辨率（`SDL_SetRenderLogicalPresentation`）、
    绘制混合模式、以及**场景得是活跃的**（`SceneLibrary::RenderScene` 只画活跃场景）。
    漏一个的表现都是同一个——"采到的永远是纯底色，三态看起来一模一样"，
    而错误信息会把人往"状态机坏了 / 配置写错了"上面引。
    另外离屏目标要**给足设计尺寸**：目标比设计尺寸小的话，按钮会被裁在视口外。
34. **别把配置的数值抄进断言**：`test_button_gen.cpp` 里原本写着"三态 alpha 是
    191 / 217 / 191"，那是把手抄的配置又抄了一遍——改 json 就红，而那种红没有
    信息量（配置本来就该能随便调）。改成验**关系**：三态互不相同、
    悬停比通常更不透明、通常既不是全透明也不是不透明。
    同一类问题在 `tests/test_button.cpp` 的 `exampleButton()` 上还在（它是手写的一份
    配置，与 `CXXCSS/Button/*.json` 各写各的）——那是有意的：那份用来测**组件**，
    要的是值稳定，所以它不跟着 json 走。分清楚"测组件"还是"测配置"再决定抄不抄。
35. **（本机环境，不是项目代码）受限会话里 `build/` 与大部分源码目录可能不可写**：
    症状是 `cmake` 报 `Cannot open file for write: …/CMakeCache.txt` /
    `System Error: Permission denied`，而**同一个路径用文件工具（write / edit）却写得进去**。
    原因是沙箱进程跑在受限令牌下，而工作区里只有一部分目录带上了那条写授权
    （实测 `CXXCSS/`、`tools/` 与工作区根可写，`build/`、`include/`、`src/`、
    `tests/`、`docs/`、`examples/` 不可写）。
    处理顺序：先看这次会话的文件策略；策略是 full access 时直接就好。
    仍然受限时，用 ACL 诊断技能那一条命令去查（**先 `Remove-Item Env:LIB`**，
    见 UsageReport §9 那条环境注记，否则脚本会在 `Add-Type` 那一步就失败），
    但别指望它一定能修：这台机器上 `build/` 的所有者是另一个沙箱账号，
    当前用户没有改权限的资格，脚本会报 `REPAIR_REFUSED`（零改动，
    不需要回滚）——那时要请用户把会话切成「完全权限」。
    **别用 `Set-Content` 往源码树写东西**（第 21 条），这条不因为权限问题而放宽。
36. **`SDL_RenderGeometry` 的逐顶点 alpha 也吃"绘制混合模式"，而且 software
    后端的采样点不在像素中心**：形状填充（抗锯齿）就建在这两件事上，
    两条都是实测出来的。
    - 顶点 alpha **确实**参与混合、而且会插值（实测：alpha=0.5 的蓝叠在不透明红上
      读出 `0xFF7F0080`；同一三角形里两个顶点 alpha 不同时中间是渐变的）。
      但混合关掉时 alpha 会被**原样写进像素**（读出 `0x80FFFFFF`），
      抗锯齿就退化成"半个像素被涂成不透明色"——和第 32 条同一个开关，
      别以为几何绘制不受它管。
    - 离屏自检跑的是 **software** 渲染器，它的像素采样点落在**右下角**
      （`(x+1, y+1)`），而 GPU 后端用像素中心（标准约定）。后果：
      "边界正好穿过像素"的那一圈会读出 **50% 的混合色**——实测一个边界对齐的
      矩形，边界内外的像素都可能是 `0xFF8C8C90` 而不是纯实心 / 纯底色。
      **所以别把"最外一圈像素必须精确等于实心或底色"写进断言**（那是后端约定，
      不是形状的契约）；要断言就断言"离边界 1 像素以上该实就实、该透就透"，
      以及"边缘存在亮度介于两者之间的过渡像素"。
    - 内圈必须按形状**法向**内缩（等价于"尺寸各减 2×半宽、半径减半宽、中心不变"），
      否则圆角处会露细缝。羽化带要**对称跨在边界上**（±0.5 像素，等价于
      标准 `alpha = clamp(0.5 - f, 0, 1)`）：整条压在边界内侧会让像素对齐的
      矩形（面板 / 背景板 / 分割线）最外一列像素集体变半透明。
37. **"动画不动"的第一个怀疑对象是"没人推进它"**：颜色过渡（`ButtonLook::Advance`）
    **不会自己跑**，它靠每渲染帧一条通道：`InkingScene::TickFrame` →
    节点 `TickAnimation`。所以：
    - 静态组件**收不到** `InkingDynamicAnchor::Tick`（那是动态档的逻辑步）——
      别指望写个 `onTick` 就能让静态按钮的颜色动起来；
    - `TickFrame` **只在场景活跃时**跑，而且**静默 / 停放 / 关闭一律不跑**。
      于是"切走再切回来，过渡停在原地继续走"是**预期**行为，不是卡住；
    - **离屏自检里只调 `SceneLibrary::RenderScene` 是不会推进过渡的**：
      渲染只画，不推时间。要采"过渡到位"的颜色，得先显式调
      `scene.TickFrame(时长)`（`examples/buttons_demo` 的自检就是这么做的：
      它先推 0.075 采中间色、再推 0.2 采终态）。
    这类"通道没接上 → 静默停在起点"的失败，和标脏、日志那些坑同源：
    **能断言的都要断言**，别靠"看着像在动"。
38. **变换的"正 / 逆"必须严格互逆，而验证它的唯一办法是"像素对账"**：
    变换通道的契约是：绘制过**正**变换（`fillShape` 里的 `ApplyTransform`）、
    命中过**逆**变换（`HitTestButton` 里的 `InverseTransform`），两者绕**同一个中心**、
    顺序互为反。写反一个符号、或者漏掉平移那一步，就会出现"画的是一个、点的是另一个"——
    而两边**单独看都对**，只有把像素和命中摆在一起才看得见。
    - 验证方式别手算几何（容易算错，而且只覆盖你想到的角度），要**交叉对账**：
      渲染到离屏，扫一片区域，对每个点同时问"这里有像素吗"和"这里命中吗"，
      两者必须同答案。实测：旋转 90° 查 18167 个点、旋转 45° 查 31949 个点，
      **不一致 0 个**（`tests/test_button.cpp`）。
    - **坑一：抗锯齿边缘会把判定搅浑**。边界那一圈的像素是半透明的，
      "有像素"的判定（`!= 底色`）会把它们算成有，而命中在边界上的归属另有说法。
      解法：只统计**周围 3×3 同色**的点，把边界整圈排除掉。
    - **坑二：扫描区里别混进别的节点**。第一次跑出 2468 个不一致，查下来不是变换
      错了，而是场景里**另一个按钮正好压在同一个位置**（旧按钮没藏起来），
      转走的那个按钮露出来的像素全是它的。写这类扫描前先把无关节点
      `SetVisible(false)`，否则你会去修一个根本不存在的 bug。
39. **标脏只写在"每帧推进"里，会漏掉"当场生效"的那条路**：这是 T-3 接动画标脏时
    真实写出来的漏洞。`ButtonLook::Advance` 里标脏（重绘 / 命中各一份），但
    **没配 `transition` 的三态切换**是"显示值当场就换掉"的——它根本不经过
    `Advance`，于是**画面变了却没人标脏**。
    - **危险的地方在于它当前看不出问题**：渲染每帧全量重画，画面照样是对的。
      等将来做了顶点缓存，"这个节点还动不动"的判断就会拿着旧顶点不撒手，
      而那时的现象是"某个按钮偶尔停在旧样子"，极难往"标脏漏了一条路"上想。
    - 修法：那种"当场换掉"先记一笔待报告（`_pendingRepaint` / `_pendingHit`），
      下一次 `Advance` 报出去。**这笔账必须在 `duration <= 0` 的提前返回之前结清**，
      否则它永远报不出来（那种情况的 `duration` 恰好就是 0）。
    - 通用教训：**标脏的触发点要覆盖"所有让显示值变化的路径"**，
      而不只是"每帧推进"那一条。写新的动画通道时先问一句：
      "有没有哪条路是当场把值改掉的？"
40. **三种脏合并的代价，只有计数能发现**：把"重绘脏"和"命中脏"合成一个 bool 时，
    功能**全对**（画面对、命中对、三态对），只是每次动画改颜色都会顺手重建一次
    绘制列表——排序 + 重算所有静态节点的坐标快照，白干。这类"结果对、过程白干"
    的错，断言值永远抓不到，只能钉**计数**：`ink_button_test` 里那条
    "整段动画下来，结构 generation 一次都没被标过"（`GetStructureGeneration()`）
    就是干这个的。写性能相关的机制时，**给它留一个能断言的计数**。
41. **观测口不推脏，就会报出"还没建"的假象**：`GetHitTableEntryCount()` 最初写成
    `const noexcept` 直接读成员——表还没建（没人调过 `QueryHit` / `Render`）时
    它老老实实返回 **0**。自检第一条就红了，而**表本身没错**：错的是"读运行时缓存
    却不先保证缓存是新的"。修法是让它和 `BuildDrawList()` 一样先推一次
    `sortDrawList()`（代价是去掉 `const`）。
    通用教训：**凡是读"延迟构建的缓存"的观测口，要么先推脏，要么在函数名或注释里
    写死"它不推脏"**——后者几乎一定会被下一个人（或自己）误用。
42. **空间索引的包络必须显式外扩，别指望两条浮点路径在边界上对齐**：
    命中表用"包络"粗筛（四个角过一遍变换再取 min/max），精判走 `ShapeContains`，
    两条路径的浮点误差在 1e-5 量级——但**恰好落在边界上的点**会因此出现
    "形状里算命中、包络里算在外"，于是被索引漏掉。实测：逐点对账的 24978 个点里
    **漏了 26 个**（就是这条对账把它抓出来的，不然会变成"偶尔点不中"的幽灵 bug）。
    修法：包络统一外扩 `kEnvelopePadding = 0.5f`（半像素），代价只是候选偶尔多一两个。
    教训：**索引里的"保守"必须落成一个显式余量**，不能靠"两边算法应该一致"。
    顺带一条测试上的：`IsAbove` 在 **z 相同时比注册序号**（后构造的在上），
    所以写"谁压在谁上面"的测试时要**显式给 z**，否则测的其实是构造顺序。
43. **手写 `ButtonData` 必须显式关掉"继承 normal"**：`hoverInheritsNormal` /
    `onclickedInheritsNormal` 的默认值是 **true**（"省略即继承"），而
    **构造按钮时会调一次 `Normalize()`**——忘了置 `false` 的话，
    hover / onclicked 会被 normal **整个覆盖**：
    - 症状极具迷惑性：**状态机照常工作**，`GetState()` 照常说 `Hover`，
      写入口也照常返回 true，但**外观一个字节都没变**——表现是"动画一动不动"，
      而你会去怀疑插值、驱动通道、场景活跃……实测就是这么绕了一圈；
    - 定位它的办法（这次管用）：把链路切成三段逐级验——
      ①组件在不在渲染树里 ②`MouseHover` 有没有报告三态变了
      ③**绕过分发直接调 `TickAnimation`**。第三段一排掉，问题就锁定在"配置"上了；
    - 生成器生成的代码不受影响（它显式写了这两个标志），
      **手写配置的人**要特别小心——`UsageReport.md` 的排错表里也记了一条。
    教训：**"默认继承"这种贴心设计，在有一条路径不会替你写默认值时会变成陷阱**；
    至少要让"配置被吞掉"这件事有个可观测的出口（这里其实有：`Normalize()`
    覆盖后三态颜色会变得一样——但那需要专门断言"三态互不相同"才看得出来）。
