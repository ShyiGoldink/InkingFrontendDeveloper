# 使用报告 —— Button 从 CXXCSS 配置到可交互窗口

> 这份是**使用说明 + 实测结果**，写给"现在就要用它做一个按钮"的人。
> 它记录的是**当前真实可用的边界**（哪些能用、哪些还是占位），
> 所有"实测"字样都是我在这台机器上跑出来的值，不是推断。
>
> 想知道"为什么这么设计"，看 `docs/DevelopLog.md`；想知道"接下来做什么"，
> 看 `docs/TempTask.md`；想改框架本身，先读 `docs/AGENTS.md` 的约定与坑。

---

## 1. 一句话结论

**能用**：写一个 json 描述按钮 → 构建期生成 C++ → 代码里按名字构造 → 鼠标点击真的回调。
**还不能用**：圆角的**填充**（命中是圆的、画出来是直角）、真文字、`img` / `svg` 外观、
场景的 json 配置（场景名仍是手写常量）。

---

## 2. 现在能用什么（逐项，实测）

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 从 json 生成按钮配置 | ✅ | 构建期自动跑，配置写错**构建就红** |
| 按名字构造按钮 | ✅ | `InkingStaticButton(parent, "normalButton")` |
| 生成名字常量 | ✅ | `ink::cxxcss::kNormalButtonName`，拼错编译期就红 |
| 三态（通常 / 悬停 / 按下） | ✅ | 只换颜色；状态机 + 指针派发都已接通 |
| 三态颜色真的看得见 | ✅ | 实测通常 `0xFF646468` vs 悬停 `0xFFDBDBDC`（悬殊一倍多） |
| 点击回调 | ✅ | 抬起时指针还在按钮上才算；滑出去松开算取消 |
| 形状命中 | ✅ | `rect` / `roundedRect` / `circle` / `ellipse`，与渲染同一份定义 |
| 形状**填充** | ⚠️ | 填的是包围盒那个**直角**矩形（圆角还没画出来） |
| 锚点与偏移 | ✅ | 百分比锚点 + 设计坐标偏移 |
| 层级 / 可见性 | ✅ | `zIndex`、`visible`（json 里可配） |
| 文字 | ⚠️ | 字段已通，但画的是**同尺寸占位方块**（没有字形图集） |
| `img` / `svg` 外观 | ⚠️ | 生成器收下并提醒，运行期**不画** |
| 多语言文字 | ✅ | `SetText()` 运行期改，不标脏 |
| Scene 的 json 配置 | ❌ | 场景名与尺寸仍是代码 / `sceneRootAnchorData()` |

自检规模（`ctest`，全部通过）：`inkgen_test`、`inkgen_validate_cxxcss`、`ink_test`、
`ink_render_probe`、`ink_button_test`、`ink_image_test`、`ink_ttf_test`、
`ink_button_gen_test`、`ink_generated_test` —— **9 / 9**。

---

## 3. 五分钟上手

### 3.1 写配置

`CXXCSS/Button/normalButton.json`（文件名必须 = `name` 字段）：

```jsonc
{
    "$comment": ["注释字段，生成器跳过"],
    "name": "normalButton",
    "width": 200,          // 设计坐标（1920×1080 的画布）
    "height": 100,
    "normal":    { "type": "color", "source": "1|1|1|0.35" },   // r|g|b|a，0~1
    "hover":     { "type": "color", "source": "1|1|1|0.85" },
    "onclicked": { "type": "color", "source": "1|1|1|0.35" },
    "selfAnchorX": 0.5, "selfAnchorY": 0.5,     // 自身锚点
    "traceAnchorX": 0.5, "traceAnchorY": 0.5,   // 上级锚点
    "shape": { "type": "roundedRect", "radius": 10 }
}
```

### 3.2 接进 CMake（一行）

```cmake
file(GLOB MY_CONFIGS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/CXXCSS/Button/*.json")

add_executable(my_app main.cpp)
target_link_libraries(my_app PRIVATE ink::core)
ink_add_generated(my_app
    OUT_DIR   "${CMAKE_BINARY_DIR}"              # 加进 include 路径的根
    NAMESPACE "ink::cxxcss"                      # 生成代码的命名空间
    HEADER    "inkgen/my_app/button_service.h"   # 产物相对 OUT_DIR 的路径
    SOURCES   ${MY_CONFIGS})
```

### 3.3 写代码

```cpp
#include <ink/ink.h>
#include <button/InkingStaticButton.h>
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>
#include <inkgen/my_app/button_service.h>   // ← 生成的

inline constexpr const char* kSceneName = "MyScene";

class MyScene : public ink::InkingScene {
public:
    MyScene() : ink::InkingScene(kSceneName),
                _button(asParent(), ink::cxxcss::kNormalButtonName) {
        _button.SetOnClicked([] { INK_LOG_PASS("App", "点到了"); });
    }
private:
    ink::InkingAnchor* asParent() noexcept {
        return static_cast<ink::InkingAnchor*>(this);
    }
    ink::InkingStaticButton _button;
};

int main() {
    MyScene scene;                                     // 必须先于 Show
    ink::InkingWindow::Instance().Show(kSceneName);    // 阻塞到窗口关闭
}
```

**两个必须记住的点**：

1. **按钮要显式给父级**（`asParent()`）。组件得知道自己属于哪个场景才能登记进渲染树，
   不登记就既不渲染也点不到，而且**不报错**。伪代码里那种"只给名字"的写法要等
   生成器连场景类一起生成时才能省掉。
2. **场景必须在 `Show()` 之前构造**。`Show(名字)` 是按名字去 `SceneLibrary` 里找的。

按钮写成场景成员是**可以的**：场景在构造末尾会回头把"在盖章成根之前就建出来的子节点"
补登记一遍（否则它们会静默地不在渲染树里）。

### 3.4 跑

```sh
cmake --preset test-debug && cmake --build --preset test-debug
build/test-debug/bin/buttons_demo.exe                  # 开窗，鼠标点它
$env:INK_AUTOQUIT='1'; build/test-debug/bin/buttons_demo.exe   # 自动退（冒烟）
```

---

## 4. 配置字段速查

字段名与 `CXXCSS.md` 一一对应。**未知字段一律报错**（不是静默忽略）。

### 4.1 顶层

| 字段 | 必填 | 类型 | 说明 |
| --- | --- | --- | --- |
| `$comment` | 否 | 字符串或数组 | 注释，生成器跳过 |
| `name` | **是** | 字符串 | 必须**等于文件名**（去掉 `.json`）；是合法 C++ 标识符时才会生成 `k<名字>Name` |
| `width` / `height` | **是** | 数字 | 设计坐标，必须 > 0。历史写法允许数字字符串，新配置请用数字 |
| `normal` | **是** | 对象 | 通常状态外观 |
| `hover` / `onclicked` | 否 | 对象 | **省略即继承 `normal`** |
| `text` | 否 | 对象 | 见 4.3 |
| `selfAnchorX/Y` | 否 | 数字 | 自身锚点，默认 0；建议留在 `[0,1]`（越界会警告） |
| `traceAnchorX/Y` | 否 | 数字 | 上级锚点，默认 0 |
| `offsetX/Y` | 否 | 数字 | 对齐之后再挪一点，设计坐标，默认 0 |
| `zIndex` | 否 | 整数 | 越大越靠上，默认 0；下限 `-128`（自动夹） |
| `visible` | 否 | 布尔 | 默认 `true` |
| `shape` | 否 | 对象 | 省略 = 直角矩形，见 4.2 |

### 4.2 外观与形状

```jsonc
"normal": { "type": "color", "source": "r|g|b|a" }   // 0~1 浮点，可只给 3 段（a=1）
"normal": { "type": "img",   "source": "D:/assets/btn.png" }  // ⚠️ 还没落地
"normal": { "type": "svg",   "source": "<svg …/>" }            // ⚠️ 还没落地

"shape": { "type": "rect" }                       // 直角矩形（默认）
"shape": { "type": "roundedRect", "radius": 10 }  // 圆角；radius 会被夹到 min(w,h)/2
"shape": { "type": "circle" }                     // 居中、直径取 min(w,h)
"shape": { "type": "ellipse" }                    // 用满 w×h
```

- `radius` 为负 → **报错**；超过 `min(w,h)/2` → 自动夹（那是形状的数学要求，不算配置错误）。
- 第二 / 第三档形状（`capsule` / `ring` / `line` / `polygon` / `arc` / `star` / …）
  报"**尚未实现**"（区别于"不认识"）。

### 4.3 文字（建议只当占位用）

```jsonc
"text": {
    "fontSize": 16, "leftSpace": 12, "topSpace": 8,
    "path": "",                                  // 空 = 机器默认字体
    "content": { "normal": "确定" }               // hover / onClicked 会提醒"只画 normal"
}
```

> **不建议在按钮里配文字**：文字最终会烘成纹理，换语言就废了。
> 多语言请用 `SetText()` 在运行期设置。现在这套字段只是**把区域和间距钉住**
> （画的是同尺寸方块），等字形图集接上才真画字。

---

## 5. 命令行生成器（手写脚本 / 别的构建系统用）

```sh
inkgen --out-dir <目录> [--namespace <ns>] [--header <相对路径>] <file.json>...
inkgen --validate <file.json>...      # 只校验不写文件（CI / 自检）
```

- 退出码：0 全成功；**1 有错误**（逐条打印，每条带文件与行号，一次报完不早退）。
- `*.example.json` 按约定**不参与生成**（传进来会提示跳过）。
- 产物：`<out-dir>/<header 的目录>/button_service.h` 与 `button_register.cpp`。
- 内容一样时**不重写文件**（时间戳不动，下游不会无谓重编）。

实测的报错样例：

```text
[错误] …/badUnknownField.json:4: 字段 "widht" 不认识（已知字段：$comment、name、width、…）
[错误] …/badColorChannels.json:6: normal.source "0|0" 要有 3 或 4 个通道（r|g|b 或 r|g|b|a），现在有 2 个
[错误] …/dupTwo.json: name "dupName" 已经被 …/dupOne.json 用了（名字必须唯一，见 CXXCSS.md §1）
[错误] …/badSyntax.json：第 4 行第 5 列：对象里只能是 '字段: 值' 用逗号分隔（尾随逗号也不允许）
```

---

## 6. 组件 API 速查

`#include <button/InkingStaticButton.h>`

| 接口 | 用途 |
| --- | --- |
| `InkingStaticButton(parent, ButtonData)` | 手写数据构造（不走配置） |
| `InkingStaticButton(parent, "name")` | 按名字从 `ButtonLibrary` 取配置 |
| `SetOnClicked(fn)` / `HasOnClicked()` | 挂 / 查点击回调（回调签名 `void()`） |
| `SetText(s)` / `GetText()` | 运行期改文字（多语言走这里，**不标脏**） |
| `bool HitTest(worldX, worldY)` | 指针命中判定（和渲染同一份形状定义） |
| `GetState()` | 当前 `Normal` / `Hover` / `Pressed` |
| `GetShape()` / `GetAppearance()` / `GetData()` | 形状 / 当前外观 / 构造时那份配置 |
| `MouseHover(bool)` / `MousePress(bool)` / `MouseRelease()` / `TriggerClick()` | 幂等的状态写入口；**框架已自动喂**，一般不用自己调 |
| `SetVisible(bool)` / `ChangeZIndex(int)` | 继承自锚点，骨架变化（会通知场景重排） |
| `FindData("name")` | 静态方法：查配置，查不到返回 `nullptr` |

**按钮是静态组件**：没有 `Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` /
`ChangeOffset`——类型上就调不到（`src/ink.cpp` 里有编译期断言钉着）。
做"按下会改变尺寸"的按钮要另立类型。

---

## 7. 关键行为约定（不知道会踩）

### 7.1 位置怎么算

```text
位置 = 父级尺寸 × 上级锚点 − 自身尺寸 × 自身锚点 + 偏移
```

`(0,0)` 左上、`(1,1)` 右下。**偏移是"对齐之后再挪一点"，不是绝对坐标**：
`selfAnchor = traceAnchor = (0.5,0.5)` + `offset = 0` → 按钮正中在父级正中（设计画布 1920×1080 的中心 = `(960,540)`）。

实测的 `buttons_demo` 按钮：尺寸 200×100、双侧锚点 0.5 → 左上角在 `(860,490)`，
中心 `(960,540)`。

### 7.2 坐标与缩放

- 组件几何全在**设计坐标**里，`GetAbsX()` 给的就是它。
- 设计画布是 1920×1080（`include/config/window_config.h`）；设计尺寸是**编译期常量**。
- 窗口尺寸是运行期属性，窗口怎么拉都不改设计坐标，只改**展示倍率**（letterbox 等比 + 黑边）。
- 鼠标坐标由窗口层**每帧用渲染器**换算回设计坐标（因为黑边有偏移，乘除倍率对不上），
  组件只管设计坐标。

### 7.3 哪些变化会"标脏"

会：几何、可见性、层级、鼠标位置。
不会：**颜色 / 文字 / 透明度 / 展示倍率**——所以三态换色几乎零成本，
按钮能一直待在静态档（将来进命中表）。

### 7.4 已知的"能做但还不完美"

- **圆角是假的**：命中已按圆角判（被磨掉的角点不到），但**填充还是直角矩形**。
  自检里显式记着这条，不假装它不存在。
- **文字是方块**：占位块，颜色由 `textColor`（代码里设）决定，默认浅色。
- **命中是线性扫**：从绘制列表尾（最上面）往前找第一个命中的。
  几十个组件无感；成百上千就该换静态命中表（接口已按那时候的形状定好）。
- **取不到配置时**：按名字构造却查不到，会打一条警告并退回默认外观（不会崩）。
  名字拼错请优先用生成的常量，编译期就红。

---

## 8. 怎么自己验（可复现）

```sh
cmake --preset test-debug && cmake --build --preset test-debug
cd build/test-debug && ctest --output-on-failure          # 9 个用例

# 端到端：配置 → 库 → 渲染树 → 模拟点击 → 回调，然后真开窗
INK_AUTOQUIT=1 build/test-debug/bin/buttons_demo.exe
```

`INK_AUTOQUIT=1` 下 `buttons_demo` 会自己打一份实测报告（在 `build/test-debug/bin/Log.html`）：

```text
[日志][ButtonsDemo] 配置：名字=normalButton 尺寸=200x100
[通过][ButtonsDemo] I am deepseek, someone call me a blue fat fish who love eating white rice!
[日志][ButtonsDemo] 三态像素：通常=0xFF646468 悬停=0xFFDBDBDC 按下=0xFF646468（底色 0xFF121218）
[通过][ButtonsDemo] 自检通过：配置→库→渲染树→点击回调 这条链路是通的
[通过][InkingWindow] 窗口已打开 1920x1080，设计尺寸 1920x1080，展示倍率 1.000000，场景：MyScene
[日志][ButtonsDemo] 窗口已关闭，按钮被点了 1 次
```

三态像素是**离屏读回来的**：通常 / 按下（alpha 0.35）叠在底色上是 `0xFF646468`，
悬停（0.85）是 `0xFFDBDBDC` —— 亮度差一倍多，肉眼可辨。

---

## 9. 排错

| 现象 | 原因 / 处理 |
| --- | --- |
| 构建期报 `[错误] …json:行: …` | 配置写错了。报错会一次列完，直接改 json |
| 按钮**画不出来也点不到**，且无报错 | 按钮没有父级（该传 `*this`）没登记进渲染树 |
| `Show()` 之后画面空白 | 场景名对不上，或场景没在 `Show()` 之前构造 |
| 日志里 `按名字构造时库里没有这份配置` | 名字拼错，或生成还没跑（改用生成的常量） |
| 日志里 `按钮尺寸非法（宽 0 × 高 0）` | json 缺 `width` / `height`（正常情况下生成期就会报） |
| 三态在屏幕上没区别 | 颜色差距太小。**白色 + 0.35 / 0.85** 这种差距才看得见 |
| 场景名 `kSceneName` 是手写的 | Scene 的 json 生成还没做，属预期 |

**环境上一条**：跑 cmake / 编译前 `Remove-Item Env:LIB`。这台机器的 `LIB` 是
`;C:\mingw-64-sdl3\lib\x64;C:\SDL3\lib\x64`（两个目录都不存在），
会让某些用 `Add-Type` 的 PowerShell 脚本报一个看起来像权限问题的错。
（另外本机 `test-debug` 预设的 `INK_BUILD_EXAMPLES` 是 `OFF`，
`buttons_demo` 仍会构建，因为集成自检需要它。）

---

## 10. 下一步（还没做的）

按优先级：

1. **形状层的抗锯齿填充** —— 换掉 `include/ink/basic/InkingDraw.h` 的
   `fillShapeBounds` 一个函数，圆角就真的圆了。方案三选一（GPU / `SDL_RenderGeometry`
   三角化 / 先只做裁剪），见 `docs/TempTask.md` 第 2 步。
2. **完整的命中与三态** —— 静态烘焙命中表 + `Block / PassThrough / Miss` +
   `isDirty` 帧计数（现在是线性扫的最小闭环），见 `docs/InputDesign.md`。
3. **文字渲染** —— 字形图集 / 纹理层，`renderText` 已经是单一替换点。
4. **Scene 的 CXXCSS 生成** —— 让场景名也来自配置（照 Button 那条路走一遍）。
5. **每个名字一个独立类** + **按布局生成专用查找**（`CXXCSS.md` §6 的第 1、3 条）。
6. **DrawCall 合批** —— 提交层已经占好位（`SceneLibrary::RenderScene`）。
