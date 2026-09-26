#pragma once

#include <string>

#include <core/InkColor.h>
#include <core/InkRect.h>

namespace ink {

/**
 * 绘制目标的抽象。
 *
 * 场景只认这四个动作，不认识 SDL：窗口层把 SDL3 的渲染器包成一份 Canvas
 * 递进来（见 src/window/SdlCanvas.h），无头自检里则可以不接任何后端直接跑。
 * 形状层（SDF）落地后，fillRect / strokeRect 会被换成形状函数 + 裁剪，
 * 场景看到的接口不变。
 */
class Canvas {
public:
    virtual ~Canvas() = default;

    /// 实心矩形。
    virtual void fillRect(const Rect& rect, const Color& color) = 0;

    /// 空心矩形（描边宽度以设计像素计）。
    virtual void strokeRect(const Rect& rect, const Color& color,
                            float thickness = 1.0f) = 0;

    /// 一条直线。
    virtual void drawLine(float x1, float y1, float x2, float y2, const Color& color) = 0;

    /// 文本。字形系统还没实现，默认什么都不画——属于占位而不是缺陷。
    virtual void drawText(const Rect& box, const std::string& text, const Color& color);
};

inline void Canvas::drawText(const Rect&, const std::string&, const Color&) {
    // 有意留空：SDF 字体层接手前，文本只占位不落笔。
}

}  // namespace ink
