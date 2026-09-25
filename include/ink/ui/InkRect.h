#pragma once

// 设计坐标系下的矩形。
//
// 全框架只有一套坐标：设计空间像素（左上角原点，y 轴向下），绝对定位。
// 场景、按钮、命中表存的全是这份矩形，所以"点击坐标 → UI"的映射不需要
// 任何换算：命中表里记的就是屏幕上的那块区域。

namespace ink {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    constexpr float left() const noexcept { return x; }
    constexpr float top() const noexcept { return y; }
    constexpr float right() const noexcept { return x + w; }
    constexpr float bottom() const noexcept { return y + h; }

    /// 半开区间 [x, x+w) × [y, y+h)：右/下边界不算在内，
    /// 这样相邻控件的接缝上不会两个都命中。
    constexpr bool contains(float px, float py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }

    /// 面积为正才算有效；宽高为 0 的矩形不配进命中表。
    constexpr bool valid() const noexcept { return w > 0.0f && h > 0.0f; }

    constexpr bool intersects(const Rect& other) const noexcept {
        return !(other.x >= right() || other.right() <= x
                 || other.y >= bottom() || other.bottom() <= y);
    }

    /// 两个矩形的并集（顺带把无效矩形当空集处理）。
    constexpr Rect united(const Rect& other) const noexcept {
        if (!valid()) {
            return other;
        }
        if (!other.valid()) {
            return *this;
        }
        const float nx = x < other.x ? x : other.x;
        const float ny = y < other.y ? y : other.y;
        const float nr = right() > other.right() ? right() : other.right();
        const float nb = bottom() > other.bottom() ? bottom() : other.bottom();
        return Rect{nx, ny, nr - nx, nb - ny};
    }

    /// 四周各向内收一点（传负数就是向外扩）。
    constexpr Rect inset(float dx, float dy) const noexcept {
        return Rect{x + dx, y + dy, w - 2.0f * dx, h - 2.0f * dy};
    }
};

/// 以父矩形左上角为原点摆一个子矩形。
constexpr Rect childRect(const Rect& parent, float x, float y, float w, float h) noexcept {
    return Rect{parent.x + x, parent.y + y, w, h};
}

/// 从父矩形右侧往左数第 index 个槽位（index 从 0 开始），垂直居中。
/// margin 是最右边距，gap 是槽位间距——头部菜单那排按钮就是用这个摆的。
constexpr Rect rightSlot(const Rect& parent, int index, float slotW, float slotH,
                         float gap, float margin) noexcept {
    const float slotRight =
        parent.right() - margin - slotW - static_cast<float>(index) * (slotW + gap);
    const float top = parent.y + (parent.h - slotH) * 0.5f;
    return Rect{slotRight, top, slotW, slotH};
}

}  // namespace ink
