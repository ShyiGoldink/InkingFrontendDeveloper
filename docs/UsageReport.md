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
**静态 / 动态两档都能用**：静态按钮几何构造即定型（进命中表那条路），
动态按钮几何可运行期改、指针输入由它自己在 `Tick` 里拉（json 里写 `"dynamic": true`）。
**形状（含圆角）是真的**：画出来的区域和命中的区域取同一份形状定义，边缘抗锯齿。
**三态之间有过渡**：状态里写 `"transition": { "duration": 0.15 }` 就有，
颜色与**变换**（`"transform"`：平移 / 旋转 / 缩放）一起走同一段时长。
**静态按钮也能移动 / 旋转，而且仍然进命中表**——变换不改本地几何、也不改表，
绘制过正变换、命中过逆变换（实测"画的和点的"扫三万多个点零不一致）。
**还不能用**：第二档形状（`capsule` / `ring` / `line` / `polygon`）、真文字、
`img` / `svg` 外观、**缓动曲线**（过渡一律 linear）、**布局通道**的动画
（改尺寸那种，要重烘命中表）、场景的 json 配置（场景名仍是手写常量）。

---

## 2. 现在能用什么（逐项，实测）

| 能力 | 状态 | 说明 |
| --- | --- | --- |
| 从 json 生成按钮配置 | ✅ | 构建期自动跑，配置写错**构建就红** |
| **每个名字一个独立的按钮类** | ✅ | `ink::cxxcss::NormalButton`，配置烘在类里、构造时不查表 |
| 名字拼错 | ✅ | **编译期红**（类型不存在），不是运行期才空按钮 |
| 按名字构造按钮 | ✅ | 生成的类只给父级；手写字符串名字那条路仍保留 |
| 三态（通常 / 悬停 / 按下） | ✅ | 只换颜色；状态机 + 指针派发都已接通 |
| 三态之间的**过渡**（颜色 + 变换） | ✅ | 状态里写 `"transition": {"duration": 0.15}`；颜色与变换走同一段时长。实测颜色中点 `0x99FFFFFF`（见 §8） |
| **变换通道**（平移 / 旋转 / 缩放） | ✅ | `"transform": {"translateY":-6,"scale":1.05}`；**静态档也能用**，不改本地几何也不改命中表（§4.2） |
| 变换后"画的和点的一致" | ✅ | 离屏交叉对账：旋转 90° 查 18167 点、45° 查 31949 点，**不一致 0 个** |
| **命中表 + 三态** | ✅ | 静态查烘焙表（簇 + 格子 + CSR）、动态自判、按全局 z 序合并；`Block / PassThrough / Miss`。实测与线性扫**逐点对账 24978 个点、不一致 0 个**（§7.5） |
| **Scene 配置**（`CXXCSS/Scene/*.json`） | ⛔ **已撤销** | 场景手写（§4.5）：收益用手写就能拿到，代价是固定的 |
| 溢出桶 / 跨簇仲裁 | ❌ | **当前不需要**（组件构造即登记、z 序全局唯一），等做局部层级时再补 |
| **标脏协议**（结构 / 重绘 / 指针） | ✅ | 三种标记各有消费点，帧末统一消费、静置自清（§7.3）。实测：动画全程**结构脏一次都没标过** |
| 顶点缓存 / 合批 | ❌ | 消费目前只是"记账 + 清脏"，没有再做重活；合批也还没落地（每节点一次 `SDL_RenderGeometry`） |
| 缓动曲线 | ❌ | 现在一律 linear（配置里也没有 `easing` 字段） |
| 布局通道的动画（改尺寸） | ❌ | 那类变化**必须重烘命中表**，还没有配置字段 |
| 三态颜色真的看得见 | ✅ | 实测通常 `0xFF646468` vs 悬停 `0xFFDBDBDC`（悬殊一倍多） |
| 点击回调 | ✅ | 抬起时指针还在按钮上才算；滑出去松开算取消 |
| 形状命中 | ✅ | `rect` / `roundedRect` / `circle` / `ellipse`，与渲染同一份定义 |
| 形状**填充** | ✅ | 圆角真的圆：轮廓三角化 + 边缘 1 像素羽化（`detail::fillShape`）。实测圆角外一点读到的是底色 |
| 第二档形状 | ❌ | `capsule` / `ring` / `line` / `polygon` 仍报"还没实现" |
| 锚点与偏移 | ✅ | 百分比锚点 + 设计坐标偏移 |
| 层级 / 可见性 | ✅ | `zIndex`、`visible`（json 里可配） |
| **动态按钮**（`"dynamic": true`） | ✅ | 生成的类继承 `InkingDynamicButton`；几何可运行期改、进出命中表那档 |
| 动态按钮的**输入** | ✅ | 与静态版**不同**：静态由场景派发**推**，动态自己 `JudgePointer()` **拉**（实测） |
| 动态按钮的三态颜色 | ✅ | 实测通常 `0xFF1C525F` vs 悬停 `0xFF2DAFC6` |
| 动态按钮的**几何动画** | ⚠️ | 写入口（`Resize` 等）能用，但"每帧插值到哪儿"还没做 |
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
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>
#include <inkgen/my_app/button_service.h>   // ← 生成的（里面已经带了按钮类）

inline constexpr const char* kSceneName = "MyScene";

class MyScene : public ink::InkingScene {
public:
    MyScene() : ink::InkingScene(kSceneName),
                _button(asParent()) {          // ← 只要父级，配置烘在类里
        _button.SetOnClicked([] { INK_LOG_PASS("App", "点到了"); });
    }
private:
    ink::InkingAnchor* asParent() noexcept {
        return static_cast<ink::InkingAnchor*>(this);
    }
    ink::cxxcss::NormalButton _button;         // ← 生成出来的类
};

int main() {
    MyScene scene;                                     // 必须先于 Show
    ink::InkingWindow::Instance().Show(kSceneName);    // 阻塞到窗口关闭
}
```

`NormalButton` 是**从 `normalButton.json` 生成出来的类型**：名字 → `NormalButton`，
颜色 / 尺寸 / 形状 / 锚点都是它类里的字面量。所以：

- 拼错名字 = **类型不存在** → 编译期就红，不会等到运行期给个空按钮；
- 构造时**不查任何表**，`ButtonLibrary` 里有没有登记都不影响它能不能用；
- 那个类的 `Data()` 就是烘好的那份配置（`NormalButton::Data()`），
  `kName` 是字符串查找键（`"normalButton"`）。

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
| `name` | **是** | 字符串 | 必须**等于文件名**（去掉 `.json`）；是合法 C++ 标识符时才会生成**类**（`normalButton` → `NormalButton`） |
| `width` / `height` | **是** | 数字 | 设计坐标，必须 > 0。历史写法允许数字字符串，新配置请用数字 |
| `normal` | **是** | 对象 | 通常状态外观 |
| `hover` / `onclicked` | 否 | 对象 | **省略即继承 `normal`** |
| `text` | 否 | 对象 | 见 4.3 |
| `selfAnchorX/Y` | 否 | 数字 | 自身锚点，默认 0；建议留在 `[0,1]`（越界会警告） |
| `traceAnchorX/Y` | 否 | 数字 | 上级锚点，默认 0 |
| `offsetX/Y` | 否 | 数字 | 对齐之后再挪一点，设计坐标，默认 0 |
| `zIndex` | 否 | 整数 | 越大越靠上，默认 0；下限 `-128`（自动夹） |
| `visible` | 否 | 布尔 | 默认 `true` |
| `dynamic` | 否 | 布尔 | 默认 `false`。`true` → 生成的类继承 `ink::InkingDynamicButton`（几何可运行期改、不进命中表、输入自己拉）；见 4.4 |
| `shape` | 否 | 对象 | 省略 = 直角矩形，见 4.2 |

### 4.2 外观与形状

```jsonc
"normal": { "type": "color", "source": "r|g|b|a" }   // 0~1 浮点，可只给 3 段（a=1）
"normal": { "type": "img",   "source": "D:/assets/btn.png" }  // ⚠️ 还没落地
"normal": { "type": "svg",   "source": "<svg …/>" }            // ⚠️ 还没落地

// 过渡（可选）：**进入这个状态时**用多久，秒、必须 > 0。不写就是瞬间切换。
"hover":  { "type": "color", "source": "1|1|1|0.85", "transition": { "duration": 0.15 } }

// 变换（可选）：平移 / 旋转 / 缩放。原点是**组件中心**，rotate 单位是**度**。
"hover":  { "type": "color", "source": "1|1|1|0.85",
            "transform": { "translateY": -6, "rotate": 0, "scale": 1.05 } }

"shape": { "type": "rect" }                       // 直角矩形（默认）
"shape": { "type": "roundedRect", "radius": 10 }  // 圆角；radius 会被夹到 min(w,h)/2
"shape": { "type": "circle" }                     // 居中、直径取 min(w,h)
"shape": { "type": "ellipse" }                    // 用满 w×h
```

- `radius` 为负 → **报错**；超过 `min(w,h)/2` → 自动夹（那是形状的数学要求，不算配置错误）。
- 第二 / 第三档形状（`capsule` / `ring` / `line` / `polygon` / `arc` / `star` / …）
  报"**尚未实现**"（区别于"不认识"）。
- `transition` 只认 `duration`：**缓动曲线还没做**（一律 linear），所以没有 `easing` 字段；
  写别的键会报错。**过场段各自带时长**——`normal` 写 0.3、`hover` 写 0.15，
  就是进得快、退得慢（`normalButton.json` 就是这么配的）。
- `transform` 四个字段（`translateX` / `translateY` / `rotate` / `scale`）都可省；
  `scale` **不能为 0**（生成期报错）。它属于 `InputDesign.md` §11 的**变换通道**：
  **不改本地几何、也不改命中表**，所以静态档也能用；绘制过正变换、命中过逆变换。
  会改表的**布局通道**（`width` / `height` 那类）还没有字段。

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

### 4.4 动态档（`"dynamic": true`）

同一个组件的两档，判据是**几何会不会在构造之后自己变**：

| | 静态（不写 `dynamic`） | 动态（`"dynamic": true`） |
| --- | --- | --- |
| 生成出来的基类 | `ink::InkingStaticButton` | `ink::InkingDynamicButton` |
| 几何写入口 | 没有（类型上就调不到） | `Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset` |
| 命中 | 由场景的命中表回答 | 不进表，自己判 |
| 输入 | 场景派发**推**（`MouseHover` / `MousePress` / `MouseRelease`） | 自己**拉**（`JudgePointer()`，在 `Tick` 里读场景的指针状态） |
| 三态 / 画出来什么样 | 两份**共用**（`ButtonLook.h` / `ButtonPaint.cpp`） | 同左 |

```jsonc
"dynamic": true,     // ← 就这一个字段的差别
"offsetX": -320      // 与静态那块错开摆，示例里并排看
```

参考配置：`CXXCSS/Button/normalButton.json`（静态）与
`CXXCSS/Button/dynamicTestButton.json`（动态，生成的类叫 `DynamicTestButton`）。

**两个使用上的注意**：

1. 动态按钮自己拉指针，所以它要**有场景**、而且要被 `TickLogic` 推到
   （正常开窗时主循环自动推）。手写代码调它时别只调 `DispatchPointer()`——
   那条路跳过动态节点，什么都不发生。
2. **一个 Button 一个名**，所以静态版和动态版是两份 json，
   共同的颜色 / 尺寸会重复写一遍——要消掉重复得另加机制（**未决定**）。

### 4.5 场景：**手写，不配置**

场景不进 CXXCSS（它曾经能生成，评估后撤掉了）。手写就是 5 行：

```cpp
class MainScene : public ink::InkingScene {
public:
    static constexpr const char* kName = "mainScene";
    MainScene() : ink::InkingScene(kName) {}

    ink::cxxcss::NormalButton normalButton{this};          // 成员 + {this}
    ink::cxxcss::DynamicTestButton dynamicTestButton{this};
};
```

```cpp
MainScene scene;                               // 场景 + 里面的组件都建好了
ink::SceneLibrary::SetActiveScene(&scene);
ink::InkingWindow::Instance().Show();
```

要点：

- **组件是成员，`{this}` 填父级**——不用写 `asParent()`，也不可能"忘了传 parent
  导致组件不登记"（那种静默失败很难查）。
- **位置写在组件自己的配置里**（Button json 的锚点 / 偏移），场景里不写。
- **场景不能拷贝**（它登记在库里，子节点的父链指着它）。
- 成员的**声明顺序 = 构造顺序**，z 相同时后构造的在上。

> 为什么不做场景生成（`CXXCSS.md` §7.2 有完整版）：两处收益（组件不传 `parent`、
> 名字有编译期保险）手写都能拿到，而代价是固定的——一个界面要看两组文件、
> 表达力受限（不能放非按钮组件 / 不能覆盖位置 / 不能嵌套 / **不能按条件建组件**）。

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
| **`ink::cxxcss::<名字>`** | **生成的类**（如 `NormalButton`）：`NormalButton(parent)` 构造，配置烘在类里 |
| `NormalButton::Data()` / `::kName` | 烘好的那份配置 / 字符串查找键 `"normalButton"` |
| `InkingStaticButton(parent, ButtonData)` | 手写数据构造（不走配置） |
| `InkingStaticButton(parent, "name")` | 按名字从 `ButtonLibrary` 取（名字运行期才知道时才用） |
| `UnknownNameCount()` | 静态：上面那条路查不到配置发生过多少次（可断言，不静默） |
| `SetOnClicked(fn)` / `HasOnClicked()` | 挂 / 查点击回调（回调签名 `void()`） |
| `SetText(s)` / `GetText()` | 运行期改文字（多语言走这里，**不标脏**） |
| `bool HitTest(worldX, worldY)` | 指针命中判定（和渲染同一份形状定义） |
| `GetState()` | 当前 `Normal` / `Hover` / `Pressed` |
| `GetDisplayColor()` | 现在**实际画出来**的颜色（过渡进行中是插值出来的中间色） |
| `GetDisplayTransform()` | 现在**实际生效**的变换（过渡进行中是插值出来的中间值） |
| `IsRepaintDirty()` / `IsDirty()` | 这个节点还是"重绘脏 / 命中脏"吗（见 §7.3） |
| `SetHitPassThrough(bool)` | 命中之后让不让过（三态里的 PassThrough，默认不让 = Block） |
| `GetHitEnvelope()` | 命中索引用的**保守包络**（**变换会变的组件必须重写它**，见 §7.5） |
| `QueryHit(x, y)`（场景） | 查一次命中：`HitResult{Block/PassThrough/Miss, target}` |
| `GetHitTableEntryCount()` 等（场景） | 表的规模与"一次查询最多扫多少候选"（自检 / 预算用） |
| `ConsumeFrameDirty()`（场景） | 帧末统一消费，**框架主循环已经调了**，业务代码不用管 |
| `GetRepaintingNodeCount()`（场景） | 还有几个节点的画面在动（"画面稳了"就是它回 0） |
| `GetShape()` / `GetAppearance()` / `GetData()` | 形状 / **目标状态**那份外观 / 构造时那份配置 |
| `MouseHover(bool)` / `MousePress(bool)` / `MouseRelease()` / `TriggerClick()` | 幂等的状态写入口；**框架已自动喂**，一般不用自己调 |
| `SetVisible(bool)` / `ChangeZIndex(int)` | 继承自锚点，骨架变化（会通知场景重排） |
| `FindData("name")` | 静态方法：查配置，查不到返回 `nullptr` |

**按钮是静态组件**：没有 `Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` /
`ChangeOffset`——类型上就调不到（`src/ink.cpp` 里有编译期断言钉着）。
要做"按下会改变尺寸"的按钮就走**动态档**（下一张表）。

### 6.1 动态按钮：`#include <button/InkingDynamicButton.h>`

用法与静态版**完全一样**（构造 / 回调 / 文字 / 三态读法），只有两条不同：
它**有**几何写入口，而且**没有** `MouseHover` / `MousePress` / `MouseRelease`。

| 接口 | 用途 |
| --- | --- |
| **`ink::cxxcss::<名字>`** | 生成的动态类（如 `DynamicTestButton`），只要父级 |
| `Resize(w, h)` / `ChangeSelfAnchor(a)` / `ChangeTraceAnchor(a)` / `ChangeOffset(x, y)` | 继承自 `InkingDynamicAnchor` 的几何写入口，会自己标脏 + 通知场景 |
| **`JudgePointer()`** | **它唯一的输入入口**：读场景的指针状态，自己判命中 / 推三态 / 触发点击。`onTick` 里每步调一次 |
| `IsPointerOver()` / `IsPressCaptured()` | 上一次判定里"指针在按钮上" / "这次按下算在它身上"（自检观测口） |
| `GetState()` / `GetShape()` / `GetAppearance()` / `GetData()` / `GetText()` / `SetText()` | 和静态版同名同义 |
| `SetOnClicked(fn)` / `HasOnClicked()` / `TriggerClick()` | 和静态版同名同义 |
| `UnknownNameCount()` | 静态：动态这条路上"按名字查不到"发生过多少次（**与静态版各记各的**） |

> **别把 `DispatchPointer()` 当成动态按钮的输入**：那条路只服务静态档
> （`DispatchPointer` 内部对动态节点直接返回 nullptr）。动态按钮的输入发生在
> 它自己的 `Tick` 里，所以要推 `InkingScene::TickLogic(步长)`。
> 示例自检里专门有一条断言钉这个："指针挪上去 + 派发一次，三态必须纹丝不动"。

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

### 7.3 标脏：三种，各有各的消费点（T-3）

别把它们当成一个"脏了"的 bool——合并的代价是"动画每帧改颜色 → 每帧白重建一次
绘制列表"（功能全对，纯白干）：

| 标记 | 什么会让它脏 | 什么时候被消费 | 消费时做什么 |
| --- | --- | --- | --- |
| **命中脏** | 几何 / 可见性 / 层级 / 父子 | **帧首**（渲染之前） | 重建绘制列表 + 重算静态坐标快照（将来：重烘命中表） |
| **重绘脏** | 颜色 / 文字 / **变换** / 几何 | **帧末** | 记账 + 连续 **5** 帧没新脏就清（将来：重算顶点 / 重提 DrawCall） |
| **指针脏** | **指针移动**（场景级） | **帧末** | 记账（T-4：拿它当"这一帧要不要重算悬停"的开关） |

- **不会标脏的**：展示倍率 / 全局缩放（表的坐标在设计空间里，缩放只影响绘制换算）。
- **为什么指针脏要单列**：鼠标移动**不改任何几何**（表不用重烘），但它让"谁被指着"
  可能变。并进命中脏的后果就是每动一下鼠标重烘一次表。
- **帧末统一消费**：一帧里连着 `Resize` 三次、`ChangeZIndex` 两次，也只处理这一回
  （`InkingScene::ConsumeFrameDirty()`，挂在主循环收尾那一步，**只对活跃场景**——
  静默 / 停放场景的脏要留着）。
- **静置自清**：重绘脏连续 5 帧、命中脏连续 3 帧没有新脏就清掉。动画停下来之后
  `GetRepaintingNodeCount()` 会回落到 0——那就是"画面稳了"。
- 过渡的推进是**每渲染帧**一条通道（`InkingScene::TickFrame` → 节点 `TickAnimation`），
  和"动态档每逻辑步一次 Tick"是两条路。它**只在场景活跃时**跑：切走再切回来，
  过渡停在原地继续走（不是卡住）。离屏自检里光调 `RenderScene` **不会**推进过渡，
  要先 `TickFrame`——`docs/AGENTS.md` §6 第 37 条记了这条。

### 7.4 已知的"能做但还不完美"

- **圆角是真的了**（T-5 本轮落地）：画出来的区域和命中的区域取同一份形状定义，
  边缘还有 1 像素的抗锯齿过渡（离屏实测：过渡像素 `0xFF8C8C90` =
  白与底色各一半）。**只剩第二档形状**（`capsule` / `ring` / `line` / `polygon`）。
- **抗锯齿边缘的精确像素值看后端**：形状本身是"轮廓三角化 + 边缘羽化"画的，
  按标准像素中心语义生成；实测 SDL 的 software 渲染器采样点偏在像素右下角，
  所以"边界正好穿过像素"那一圈会读出 50% 的混合色。这不影响观感，
  但别按它写断言（细节见 `docs/AGENTS.md` §6 第 36 条）。
- **文字是方块**：占位块，颜色由 `textColor`（代码里设）决定，默认浅色。
- **命中是线性扫**：从绘制列表尾（最上面）往前找第一个命中的。
  几十个组件无感；成百上千就该换静态命中表（接口已按那时候的形状定好）。
- **动态按钮不进命中表、也不做遮挡仲裁**：它自判即自管自
  （`InputDesign.md` §15 第 1 条仍是开放问题）。顺带一提，现在这条线性扫里
  动态节点**仍然参与**"谁在最上面"的判断，所以它压住的静态组件不会被派发到
  ——遮挡本身是对的，只是"事件由谁接手"由动态组件自己决定。
- **动态按钮的输入采样率 = 逻辑步（50Hz）**：短于 20ms 的点击看不出来
  （人手点击 80ms 起步，够用）。要更稳得等输入机制那一轮把帧计数落下来。
- **过渡只有 linear 缓动**（T-2 落地）：颜色与变换会在时长内线性插值，
  但**缓动曲线还没做**——配置里也没有 `easing` 字段（不给只能填一个值的开关）。
- **过渡期间"目标外观"与"显示值"是两回事**：`GetAppearance()` 给你配置里
  那一份（目标），`GetDisplayColor()` / `GetDisplayTransform()` 才是屏幕上
  正在显示的颜色与变换。
- **文字占位块不跟着变换**：按钮转了、字还在原地。它现在用 `SDL_RenderFillRect` 画，
  而 FillRect 只能画轴对齐矩形；等字形图集落地、文字也走"形状填充 + 顶点变换"
  时自然跟上。
- **变换只作用于组件自己，不影响子级**：那种"整块在动"（抽屉 / Toast）要的是
  子树自带子表 + 变换通道，属于 `InputDesign.md` §9 的第三档，还没做。
- **取不到配置时**：按名字构造却查不到，会打一条**错误**日志 + 给一个**空按钮**
  （不画东西、不响应点击），并让 `UnknownNameCount()` 加一——**可断言，不静默**。
  名字拼错请优先用**生成的类**，那样编译期就红。

### 7.5 点一下会发生什么（T-4：命中表 + 三态）

```
鼠标位置（设计坐标）
  → 定位簇（粗块 256px，二分）→ 簇内格子（cellSize 按该簇条目尺寸定）
  → 扫该格候选（已是全局 z 降序）：包络粗筛 → node->HitTest 精判
  → 命中且 PassThrough → 记下继续往下找；命中且不让过 → Block，到此为止
  → 都没中 → 看点在不在**动态洞**里：在 → 问动态层、按 z 序合并；不在 → Miss
```

- **静态查表、动态自判**：动态组件不进表（它自己在 `Tick` 里判），只在表里留一个
  **洞**。洞是**性能开关**——漏登也只是多问一次动态层，不会点错
  （正确性由 z 序合并保证）。
- **三态**：`Block`（到此为止，半透明遮罩也是这类）、`PassThrough`（有东西但让过）、
  `Miss`（没东西）。让不让过用 `SetHitPassThrough(true)` 设——它是**策略**，
  和形状无关（形状由 `HitTest` 答），改了不标脏。
- **表什么时候重建**：跟绘制列表**同生同灭**——几何 / 可见性 / 层级 / 父子一变，
  帧首重建一次（§7.3 的命中脏）。**变换不触发重建**：组件上报的是"所有可能变换"的
  保守包络，精判用当前变换，所以动画期间表一次都不动。
- **空白区没有簇**：建表成本正比于**组件数量**，与画布面积无关。
- 要自己判命中就调 `QueryHit(x, y)`；`GetHitTableEntryCount()` 那组是观测口。

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
[日志][ButtonsDemo] 场景建好了：静态 normalButton（200x100） + 动态 dynamicTestButton（220x100）
[日志][ButtonsDemo] 配置：名字=normalButton 尺寸=200x100
[日志][ButtonsDemo] 悬停过渡（源色）：起=0x59FFFFFF 中途=0x99FFFFFF 到位=0xD9FFFFFF 目标=0xD9FFFFFF
[通过][ButtonsDemo] I am deepseek, someone call me a blue fat fish who love eating white rice!
[日志][ButtonsDemo] 三态像素：通常=0xFF646468 悬停=0xFFDBDBDC 按下=0xFF646468（底色 0xFF121218）
[通过][ButtonsDemo] 动态按钮被点了一下！
[日志][ButtonsDemo] 动态按钮三态像素：通常=0xFF1C525F 悬停=0xFF2DAFC6 按下=0xFF1C525F（底色 0xFF121218）
[通过][ButtonsDemo] 自检通过：配置→库→渲染树→点击回调 这条链路是通的
[通过][InkingWindow] 窗口已打开 1920x1080，设计尺寸 1920x1080，展示倍率 1.000000，场景：MyScene
[日志][ButtonsDemo] 窗口已关闭，按钮被点了 1 次
```

三态像素是**离屏读回来的**：

- 静态按钮：通常 / 按下（alpha 0.35）叠在底色上是 `0xFF646468`，
  悬停（0.85）是 `0xFFDBDBDC` —— 亮度差一倍多，肉眼可辨；
- 动态按钮（青色，0.35 / 0.85）：通常 `0xFF1C525F`、悬停 `0xFF2DAFC6`。

**过渡那行是 T-2 的证据**（源色，未合成）：起手 `0x59FFFFFF`（alpha 0.35）、
推半个时长 `0x99FFFFFF`、到位 `0xD9FFFFFF`（alpha 0.85）——中点正好是两者之间，
说明插值真的在跑。自检里是先 `scene.TickFrame(...)` 把过渡推完**再**采三态像素的，
否则采到的还是起手色（见 §7.3 那条）。

**变换的证据在 `ink_button_test` 里**，方式是"像素与命中交叉对账"
（不手算几何）：扫一片区域，逐个点问"这里有像素吗 / 这里命中吗"，
两者必须同答案。抗锯齿边缘（半透明那一圈）用"周围 3×3 同色"排除掉：

```text
[实测] 变换一致性：查了 18167 个点，不一致 0 个        ← 旋转 90°
[实测] 变换一致性：查了 31949 个点，不一致 0 个        ← 旋转 45°（过渡中途）
[通过] 旋转 90°：画出来的地方都能点到、点到的地方都画出来了
[通过] 旋转 90° 后 (110,150) 已经没有像素 / 也点不中
```

这一条钉的是变换通道的核心契约：**绘制过正变换、命中过逆变换，两者严格互逆**。
写反一个符号就会变成"画的是一个、点的是另一个"，而两边单独看都对。

**标脏协议的证据也在 `ink_button_test` 里**（21 条断言）。它盯的不是"值对不对"，
而是"三种脏互不牵连"——这类错功能全对、只是白干活，只有计数抓得住：

```text
[通过] 过渡推进了一帧 → 重绘脏
[通过] 变换在变 → 命中脏（同一个点可能落到别的组件上）
[通过] 颜色/变换动画**不该**惊动结构（惊动了就是每帧白重建一次绘制列表）
[通过] 过渡没走完，消费一次之后仍然脏
[通过] 场景看到「有 1 个节点的画面在动」（实得 1）
[通过] 连续 5 帧没有新的重绘脏 → 自己清掉 / 命中脏 3 帧之后清掉
[通过] 整段动画下来，结构脏一次都没被标过
[通过] 没配过渡的三态切换也要标重绘脏（画面当场就变了）
[通过] 原地喂同一个坐标不算脏（窗口层每帧都会喂一次）
```

**命中表的证据是"逐点对账"**（同一份 `ink_button_test`）：拿表和线性扫各查一遍，
全图每个点都必须给出同一个目标——参考实现写在自检里，框架里只留表，
两份实现各自独立：

```text
[实测] 命中表对账：查了 24978 个点，不一致 0 个
[通过] PassThrough 让过：点 C 穿到了下面的 D
[通过] 重叠区取 z 更高的那个 / 空白处是 Miss / 隐藏的组件既命不中也留不下洞
[通过] 动态压在静态上时动态赢 / 静态抬到更高 z 之后静态赢
[通过] 变换不触发重烘（条目数没变）/ 旋转后的按钮照样点得到
```

动态按钮那一段自检还多验了**输入路径**：指针挪上去只调 `DispatchPointer()`
时三态必须**纹丝不动**，推一个 `TickLogic(1/50)` 之后才变成 Hover ——
这就是"输入与静态 Button 不同"的硬证据。

**形状（含圆角与抗锯齿）的证据在 `ink_button_test` 里**，它把按钮画到离屏再读回像素：

```text
[实测] 圆角外一点的 f = 2.728，该点像素 = 0xFF1A1A22（底色 0xFF1A1A22）
[通过] 填充按形状走了：被磨掉的角现在是底色
[实测] 圆扫描线 y=460：实心 120，底色 98，过渡 2（样本 0xFF8C8C90）
[通过] 过渡像素的亮度严格介于底色与实心色之间
```

第一条钉"填充和命中是同一份定义"（角上真没像素了），
第二条钉"边缘是抗锯齿的"（过渡像素 = 白与底色各一半）。

---

## 9. 排错

| 现象 | 原因 / 处理 |
| --- | --- |
| 构建期报 `[错误] …json:行: …` | 配置写错了。报错会一次列完，直接改 json |
| 按钮**画不出来也点不到**，且无报错 | 按钮没有父级（该传 `*this`）没登记进渲染树 |
| `Show()` 之后画面空白 | 场景名对不上，或场景没在 `Show()` 之前构造 |
| 日志里 `按名字构造却查不到配置，给了一个空按钮` | 名字拼错，或这条用法该换成生成的类（`ink::cxxcss::NormalButton`） |
| 日志里 `按钮尺寸非法（宽 0 × 高 0）` | json 缺 `width` / `height`（正常情况下生成期就会报） |
| 三态在屏幕上没区别 | 颜色差距太小。**白色 + 0.35 / 0.85** 这种差距才看得见 |
| 动态按钮**完全没反应** | 它的输入不在 `DispatchPointer()` 上（那条只服务静态档）。推逻辑步：`scene.TickLogic(1.0/50)`；正常开窗时主循环会自动推 |
| 动态按钮的 `dynamic` 写成了字符串 | 生成期报错（`dynamic` 必须是 true / false），别指望它按 truthy 处理 |
| 配了 `transition` 但颜色**不渐变**、直接跳 | 三个可能：① 那个状态没配（过场时长写在**进入**的那个状态上）；② 手写代码里没人推 `scene.TickFrame(...)`（离屏自检最容易漏，见 §7.3）；③ 场景不活跃（静默 / 停放时过渡是不跑的） |
| **某个组件偶尔点不中**（尤其边角、旋转中的） | 十有八九是它的 `GetHitEnvelope()` 没盖住"所有可能的变换"。默认实现给的是整块矩形、按钮按三态并集算——**自己写的组件如果变换会在运行期变，必须重写它**（保留半像素余量，见 `HitTable.cpp` 里那条注释） |
| 命中结果和"看着在上面的那个"不符 | 检查 z：`IsAbove` 是 **z 优先、z 相同时后构造的在上**。同 z 的两个组件叠在一起时，赢的是后构造那个 |
| **三态外观配了却不生效**（`GetState()` 说 Hover，颜色/位置一点没变） | 手写 `ButtonData` 时忘了置 `hoverInheritsNormal = false`（和 `onclickedInheritsNormal`）——**构造按钮时会调 `Normalize()`**，它会拿 normal 把这两态整个覆盖。生成器生成的代码不受影响（它显式写了这两个标志），所以这是**手写配置**专门的坑（细节见 `docs/AGENTS.md` §6 第 43 条） |
| 动态组件忽然抢不到点击 / 挡不住下面 | 看它的**洞**有没有登记上：洞只在表重建时更新，而表跟着绘制列表走——几何变化要走写入口（`Resize` 等）才会标脏 |
| `transition.duration` 写成 0 或负数 | 生成期报错（必须 > 0）；不想要过渡就把整个 `transition` 删掉 |
| 场景名 `kSceneName` 是手写的 | Scene 的 json 生成还没做，属预期 |

**环境上一条**：跑 cmake / 编译前 `Remove-Item Env:LIB`。这台机器的 `LIB` 是
`;C:\mingw-64-sdl3\lib\x64;C:\SDL3\lib\x64`（两个目录都不存在），
会让某些用 `Add-Type` 的 PowerShell 脚本报一个看起来像权限问题的错。
（另外本机 `test-debug` 预设的 `INK_BUILD_EXAMPLES` 是 `OFF`，
`buttons_demo` 仍会构建，因为集成自检需要它。）

---

## 10. 下一步（还没做的）

按优先级：

1. ~~**形状层的抗锯齿填充**~~ ✅ **已完成**（轮廓采样 + `SDL_RenderGeometry`
   三角化 + 边缘羽化；静态 / 动态按钮与基类默认矩形共用 `detail::fillShape`）。
   同一条线上只剩**第二档形状**（`capsule` / `ring` / `line` / `polygon`）：
   要新写 SDF 公式与配置字段（`thickness` / `cap` / `sides` / `rotation`）。
2. **完整的命中与三态** —— 静态烘焙命中表 + `Block / PassThrough / Miss` +
   `isDirty` 帧计数（现在是线性扫的最小闭环），见 `docs/InputDesign.md`。
   动态组件的"洞"（§10.5 第 3 条）也在这一轮，动态按钮的输入方式不用改。
3. **Animation** —— 颜色过渡 + **变换通道**（平移 / 旋转 / 缩放）✅ 已完成（T-2）；
   **标脏协议**（结构 / 重绘 / 指针 + 帧末统一消费）✅ 已完成（T-3）；
   **命中表 + 三态**（簇 + 格子 + CSR + 动态洞）✅ 已完成（T-4）。
   同一条线上只剩**缓动曲线**（现在一律 linear）与**布局通道**（改尺寸那种，
   必须重烘命中表——"每态一份表"该出场的场景）。
4. **静态烘焙**（顶点缓存 + 合批）—— T-3 已经把"哪个节点需要重算"的信号
   （重绘脏 + 连续 5 帧自清）准备好了，消费点也留着，接上去就能见效。
5. **命中表的下一步**（要等前置条件）—— §10.4 的"按布局生成专用查找"
   （等生成器那一轮）；溢出桶与跨簇仲裁（等**局部层级 / 堆叠上下文**）。
4. **文字渲染** —— 字形图集 / 纹理层，`detail::PaintButtonText` 已经是单一替换点。
   接上之后顺带解决"文字不跟变换走"这条（它会改走形状填充 + 顶点变换）。
5. **Scene 的 CXXCSS 生成** —— 让场景名也来自配置（照 Button 那条路走一遍，T-7）。
6. **每个名字一个独立类** ✅（含动态档）+ **按布局生成专用查找**（`CXXCSS.md` §6 的第 3 条）。
7. **DrawCall 合批** —— 提交层已经占好位（`SceneLibrary::RenderScene`）。
   形状那一笔现在是"每个节点每帧重建顶点"（缓冲已复用），合批那一轮应该连
   顶点一起烘成静态网格。
