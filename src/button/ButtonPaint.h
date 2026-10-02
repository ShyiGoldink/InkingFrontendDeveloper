#pragma once

// 按钮"画一笔 + 判一下命中"的实现：**静态 Button 与动态 Button 共用**。
//
// 为什么放 src/ 而不是 include/：它 include 了 `<SDL3/SDL.h>`，而公开头的纪律是
// 不把 SDL 拖进来（`include/ink/basic/InkingDraw.h` 是唯一被记录在案的例外，
// 那是框架内部件）。按钮的绘制属于**组件内部实现**，所以留在 src/ 里，
// 由两个 .cpp 各自 `#include "ButtonPaint.h"`（同目录相对包含）。
//
// 为什么不能各写一份：形状填充、文字占位块、以及**带变换的命中判定**是
// "按钮看起来是什么样 / 点得中哪儿"的唯一来源，两份实现各自演化就会出现
// "静态按钮和动态按钮画得不一样"——而这种不一致两边单独看都对，合起来才错。
//
// 绘制与命中放在同一个文件里是**刻意的**：它们必须共用同一份形状定义
// （`ShapeContains`）与同一份变换定义（`InkingTransform.h` 的正/逆变换），
// 而那两份东西的正确性恰恰取决于"正变换与逆变换严格互逆"。

#include <button/ButtonData.h>
#include <ink/basic/InkingAnchor.h>
#include <ink/dataStruct/InkingShapeSpec.h>
#include <ink/dataStruct/InkingTransform.h>

#include <cstdint>

namespace ink::detail {

/**
 * 按形状填一笔（设备像素）。
 *
 * 形状是唯一真相：几何走 `detail::fillShape`（`InkingDraw.h`），
 * 取值来自和命中用的 `ShapeContains` 同一份定义（InkingShapeSpec.h）——
 * 所以"画的是圆角、点到的是直角"这条已知不一致已经不存在了，
 * 圆角是真的圆（带 1 像素羽化边缘，见 InkingDraw.cpp）。
 *
 * `transform` 是**变换通道**（平移 / 旋转 / 缩放）：只改这一笔的位置与朝向，
 * 不改形状与尺寸，也不改命中表。
 */
void PaintButtonShape(const InkingAnchor& node, const ShapeSpec& shape,
                      const TransformSpec& transform, std::uint32_t color,
                      float pixelX, float pixelY);

/**
 * 按钮命中：世界坐标（设计空间）→ 本地坐标 → 形状判定。
 *
 * 三步和绘制**必须**用同一套定义：
 *   1. 减去布局位置（`GetAbsX/Y`，锚点推导出来的，和提交给渲染器的左上角同源）；
 *   2. 过一次**逆变换**——绘制那边过的是正变换，两者严格互逆，
 *      所以"画在哪儿"和"点在哪儿"永远是同一个答案；
 *   3. 问形状层（`ShapeContains`）。
 *
 * 三条中任何一条走偏，就会回到"画的是一个、点的是另一个"那条老路——
 * 这类 bug 两边单独看都对，只有把像素和命中摆在一起才能发现。
 */
bool HitTestButton(const InkingAnchor& node, const ShapeSpec& shape,
                   const TransformSpec& transform, float worldX, float worldY);

/**
 * 按钮的**保守命中包络**（本地坐标，相对左上角）。
 *
 * 为什么要专门算它：按钮的变换会随三态变（悬停平移+放大、按下下沉），
 * 而命中表的格子登记**冻结**（§11「变换通道不改表」）——所以登记时用的必须是
 * "三个状态盖住的全部范围"，精判才用当前变换。**只按当前变换登记会漏命中**：
 * 悬停那一帧按钮挪出去一点，边缘的点就落在表外了。
 *
 * 静态 / 动态按钮共用这一份：洞（动态组件占地）与表条目（静态组件）要的是
 * 同一件事——"它可能盖住哪儿"。
 */
ShapeBounds ButtonHitEnvelope(const ButtonData& data, const ShapeSpec& shape,
                              int width, int height);

/**
 * 画按钮上的文字。
 *
 * **现在还不是真的文字**：先按 fontSize / leftSpace / topSpace 占一块
 * 同尺寸的方块，好处是三个间距参数和"文字区域在哪儿"现在就能被自检用像素
 * 钉住。等字形图集 + 纹理层接上，换掉这一个函数体即可，调用点不用动。
 */
void PaintButtonText(const InkingAnchor& node, const ButtonData& data,
                     float pixelX, float pixelY);

}  // namespace ink::detail
