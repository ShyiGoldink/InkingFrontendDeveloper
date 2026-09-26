#pragma once

#include <string>

#include <core/Canvas.h>

struct SDL_Renderer;

namespace ink {

// SDL3 后端的画布：把 Canvas 的四个动作翻成 SDL_Render* 调用。
//
// 它只出现在窗口层的实现里，公共头文件看不到——这样场景与按钮完全不依赖 SDL，
// 自检可以不接任何后端直接跑。等 SDF 形状层落地，这里换成「形状函数 + 裁剪 +
// DrawCall 合批」，场景那一边的代码一行都不用改。
class SdlCanvas final : public Canvas {
public:
    explicit SdlCanvas(SDL_Renderer* renderer);

    void fillRect(const Rect& rect, const Color& color) override;
    void strokeRect(const Rect& rect, const Color& color, float thickness) override;
    void drawLine(float x1, float y1, float x2, float y2, const Color& color) override;
    void drawText(const Rect& box, const std::string& text, const Color& color) override;

    /// 整屏刷底色（重绘时才调）。
    void clear(const Color& color);
    /// 把这一帧提交上屏。
    void present();

private:
    SDL_Renderer* _renderer = nullptr;
};

}  // namespace ink
