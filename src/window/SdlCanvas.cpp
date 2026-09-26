#include "SdlCanvas.h"

#include <SDL3/SDL.h>

namespace ink {

SdlCanvas::SdlCanvas(SDL_Renderer* renderer) : _renderer(renderer) {}

void SdlCanvas::fillRect(const Rect& rect, const Color& color) {
    if (_renderer == nullptr || !rect.valid() || color.a == 0) {
        return;
    }
    SDL_SetRenderDrawColor(_renderer, color.r, color.g, color.b, color.a);
    const SDL_FRect box{rect.x, rect.y, rect.w, rect.h};
    SDL_RenderFillRect(_renderer, &box);
}

void SdlCanvas::strokeRect(const Rect& rect, const Color& color, float thickness) {
    if (_renderer == nullptr || !rect.valid() || color.a == 0) {
        return;
    }
    SDL_SetRenderDrawColor(_renderer, color.r, color.g, color.b, color.a);

    if (thickness <= 1.0f) {
        // SDL_RenderRect 画的就是一像素边，够用就别自己拆。
        const SDL_FRect box{rect.x, rect.y, rect.w, rect.h};
        SDL_RenderRect(_renderer, &box);
        return;
    }

    // 粗描边：四条边各填一条（SDL 的线宽固定一像素）。
    fillRect(Rect{rect.x, rect.y, rect.w, thickness}, color);
    fillRect(Rect{rect.x, rect.bottom() - thickness, rect.w, thickness}, color);
    fillRect(Rect{rect.x, rect.y + thickness, thickness, rect.h - 2.0f * thickness}, color);
    fillRect(Rect{rect.right() - thickness, rect.y + thickness, thickness,
                  rect.h - 2.0f * thickness},
             color);
}

void SdlCanvas::drawLine(float x1, float y1, float x2, float y2, const Color& color) {
    if (_renderer == nullptr || color.a == 0) {
        return;
    }
    SDL_SetRenderDrawColor(_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(_renderer, x1, y1, x2, y2);
}

void SdlCanvas::drawText(const Rect&, const std::string&, const Color&) {
    // 字形系统未实现：见 Canvas::drawText 的说明。
}

void SdlCanvas::clear(const Color& color) {
    if (_renderer == nullptr) {
        return;
    }
    SDL_SetRenderDrawColor(_renderer, color.r, color.g, color.b, color.a);
    SDL_RenderClear(_renderer);
}

void SdlCanvas::present() {
    if (_renderer == nullptr) {
        return;
    }
    SDL_RenderPresent(_renderer);
}

}  // namespace ink
