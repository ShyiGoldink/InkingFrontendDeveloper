# CXXCSS 配置格式

CXXCSS 是本项目的**界面描述文件**：你在 `CXXCSS/` 下按组件类型建目录、
一个组件一个 `.json`，代码生成器（`inkgen`）读它们，在编译期生成对应的
组件类与查找结构。运行时只做每帧必须做的事。

**当前状态：格式已定型，生成器已实现，正在使用。**
生成器（`inkgen`，源码在 `tools/inkgen/`）会在构建期读 `CXXCSS/Button/*.json`，
校验后在 `build/` 下生成 C++：名字常量 + 一份启动期自动登记的配置表。
手写组件那条路（`ButtonData` / `AnchorData`）仍然可用，两条路落到同一份数据结构上。
哪些字段已经能对上、哪些还是纸面约定，见文末「落地状态」。

> **这份格式已经有第一个真实消费者了**：Button 的字段（三态外观、形状、锚点、
> 文字）已经完整落在 `include/button/ButtonData.h` + `InkingStaticButton` 里，
> `shape` 的几何（SDF / 命中 / AABB）也已经在 `InkingShapeSpec.h` 里实现。
> 生成器读的就是这些字段。剩下没对上的只有 `img` / `svg`、真文字渲染，
> 以及第二档形状（`capsule` / `ring` / `line` / `polygon`）。
>
> **一处约定先记住**：想让生成器给出 `ink::cxxcss::k<名字>Name` 这个常量，
> `name` 必须是**合法的 C++ 标识符**（`normalButton` 行，`button.example` 不行）。
> 名字里有 `.` 或 `-` 时不断生成失败，只是不生成常量——照样能用字符串按名查找。
> 另外，生成出来的常量放在**由构建脚本指定**的命名空间里（本项目示例用 `ink::cxxcss`）。

---

## 1. 目录与命名

```text
CXXCSS/
├─ CXXCSS.md              ← 本文档
├─ Button/
│  ├─ normalButton.json   ← 真实配置，参与生成
│  └─ button.example.json ← 示例，不参与生成
└─ <其它组件类型>/
   └─ <组件名>.json
```

> **没有 `Scene/`**：场景**不交给生成器**，手写（见 §7）。
> 这里曾经有过 `CXXCSS/Scene/<场景名>.json`，评估后撤掉了——理由在 §7.2。

三条**必须**遵守的命名规则：

1. **目录名 = 组件类型**（`Button` / `Scene` / …），大小写敏感；
2. **文件名 = `name` 字段** —— 两者不一致时生成器会报错，
   因为生成出来的类名、查找用的键都取自这个名字；
3. 以 `.example.json` 结尾的文件只作参考，**不参与生成**。

名字必须唯一且稳定：两个文件认领同一个 `name` 时，生成器会在**生成期**就报出
"已经被 xxx 用了"（运行期的库也会驳回后来者，但那是静默的错，晚一步难查得多）。

> **当前生成器做到哪一步**：**每个独立名字生成一个独立的类**
> （`normalButton` → `ink::cxxcss::NormalButton`），颜色 / 尺寸 / 形状 / 锚点
> 全部作为字面量烘在类里——构造按钮时不查任何表，"配置取不到"在类型上不可能发生。
> 代码里写 `ink::cxxcss::NormalButton _button{parent};` 就行。
> 还没做的是第 6 节第 3 条（按布局生成专用查找），以及"专属命中结构"这类更深一层的特化。
>
> **一处约定**：想让生成器给出类名，`name` 必须是**合法的 C++ 标识符**
> （`normalButton` 行，`button.example` 不行）。名字里有 `.` 或 `-` 时不断生成失败，
> 只是没有类——数据照样进 `RegisterAllButtons()`，那时只能按字符串名字使用。
> 生成出来的类放在**由构建脚本指定**的命名空间里（本项目示例用 `ink::cxxcss`）。

> **`width` / `height` 的两种写法**：数字和"数字字符串"都收（历史遗留写法，
> 见 §2.1），新写的配置请用数字。

> **怎么用生成的结果**：构建脚本调 `ink_add_generated()`，代码里
> `#include <inkgen/<你的集合名>/button_service.h>`，然后直接用生成的类
> （`ink::cxxcss::NormalButton`；字符串查找键是 `::kName`）。完整例子见
> [../examples/buttons_demo/main.cpp](../examples/buttons_demo/main.cpp)。

---

## 2. 公共骨架

任何组件配置的超集都长这样（以 Button 为例，实际见 `Button/button.example.json`）：

```jsonc
{
    "$comment": ["……"],        // 注释，生成器不读
    "name": "button.example",   // 必须与文件名一致
    "width": "200",             // 设计坐标
    "height": "100",

    "normal":    { "type": "color", "source": "0|0|0|0.75",
                   "transition": { "duration": 0.3 } },   // 进这个状态用多久（可选）
    "hover":     { "type": "color", "source": "0|0|0|0.85",
                   "transition": { "duration": 0.15 },
                   "transform": { "translateY": -6, "scale": 1.05 } },  // 平移/旋转/缩放（可选）
    "onclicked": { "type": "color", "source": "0|0|0|0.75" },

    "selfAnchorX": 0.5, "selfAnchorY": 0.5,   // 自身锚点
    "traceAnchorX": 0,  "traceAnchorY": 0,    // 上级锚点

    "shape": { "type": "roundedRect", "radius": 10 }
}
```

### 2.1 书写约定

| 约定 | 说明 |
| --- | --- |
| 编码 | **UTF-8**（无 BOM） |
| 注释 | 用 `"$comment"` 字段，字符串或字符串数组都行；生成器跳过它 |
| 键名 | 统一 **camelCase**（`selfAnchorX`），别用下划线 |
| 未知字段 | 生成器**报错**而不是静默忽略——拼错的字段比缺字段更难查 |

> **数值类型**：现有示例里 `width` / `height` 写成字符串（`"200"`），
> 而锚点、`radius` 写成数字。这个不一致是历史遗留，**新字段一律用数字**；
> 生成器对这两个字段两种写法都接受。等你决定要不要统一，我把示例改成数字。

### 2.2 锚点

锚点用**百分比**表示，(0,0) 是左上角、(1,1) 是右下角：

- `selfAnchorX/Y`：**自身**以内部哪个点去对应；
- `traceAnchorX/Y`：这个点对上**父级**的哪个位置。

组件在用锚点时一律被视作矩形。位置不作为字段存在，由这两个锚点推导
（`docs/API.md`「InkingAnchor」）。允许任意浮点数，但**建议保持在 `[0,1]`**——
超出范围的行为未定义，本项目不保证。

> ⚠️ 旧示例里 `traceAnchorY` 曾拼成 `trancAnchorY`。生成器会把拼错的键
> 当作未知字段报错，所以这类错不会静默通过——但改配置时留意别抄错。

---

## 3. 形状层（shape）

这是格式里最需要定清楚的部分，因为它**同时决定两件事**：

```text
shape ──┬─→ 渲染（SDF：填充 / 描边 / 裁剪）
        ├─→ 命中（contains：这个点在不在里面）
        └─→ 索引（AABB：塞进命中表的哪个格子）
```

**三者由同一份定义派生，不允许各写一遍。** 否则就会出现"画的是圆角、
点到的是直角"——两边单独看都对，合起来才错，而且极难排查。

### 3.1 写法：对象 + 参数，**不要把参数编进字符串**

```jsonc
"shape": { "type": "roundedRect", "radius": 10 }    // ✅ 推荐
"shape": "rounded-rect-10"                          // ❌ 不要
```

理由：生成器要按 `type` 查形状表做参数校验；把参数塞进字符串意味着
再写一个迷你解析器，且没法给出"radius 必须为正"这种具体报错。

### 3.2 形状可以省略

不写 `shape`（或写 `{}`）等价于 `{"type":"rect"}`——由 `width` / `height`
定义的直角矩形。所以老配置不加字段也能继续工作。

### 3.3 形状表

**第一档：现在就要，Button 立刻用得上**

| `type` | 参数 | 说明 |
| --- | --- | --- |
| `rect` | 无（尺寸来自 `width`/`height`） | 直角矩形，默认值 |
| `roundedRect` | `radius` | 圆角矩形。`radius` 会被夹到 `min(width,height)/2` |
| `circle` | 无 | 正圆，直径取 `min(width,height)`，居中 |
| `ellipse` | 无 | 椭圆，用满 `width`×`height` |

**第二档：UI 高频，紧跟着做**

| `type` | 参数 | 说明 |
| --- | --- | --- |
| `capsule` | 无 | 胶囊：两端是半径 `min(w,h)/2` 的半圆 |
| `ring` | `thickness` | 圆环，`thickness` 为环宽（设计坐标，>0） |
| `line` | `thickness`、`cap`（`butt`\|`round`\|`square`） | 线段/分割线 |
| `polygon` | `sides`（≥3）、`rotation`（弧度，可选） | 正多边形 |

**第三档：先不做，留位**

`arc`（圆弧）、`star`、`chamferRect`（切角矩形）、`superellipse`（squircle）。

**明确不支持**（别指望 `shape` 能表达）：

| 需求 | 为什么不行 | 该走什么 |
| --- | --- | --- |
| 虚线边框 | 不是单一距离场 | 专门的 dash 逻辑 |
| 任意贝塞尔/SVG 路径 | 需要数值最近点求解，代价高 | 独立的路径模块 |
| 文字 | 字形是另一条路 | `text` 段（见第 5 节） |
| 纹理填充 | 形状只管"点是否在内" | 纹理层 + 形状裁剪 |

### 3.4 关于符号约定（一处与旧文档的偏差）

SDF 的**通用约定**是：`f < 0` 在内部、`f = 0` 在边界、`f > 0` 在外部。
本项目原先在 `docs/AGENTS.md` 里写的是「裁剪 = `f >= 0`」，方向是反的。

**形状层的落地采用通用约定（负 = 内）**，理由：

- 可以直接照抄公开的 SDF 参考实现，不用逐个翻符号；
- 抗锯齿是标准写法 `smoothstep(-aa, aa, d)`；
- `abs(d)` 天然是"到边界的距离"，描边宽度直接用。

裁剪的语义相应写成「保留 `f <= 0` 的区域」。`docs/AGENTS.md` 已同步。
形状层还没实现，现在改零成本；一旦铺开，翻符号是持续的隐性成本。

### 3.5 形状与语义：形状 ≠ 填充

形状只描述**几何**，颜色/描边是另一层的事。一段配置里它们平级：

```jsonc
"shape": { "type": "roundedRect", "radius": 10 },
"normal": { "type": "color", "source": "0|0|0|0.75" }
```

描边（`stroke`）与阴影（`shadow`）在生成器落地时按同样思路加平级字段，
**不要把描边宽度塞进 `shape` 的参数里**——那会让同一个圆角矩形出两套参数。

### 3.6 不合法参数的处理

**必须是"报错停下"，不是"夹一下就过"。** 生成器阶段的问题最容易查，
放进运行时就是"跑起来了但样子不对"。例如：

- `radius` 为负、或不是数字 → 报错；
- `type` 不认识 → 报错，并列出支持的取值；
- `polygon` 的 `sides < 3` → 报错。

唯一允许自动修正的是**语义上无歧义的夹取**：`radius` 超过
`min(width,height)/2` 时夹到上限（这是形状本身的数学要求，不是配置错误）。

---

## 4. Button

完整字段见 `Button/button.example.json` 里的 `$comment`。要点：

| 字段 | 说明 |
| --- | --- |
| `name` | 与文件名、代码里使用的名字三处一致 |
| `width` / `height` | 设计坐标尺寸 |
| `dynamic` | 可选，布尔。`true` → 生成的类继承 `ink::InkingDynamicButton`（几何可在运行期改、不进命中表、指针由它自己在 `Tick` 里拉）；不写或 `false` → `ink::InkingStaticButton`。见 4.3 |
| `normal` / `hover` / `onclicked` | 三态外观，结构相同 |
| `text` | 可选。**不建议用**，见下 |
| `selfAnchorX/Y`、`traceAnchorX/Y` | 锚点 |
| `shape` | 形状层 |

### 4.1 三态外观

`normal`（通常）/ `hover`（悬停）/ `onclicked`（按下），三者结构一致：

```jsonc
{ "type": "color" | "img" | "svg", "source": "……" }
```

| `type` | `source` 写什么 |
| --- | --- |
| `color` | RGBA，用 `|` 分隔：`"0|0|0|0.75"`（0~1 的浮点） |
| `img` | 图像**绝对路径**，编译期转成二进制纹理 |
| `svg` | 直接写 SVG 代码 |

> **注意**：旧示例把键写成了 `"type:"`（多一个冒号）。那是笔误，
> 正确写法是 `"type"`。本仓库的示例已修正。

### 4.1.1 `transition`：三态之间的过渡时长（可选）

```jsonc
"hover": { "type": "color", "source": "1|1|1|0.85", "transition": { "duration": 0.15 } }
```

写在**状态自己**身上，表示「**进入这个状态时**用多久过渡」：

| 字段 | 说明 |
| --- | --- |
| `duration` | 秒，**必须 > 0**。不想要过渡就整个 `transition` 都别写 |

于是"过场段各自带时长"天然成立：`normal` 写 0.3、`hover` 写 0.15，
就是**进悬停快、退回慢**（`Button/normalButton.json` 就是这么配的）。
按下（`onclicked`）通常**不配**——按下要的是即时反馈，那一档瞬间切换。

三条要记住的约定：

1. **颜色与变换都按这一段时长走**：同一段过渡里两者一起插值，不需要各配一个时长。
2. **缓动曲线还没做，现在一律 linear。** 配置里**没有** `easing` 字段——
   给一个只能填一个值的开关只会让人以为有得选；真要曲线时再加，那时它是新字段。
3. **过渡由渲染帧推进**（不是逻辑步）：它跟着画面走，用的是真实 delta。

### 4.1.2 `transform`：平移 / 旋转 / 缩放（可选）

```jsonc
"hover": {
    "type": "color", "source": "1|1|1|0.85",
    "transition": { "duration": 0.15 },
    "transform": { "translateY": -6, "scale": 1.05 }
}
```

四个字段都可以不写（不写就是单位变换，等于不动）：

| 字段 | 含义 |
| --- | --- |
| `translateX` / `translateY` | 平移，**设计坐标**（不是百分比） |
| `rotate` | 旋转，**度**、顺时针为正 |
| `scale` | 均匀缩放，**不能为 0**（0 会在生成期报错——那会把形状压成一个点） |

两条约定：

- **原点固定是组件中心**（等价 CSS 的 `transform-origin: 50% 50%`）。
  形状都是按左上角定义的，绕左上角转不是直觉里的样子。
- 它属于 **InputDesign.md §11 的"变换通道"**：**不改本地几何、也不改命中表**——
  表按本地形状烘，查询时把点**反变换**回本地空间。所以：

| 通道 | 例子 | 命中表要不要重烘 |
| --- | --- | --- |
| **变换**（这个字段） | `translateX/Y`、`rotate`、`scale` | **不用**——表冻结，查询时反变换 |
| **布局**（还没有字段） | `width` / `height` / 会让兄弟重排的定位 | **要**重烘受影响的子树 |

推论：**静态组件也能配 `transform`，而且仍然进命中表**。
绘制时顶点过正变换、命中时点过逆变换，两者严格互逆（`InkingTransform.h` 里
两份函数挨着写，就是为了这个）。示例见 `Button/normalButton.json`
（悬停上移 6 + 放大 5%、按下下压 2）与 `Button/dynamicTestButton.json`
（悬停转 8° + 放大 4%）。

> **已知限制**：文字占位块**不跟着** `transform` 走（按钮转了、字还在原地）。
> 它现在用 `SDL_RenderFillRect` 画，而 FillRect 只能画轴对齐矩形；
> 等字形图集落地、文字也走"形状填充 + 顶点变换"时自然跟上。

### 4.2 `hover` / `onclicked` 可省略

省略时继承 `normal` 的外观。三个都写只是为了示例完整。

> 注意继承是**整段**继承：`hover` 省略时它的 `transition` 与 `transform`
> 也跟着 `normal` 走。想只要颜色继承、其余另配，就得把 `hover` 显式写全。

### 4.3 `dynamic`：这块按钮是静态档还是动态档

```jsonc
"dynamic": true     // 动态：生成的类继承 ink::InkingDynamicButton
```

判据只有一条（`include/ink/basic/InkingAnchor.h` 末尾那段）：**几何会不会在构造
之后自己变**。

| | 静态（不写 `dynamic`） | 动态（`"dynamic": true`） |
| --- | --- | --- |
| 生成出来的基类 | `ink::InkingStaticButton` | `ink::InkingDynamicButton` |
| 几何写入口 | **没有**（类型上就调不到） | `Resize` / `ChangeSelfAnchor` / `ChangeTraceAnchor` / `ChangeOffset` |
| 命中 | 由场景的命中表回答 | 不进表，自己判 |
| 输入 | 场景派发**推**过来（`MouseHover` / `MousePress` / `MouseRelease`） | 自己在 `Tick` 里**拉**（`JudgePointer`，读场景的指针状态） |
| 三态外观 / 画出来什么样 | 两份**共用**（`include/button/ButtonLook.h`、`src/button/ButtonPaint.cpp`） | 同左 |

**它不是"另一个组件"，是同一个组件的另一档**：颜色、形状、文字、点击回调的
用法完全一样，差别只在"几何能不能在运行期改"和"输入从哪来"。
所以两块按钮要各自一份 json（名字也必须各自唯一，见 §1），颜色之类的共同字段
会重复一遍——要消掉重复得另加机制（例如 json 之间的 `extends`），**未决定**。

`dynamic` 是个**生成期**开关，它不进运行期那份数据结构（`ButtonData` 里没有这个
字段）：类型本身已经说明它属于哪一档，再存一遍就是第二份真相。

参考配置：`Button/normalButton.json`（静态）与 `Button/dynamicTestButton.json`（动态）。

---

## 5. 文字（text）

结构：

```jsonc
"text": {
    "fontSize": 16,
    "leftSpace": 12,      // 距左侧（设计坐标）
    "topSpace": 8,        // 距顶部（设计坐标）
    "path": "",           // 字体绝对路径；空则用机器默认字体
    "content": {
        "normal": "确定",
        "hover": "确定",
        "onClicked": "确定"
    }
}
```

> ⚠️ **不建议直接在按钮里配文字。**
> 文字最终会被烘成**纹理**，纹理里的字是"画上去的像素"，换语言就废了。
> 需要多语言的界面请把文案放到运行时数据里，用代码设置。
> 这条不是格式限制，是设计建议——格式允许你这么写，后果自负。

`path` 为空时用机器的默认字体。**依赖机器字体意味着跨机器不一致**，
交付版本应显式指定字体路径。

---

## 6. 生成器会拿这些做什么

理解这一步有助于判断"某个字段该不该拆"：

1. **每个名字生成一个独立类**（`button.example` → 一个专用按钮类；
   `mainScene` → 一个场景类）；
2. **形状展开成 SDF + `contains` + AABB**，三份来自同一个 `type` 分支；
3. **按布局类型生成专用查找**——等宽等距的按钮组可以算成
   `index = (x - rowX) / (w + gap)`，O(1) 且不需要表（`docs/InputDesign.md` §10.4）；
4. **锚点与尺寸变成编译期常量**，运行期不再解析配置。

所以配置里**能让生成器算的，就别留给运行时**。这也是"不要在按钮里配文字"
的同一个道理：能烘的烘掉，烘不了（多语言）的才留给运行时。

**已经做到 / 还没做到**（写清楚，免得以为第 1、3 条生效了）：

| 第几条 | 状态 |
| --- | --- |
| 1. 每个名字一个独立类 | ✅ 已做到：**按钮**类（`ink::cxxcss::NormalButton`；`"dynamic": true` 时基类是 `InkingDynamicButton`）由配置烘成字面量 |
| 2. 形状展开成 SDF + contains + AABB | ✅ 形状层已经就位（`InkingShapeSpec.h`），一个 `type` 分支派生三个量 |
| 3. 按布局类型生成专用查找 | ❌ 还没做（命中走的是烘焙表，还没有"按布局生成纯算术索引"） |
| 4. 锚点与尺寸变成编译期常量 | ✅ 生成成了字面量 + 类内静态数据，运行期不再解析 json |

---

## 7. 场景：**手写，不生成**

场景不进 CXXCSS。它是**编排**（放哪些组件、按什么条件放），不是数据。

```cpp
class MainScene : public ink::InkingScene {
public:
    static constexpr const char* kName = "mainScene";   // 一行常量

    MainScene() : ink::InkingScene(kName) {}

    // 组件是成员；`{this}` 由默认成员初始化器填父级。
    // 少了 `asParent()` 这处样板，也不可能"忘了传 parent → 组件不登记"。
    ink::cxxcss::NormalButton normalButton{this};
    ink::cxxcss::DynamicTestButton dynamicTestButton{this};
};
```

用户代码就三行：

```cpp
MainScene scene;                               // 场景 + 里面的组件都建好了
ink::SceneLibrary::SetActiveScene(&scene);
ink::InkingWindow::Instance().Show();
```

组件的位置仍然写在**组件自己的配置**里（Button json 的锚点 / 偏移），
所以"换个场景位置跟着走"这条照样成立——它和场描述在哪儿无关。

### 7.1 为什么手写就够

生成场景本来能拿到两处收益，但**它们都不需要生成器**：

| 收益 | 手写怎么拿到 |
| --- | --- |
| 组件不用传 `parent` | 成员 + 默认成员初始化器 `{this}`（就是上面那几行） |
| 名字有编译期保险 | 组件类型名由**生成的按钮类**提供（`NormalButton` 拼错就是编译错误）；场景名一行 `kName` 常量就够 |

### 7.2 为什么撤掉（2026-10-02 评估）

生成场景曾经实现过一版并跑通（`TaskGuide.md` 的 T-7），评估后回退，理由是
**收益用手写就能拿到，而代价是固定的**：

- 一个界面要看**两组**文件（成员清单在 Scene json、位置在 Button json）；
- 表达力受限，而且限制是结构性的：不能放非按钮组件、不能在场景里覆盖位置、
  不能嵌套、**不能按条件建组件**（`if (isAdmin) ...` 手写随手就写，
  生成出来的是静态成员）；
- 多一类输入、多一层跨文件校验、多一个示例、多四个测试 fixture ——
  全都在维护面上。

它真正的价值在**将来**：`docs/InputDesign.md` §10.4 那种"按布局生成专用查找"
（那时配置里得带 layout 信息，生成才有意义），或者窗口层真的开始
"按名字切场景"。到那时再设计，而且那时该重新设计（现在的格式表达不了布局）。

---

## 8. 落地状态

诚实地区分"已实现"和"纸面约定"，别让后来的人以为字段已经生效：

| 项 | 状态 |
| --- | --- |
| 本文档描述的格式 | **已定型**（约定） |
| 代码生成器 `inkgen` | ✅ 已实现（`tools/inkgen/`，构建期生成到 `build/`） |
| **每个名字一个独立的按钮类**（配置烘成字面量） | ✅ 已实现（`build/<预设>/inkgen/<集合>/button_service.h`） |
| `CXXCSS/` 目录被读取 | ✅ 构建时由 `ink_add_generated()` 传入文件列表（`CONFIGURE_DEPENDS` 自动跟进新文件） |
| 生成代码接进构建 | ✅ `ink_add_generated(目标 …)` 一行，产物目录自动进 include 路径 |
| `name` 与文件名一致性 / 未知字段 / 参数合法性 | ✅ 生成期报错（每条带文件与行号）；重名也在这里拦 |
| `shape` 形状层的**几何**（SDF / 命中 / AABB） | ✅ 已实现（`include/ink/dataStruct/InkingShapeSpec.h`，第一档四种） |
| `shape` 形状层的**填充**（圆角真的画出来） | ✅ 已实现：轮廓采样 + `SDL_RenderGeometry` 三角化 + 边缘 1 像素羽化（`include/ink/basic/InkingDraw.h` 的 `detail::fillShape`，实现在 `src/ink/basic/InkingDraw.cpp`）。填充与命中同源 |
| `shape` 的第二档形状（`capsule` / `ring` / `line` / `polygon`） | ❌ 未实现（生成器仍报"格式里有定义但形状层还没实现"） |
| 锚点 / 尺寸 / 层级 | ✅ 已实现，配置与手写 `AnchorData` 两条路都通 |
| Button 的字段（三态 / 形状 / 锚点 / 回调） | ✅ 已实现（`include/button/ButtonData.h` + `InkingStaticButton`） |
| `dynamic` 字段（静态档 / 动态档） | ✅ 已实现：生成器按它选基类（`InkingStaticButton` / `InkingDynamicButton`）；两档的输入方式不同，见 §4.3 |
| `transition` 字段（三态之间的过渡） | ✅ 已实现：生成器烘进 `ButtonAppearance::transitionSeconds`，运行期由 `ButtonLook::Advance` 插值（颜色与变换同一段时长，见 §4.1.1）。**缓动曲线未实现**（一律 linear） |
| `transform` 字段（平移 / 旋转 / 缩放） | ✅ 已实现：**变换通道**，不改本地几何也不改命中表（§4.1.2），所以静态 / 动态两档都能用。文字占位块暂不跟随 |
| **场景配置**（`CXXCSS/Scene/*.json`） | ⛔ **已撤销**（§7.2）：场景手写，位置仍写在组件自己的配置里 |
| **布局通道**的动画（改尺寸 / 会重排的定位） | ❌ 未实现：那类变化**必须重烘命中表**（"每态一份表"该出场的场景），还没有配置字段 |
| Button 的 `img` / `svg` 两种 `type` | ⚠️ 生成器收下并提醒，运行期**不画**（要纹理层） |
| `text` 文字渲染 | ⚠️ 字段已实现并经生成器落地；画的是同尺寸占位方块（等字形图集） |
| `zIndex` / `visible` / `offsetX` / `offsetY` | ✅ 生成器认这几个字段（示例里没写，默认 0 / true） |
| Scene 的配置（`CXXCSS/Scene/*.json`） | ❌ 未实现（场景名目前还是代码里的常量） |

**改 `CXXCSS/Button/*.json` 现在会真的影响行为**：下次构建时重新生成。
但**新增**一个 json 之后，Ninja 会自动重新 glob（`CONFIGURE_DEPENDS`），
不需要手动重新配置。

> **给字段定稿的人一句提醒**：第 5 节的 `text` 字段是从原示例注释**推断**的，
> 没有示例文件可对照。它现在已经在 `ButtonData` 与生成器里各固化了一次
> （`fontSize` / `leftSpace` / `topSpace` / `path` / `content`）——
> 要改名或改结构，现在改代价最小。
