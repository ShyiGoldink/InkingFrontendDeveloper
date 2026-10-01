#pragma once

// 形状层：SDF（带符号距离场）。
//
// 符号用**通用约定**（CXXCSS.md §3.4）：
//   f < 0 在内部、f = 0 在边界、f > 0 在外部；
//   裁剪 = 保留 f <= 0 的区域。
// 这样公开的 SDF 参考实现可以照着抄，不用逐个翻符号；抗锯齿也是标准写法
// `smoothstep(-aa, aa, d)`；`abs(d)` 天然就是"到边界的距离"。
//
// ---------------------------------------------------------------------------
// 一条硬约定：**形状同时决定渲染 / 命中 / AABB，三者从这一份定义派生**
//
// 各写一遍就会出"画的是圆角、点到的是直角"——两边单独看都对，合起来才错，
// 而且极难排查（CXXCSS.md §3）。所以本文件只提供三个派生量，且都从这里算：
//   SignedDistance   → 渲染（填充 / 描边 / 裁剪，形状层接手后）
//   Contains         → 命中（这个点在不在形状里，现在就能用）
//   GetBounds        → AABB（将来塞进命中表的哪个格子）
//
// ---------------------------------------------------------------------------
// 坐标系与形状语义（都以组件左上角为原点、单位是设计坐标）
//
//   rect         用满 width × height
//   roundedRect  用满 width × height，四角半径 radius（夹到 min(w,h)/2）
//   circle       正圆，直径取 min(w,h)，**居中**
//   ellipse      椭圆，用满 width × height
//
// 注意 circle 是居中的：一个 100×60 的组件画圆，得到的是居中、直径 60 的圆，
// 而不是一个 100×60 的"椭圆"。要椭圆就写 ellipse。

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>

namespace ink {

/// 形状的种类。放一个显式的整数而不是直接塞 variant：
/// 生成器是按 `type` 查表展开的，运行期只需要"这一个是哪一种"。
enum class ShapeKind : std::uint8_t {
    Rect = 0,
    RoundedRect,
    Circle,
    Ellipse,
};

/**
 * @brief 一个形状的定义：种类 + 只属于它的那几个参数。
 *
 * 参数**不编进字符串**（CXXCSS.md §3.1）。配置里是
 * `{"type":"roundedRect","radius":10}`，生成器在编译期把它展开成本结构。
 *
 * 用工厂函数造，别手动填字段：夹取和校验都在工厂里做了。
 */
struct ShapeSpec {
    ShapeKind kind = ShapeKind::Rect;

    /// 只对 RoundedRect 有意义；一律已夹到 [0, min(w,h)/2]（见 ClampRadius）。
    float radius = 0.0f;

    /// 直角矩形（= 不写 shape），尺寸完全来自 width / height。
    static constexpr ShapeSpec Rect() { return ShapeSpec{}; }

    /// 圆角矩形。radius < 0 是配置错误，这里断言挡住；过大则夹到上限。
    static ShapeSpec RoundedRect(float radius) {
        ShapeSpec spec;
        spec.kind = ShapeKind::RoundedRect;
        // 负半径没有意义，而且会让四个角"翻出去"长成另一个形状——
        // CXXCSS.md §3.6 要求报错停下，这里用断言当场拦（Debug 构建）。
        assert(radius >= 0.0f && "shape:radius 不能为负");
        spec.radius = radius < 0.0f ? 0.0f : radius;
        return spec;
    }

    /// 正圆：直径取 min(width,height)，居中。
    static constexpr ShapeSpec Circle() {
        ShapeSpec spec;
        spec.kind = ShapeKind::Circle;
        return spec;
    }

    /// 椭圆：用满 width × height。
    static constexpr ShapeSpec Ellipse() {
        ShapeSpec spec;
        spec.kind = ShapeKind::Ellipse;
        return spec;
    }

    /**
     * 半径的夹取：上限是 min(width,height)/2。
     *
     * 这是**形状本身的数学要求**（超过一半就画不成圆角矩形了），
     * 属于 CXXCSS.md §3.6 里唯一允许自动修正的那一类——不是配置错误，
     * 所以夹取而不是报错。
     */
    static float ClampRadius(float radius, int width, int height) {
        const float shorter =
            static_cast<float>(width < height ? width : height);
        const float limit = shorter * 0.5f;
        if (radius < 0.0f) {
            return 0.0f;
        }
        return radius > limit ? limit : radius;
    }

    /** 夹取之后真正生效的半径（宽或高有一边是 0 时就是 0）。 */
    float EffectiveRadius(int width, int height) const {
        return ClampRadius(radius, width, height);
    }
};

/**
 * @brief 点的带符号距离，单位是设计坐标。
 * @param x,y 相对组件左上角的坐标
 * @return f < 0 在内部、f = 0 在边界、f > 0 在外部
 */
inline float SignedDistance(const ShapeSpec& shape, float x, float y, int width,
                            int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case ShapeKind::RoundedRect: {
            // 半径先夹取（形状自己的数学要求），再按"内缩矩形 + 到角心的距离"算。
            //
            // 关键在最后减的那一项：
            //   - 边上（内缩矩形之外、但只有一个方向越界）到的是**边线**，
            //     已经是真距离，不用再减半径；
            //   - 角上到的是**角心**，要减掉半径才是到圆弧的距离。
            // 所以减的是 `min(max(qx,qy), 0)`：只有"角上"这个分量才非零。
            // 直接在结果上减 r 会把四条直边也算短 r——圆角矩形的中心到上边
            // 就是 h/2，不是 h/2 - r。
            const float r = shape.EffectiveRadius(width, height);
            const float halfW = w * 0.5f;
            const float halfH = h * 0.5f;
            const float innerW = halfW - r;
            const float innerH = halfH - r;

            // 把点搬到"内缩矩形的角"为原点的象限里，取绝对值即可复用一份公式。
            const float qx = std::fabs(x - halfW) - innerW;
            const float qy = std::fabs(y - halfH) - innerH;

            const float outsideX = qx > 0.0f ? qx : 0.0f;
            const float outsideY = qy > 0.0f ? qy : 0.0f;
            const float outside =
                std::sqrt(outsideX * outsideX + outsideY * outsideY);

            // 内部时 max(qx,qy) 为负，就是到最近边线的距离。
            const float nearer = qx > qy ? qx : qy;
            const float inside = nearer < 0.0f ? nearer : 0.0f;

            // 只有"两个方向都越界"（真在角上）时才还要减半径。
            return outside + inside - (nearer < 0.0f ? 0.0f : r);
        }

        case ShapeKind::Circle: {
            // 居中、直径取 min(w,h)：所以半径是 min(w,h)/2，圆心是 (w/2, h/2)。
            const float r = (w < h ? w : h) * 0.5f;
            const float dx = x - w * 0.5f;
            const float dy = y - h * 0.5f;
            return std::sqrt(dx * dx + dy * dy) - r;
        }

        case ShapeKind::Ellipse: {
            // 椭圆的 SDF 没有精确闭式（真值要解数值最近点），
            // 但这里要的是**符号正确的近似**：内外判定精确，
            // 距离在边缘附近也够准。
            //
            // 写法：在归一化空间算到单位圆的距离，再乘回最小半径——
            // 于是内部为负、外部为正、边界为 0，`Contains` 永远正确。
            const float rx = w * 0.5f;
            const float ry = h * 0.5f;
            if (rx <= 0.0f || ry <= 0.0f) {
                const float dx = rx <= 0.0f ? std::fabs(x) : 0.0f;
                const float dy = ry <= 0.0f ? std::fabs(y) : 0.0f;
                return dx + dy;  // 退化成一条线 / 一个点，只有它本身算"在内"
            }
            const float k0 = std::hypot((x - rx) / rx, (y - ry) / ry);
            const float k1 = std::hypot((x - rx) / (rx * rx),
                                        (y - ry) / (ry * ry));
            const float scale = rx < ry ? rx : ry;
            if (k1 <= 0.0f) {
                return -scale;
            }
            return (k0 * (k0 - 1.0f) / k1) * scale;
        }

        case ShapeKind::Rect:
        default: {
            // 直角矩形：把点按两条中轴线折叠到右下象限，
            // 横向越界量 qx、纵向越界量 qy 都 >= 0。
            const float qx = std::fabs(x - w * 0.5f) - w * 0.5f;
            const float qy = std::fabs(y - h * 0.5f) - h * 0.5f;

            // 角上：到角顶点的距离（两个都为正）。
            const float outsideX = qx > 0.0f ? qx : 0.0f;
            const float outsideY = qy > 0.0f ? qy : 0.0f;
            const float outside =
                std::sqrt(outsideX * outsideX + outsideY * outsideY);

            // 内部：两个都是负数，取**较大**的那个（离边界更近）当负距离。
            const float inside =
                (qx > qy ? qx : qy) < 0.0f ? (qx > qy ? qx : qy) : 0.0f;
            return outside + inside;
        }
    }
}

/**
 * @brief 这个点在不在形状里（命中判定）。
 *
 * 与 `SignedDistance` 同源：判据就是 `f <= 0`，和裁剪保留的区域完全一致，
 * 不会出现"画的是圆角、点到的是直角"。
 */
inline bool ShapeContains(const ShapeSpec& shape, float x, float y, int width,
                          int height) {
    return SignedDistance(shape, x, y, width, height) <= 0.0f;
}

/**
 * @brief 形状的包围盒（AABB），相对组件左上角。
 *
 * 将来命中表按格子索引时就问这个：半径不同、形状不同，包围盒也可能不同
 * （circle 只占中间一块），所以不能一律拿 width × height 顶上。
 */
struct ShapeBounds {
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

inline ShapeBounds GetShapeBounds(const ShapeSpec& shape, int width,
                                  int height) {
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);

    switch (shape.kind) {
        case ShapeKind::Circle: {
            // 居中 + 直径 min(w,h)：宽高不等时两侧会留白，包围盒也跟着小。
            const float diameter = w < h ? w : h;
            return ShapeBounds{(w - diameter) * 0.5f, (h - diameter) * 0.5f,
                               diameter, diameter};
        }
        case ShapeKind::RoundedRect:
        case ShapeKind::Ellipse:
        case ShapeKind::Rect:
        default:
            // 这三种都用满整个矩形（圆角只是把四角磨掉，不影响包围盒）。
            return ShapeBounds{0.0f, 0.0f, w, h};
    }
}

}  // namespace ink
