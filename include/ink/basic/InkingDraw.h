#pragma once

// 绘制助手：**框架内部共用**，别在业务代码里直接用。
//
// 放在 include/ 下而不是 src/ 下，是因为 include/ 是唯一的头文件搜索根：
// 内部头放 src/ 就得再挂一个 PRIVATE include 目录，而它 include 的 SDL
// 反正已经由 ink_core PUBLIC 传播（SDL3::Headers），不差这一份。
//
// 为什么要有这个文件：`InkingAnchor::SubmitToRenderer` 里的 `onRender` 签名
// 只有坐标、没有渲染器——不希望每个组件实现都被迫多接一个参数。所以提交前
// 把渲染器挂到"当前渲染器"上，`onRender` 期间从槽里取。
//
// 这套槽 + 取色的样板原先只长在 InkingAnchor.cpp 里，Button 要按形状填色时
// 就得再抄一遍。抄第二遍的时候抽出来：两份实现各自演化，迟早会分叉
// （例如一份处理了空渲染器、另一份没处理）。

#include <ink/basic/InkingAnchor.h>
#include <ink/dataStruct/InkingColor.h>
#include <ink/dataStruct/InkingShapeSpec.h>
#include <ink/dataStruct/InkingTransform.h>

#include <SDL3/SDL.h>

#include <cstdint>

namespace ink::detail {

/**
 * 当前渲染器槽位：提交前设置、提交后清成 nullptr。
 *
 * 函数内静态量而不是文件级静态量：避开静态初始化顺序问题
 * （AGENTS §6 第 12 条）。绘制全程在 UI 线程，不需要加锁。
 */
inline SDL_Renderer*& currentRendererSlot() {
    static SDL_Renderer* renderer = nullptr;
    return renderer;
}

/** 当前渲染器；不在提交过程中时为 nullptr。 */
inline SDL_Renderer* currentRenderer() {
    return currentRendererSlot();
}

/** 0xAARRGGBB → SDL 的绘制颜色。 */
inline void setDrawColor(SDL_Renderer* renderer, std::uint32_t color) {
    SDL_SetRenderDrawColor(renderer, ColorRed(color), ColorGreen(color),
                           ColorBlue(color), ColorAlpha(color));
}

/**
 * @brief 按形状填一块，边缘**抗锯齿**——这是唯一的填充入口。
 *
 * 形状是唯一真相：几何取的是 `InkingShapeSpec.h` 那一份定义，
 * 和命中用的 `ShapeContains` 同源，所以"画的是圆角、点到的是直角"
 * 这条已知不一致在这里被消掉（早先那个只填包围盒的 `fillShapeBounds`
 * 已经删掉，调用方一处都没改：两条调用都换成了本函数）。
 *
 * 怎么画的：把形状的轮廓解析地采样成多边形，用 `SDL_RenderGeometry`
 * 三角化提交——**不是 CPU 逐像素光栅化**（docs/AGENTS.md §3 第 3 条）。
 * 边界那一圈再切出一条约 1 像素宽的带子：带子外圈顶点 alpha = 0、
 * 内圈顶点 alpha = 源色 alpha，于是边界上是 0→1 的线性过渡。
 *
 * 成立的前提是**绘制混合模式是打开的**：窗口层开过一次
 * （`InkingWindow::Show`），离屏自检也要自己开（AGENTS §6 第 32 条）。
 * 实测（software 渲染器）：`SDL_RenderGeometry` 的逐顶点 alpha 确实参与
 * 混合——alpha=0.5 的蓝叠在不透明红上读出 `0xFF7F0080`；同一三角形里
 * 两个顶点 alpha 不同时中间会插值（`0xFFE41B00` → `0xFF08EE08`）。
 * 混合关掉时 alpha 会被原样写进像素（读出 `0x80FFFFFF`），那时抗锯齿
 * 只剩"半个像素被涂成不透明色"——所以别把混合当可选项。
 *
 * 实现见 `src/ink/basic/InkingDraw.cpp`。
 *
 * @param renderer      渲染器；传 nullptr 是空操作。
 * @param shape         形状定义（`rect` / `roundedRect` / `circle` / `ellipse`）。
 * @param width,height  组件尺寸，**设计坐标**。
 * @param magnification 展示倍率（设计单位 → 设备像素）；几何与羽化带都乘它。
 * @param pixelX,pixelY 左上角，**设备像素**（调用方已经换算过）。
 * @param transform     变换通道（平移 / 旋转 / 缩放），绕组件中心应用；
 *                      传 `TransformSpec{}` 就是不动。它**只动这一笔的位置与朝向**，
 *                      不改形状、尺寸，也不改命中表（docs/InputDesign.md §11）。
 * @param color         填充色 0xAARRGGBB；alpha 会参与合成。
 */
void fillShape(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
               int height, float magnification, float pixelX, float pixelY,
               const TransformSpec& transform, std::uint32_t color);

}  // namespace ink::detail
