#pragma once

#include <cstdint>

namespace ink {

/// 直通的 8 位 RGBA。
///
/// SDF 形状层接手之前，颜色只是喂给绘制调用的四个字节：不做色彩空间转换、
/// 不做渐变采样，避免在骨架期就引入一套用不上的颜色模型。
struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

/// 不透明色。
constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) noexcept {
    return Color{r, g, b, 255};
}

/// 带透明度的颜色。
constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                     std::uint8_t a) noexcept {
    return Color{r, g, b, a};
}

/// 完全透明：等于「这块不画」。
constexpr Color kTransparent{0, 0, 0, 0};

}  // namespace ink
