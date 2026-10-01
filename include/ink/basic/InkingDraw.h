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
 * 把形状的**包围盒**填成一块实心色，位置是设备像素。
 *
 * 名字特意写 Bounds 而不是 Shape：现在的填充还是**直角矩形**——形状层要
 * 抗锯齿填充得换 SDL3 GPU API、或者走 `SDL_RenderGeometry` 三角化
 * （TempTask 第 2 步的三个选项），还没做。
 *
 * 所以现在有一个**已知的不一致**：
 *   - 命中判定（`ShapeContains`）已经按圆角在判，被磨掉的角点不到；
 *   - 填充仍然把那个角涂上颜色。
 * 编译期和运行期都不会报错，只有真像素能看出来——所以 ink_button_test 把
 * 「圆角区域的像素还在」显式记了一条，而不是假装它不存在。
 * 填充换上 SDF 之后换掉这一个函数，调用方一处都不用改。
 */
inline void fillShapeBounds(SDL_Renderer* renderer, const ShapeBounds& bounds,
                            float pixelX, float pixelY, std::uint32_t color) {
    if (renderer == nullptr) {
        return;
    }
    const SDL_FRect rect{pixelX + bounds.x, pixelY + bounds.y, bounds.width,
                         bounds.height};
    setDrawColor(renderer, color);
    SDL_RenderFillRect(renderer, &rect);
}

}  // namespace ink::detail
