#include "ButtonPaint.h"

#include <ink/basic/InkingDraw.h>

#include <SDL3/SDL.h>

#include <cstdint>

namespace ink::detail {

namespace {

/**
 * 配置里没写 fontSize 时用的兜底字号（设计坐标）。
 *
 * 文字真正落地（字形图集）时，这里会换成"机器默认字体在当前 DPI 下的推荐
 * 字号"；现在写死一个常量，是为了让"没配字号"不至于画出一个零高度的方块——
 * 那看起来像 bug，其实是没配。
 */
constexpr float kDefaultFontSize = 16.0f;

/**
 * 设计坐标 → 设备像素的倍率。
 *
 * 基类的 SubmitToRenderer 已经把**左上角**换算过了，但尺寸还得自己乘——
 * 这是"展示倍率不参与布局、只在绘制时换算"那条（docs/API.md「[x] 展示倍率」）。
 */
float Magnify(const InkingAnchor& node) {
    const float value = node.GetMagnification();
    return value > 0.0f ? value : 1.0f;
}

/// 文字占位块的颜色兜底：文字色纯透明时看不到，给个不透明的白。
std::uint32_t VisibleTextColor(std::uint32_t color) {
    return ColorAlpha(color) == 0u ? 0xFFFFFFFFu : color;
}

}  // namespace

void PaintButtonShape(const InkingAnchor& node, const ShapeSpec& shape,
                      const TransformSpec& transform, std::uint32_t color,
                      float pixelX, float pixelY) {
    // 三态之间的差别只体现在颜色（含 alpha）与**变换**上，形状与尺寸完全不动——
    // 这正是静态按钮能一边有动画、一边继续待在"进命中表"那一档的原因
    // （变换不改表，见 docs/InputDesign.md §11）。
    //
    // 形状怎么变成像素（三角化 + 边缘羽化 + 顶点过变换）在 `detail::fillShape` 里，
    // 静态按钮、动态按钮、以及基类那份默认矩形共用同一份实现。
    fillShape(currentRenderer(), shape, node.GetWidth(), node.GetHeight(),
              Magnify(node), pixelX, pixelY, transform, color);
}

bool HitTestButton(const InkingAnchor& node, const ShapeSpec& shape,
                   const TransformSpec& transform, float worldX, float worldY) {
    // 父链上任何一层不可见，就不该还能被点到。
    if (!node.IsVisibleInTree()) {
        return false;
    }

    // ① 世界坐标 → 相对组件左上角（设计坐标）。和渲染提交时用的是同一套推导
    //    （`SubmitToRenderer` 给的左上角就是 `GetAbsX/Y`），所以整体缩放
    //    不需要重烘任何东西（docs/InputDesign.md §2 第 3 条）。
    const float localX = worldX - node.GetAbsX();
    const float localY = worldY - node.GetAbsY();

    // ② 变换通道：过一次**逆**变换。绘制那边过的是正变换（`fillShape` 里
    //    `ApplyTransform`），这里必须是它的严格逆，且绕同一个中心。
    const Point2 local = InverseTransform(
        transform, localX, localY,
        static_cast<float>(node.GetWidth()) * 0.5f,
        static_cast<float>(node.GetHeight()) * 0.5f);

    // ③ 形状是唯一真相：渲染、命中、AABB 都从 GetShape() 派生。
    return ShapeContains(shape, local.x, local.y, node.GetWidth(),
                         node.GetHeight());
}

ShapeBounds ButtonHitEnvelope(const ButtonData& data, const ShapeSpec& shape,
                              int width, int height) {
    // 形状本身的 AABB：圆和椭圆不是整块矩形（`GetShapeBounds` 已经算准了）。
    const ShapeBounds local = GetShapeBounds(shape, width, height);

    // 绕组件中心做变换：和绘制 / 命中用的是同一个原点
    // （`InkingTransform.h` 里定的"原点取组件中心"）。
    const float centerX = static_cast<float>(width) * 0.5f;
    const float centerY = static_cast<float>(height) * 0.5f;

    const auto transformedBounds = [&](const TransformSpec& transform) {
        // 四个角各过一次正变换，再取外接 AABB。旋转后的包围盒会比原来的大——
        // 这正是"保守"的含义：宁可多登记几个格子，也不能漏。
        const float xs[4] = {local.x, local.x + local.width, local.x,
                             local.x + local.width};
        const float ys[4] = {local.y, local.y, local.y + local.height,
                             local.y + local.height};

        float minX = 0.0f;
        float minY = 0.0f;
        float maxX = 0.0f;
        float maxY = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const Point2 point = ApplyTransform(transform, xs[i], ys[i],
                                                centerX, centerY);
            if (i == 0) {
                minX = maxX = point.x;
                minY = maxY = point.y;
                continue;
            }
            minX = point.x < minX ? point.x : minX;
            minY = point.y < minY ? point.y : minY;
            maxX = point.x > maxX ? point.x : maxX;
            maxY = point.y > maxY ? point.y : maxY;
        }
        return ShapeBounds{minX, minY, maxX - minX, maxY - minY};
    };

    // **三个状态都要算**：悬停会平移+放大、按下会下沉，包络得把它们全盖住。
    // 只按当前变换登记的话，"悬停那一帧按钮挪出去一点"边缘的点就落在表外了
    // ——那是漏命中，属于最难查的那种（偶尔点不中）。
    const TransformSpec* states[3] = {&data.normal.transform,
                                      &data.hover.transform,
                                      &data.onclicked.transform};

    ShapeBounds merged = transformedBounds(*states[0]);
    for (int i = 1; i < 3; ++i) {
        const ShapeBounds one = transformedBounds(*states[i]);
        const float maxX = merged.x + merged.width;
        const float maxY = merged.y + merged.height;
        const float oneMaxX = one.x + one.width;
        const float oneMaxY = one.y + one.height;

        const float minX = one.x < merged.x ? one.x : merged.x;
        const float minY = one.y < merged.y ? one.y : merged.y;
        const float outMaxX = oneMaxX > maxX ? oneMaxX : maxX;
        const float outMaxY = oneMaxY > maxY ? oneMaxY : maxY;
        merged = ShapeBounds{minX, minY, outMaxX - minX, outMaxY - minY};
    }
    return merged;
}

void PaintButtonText(const InkingAnchor& node, const ButtonData& data,
                     float pixelX, float pixelY) {
    SDL_Renderer* renderer = currentRenderer();
    if (renderer == nullptr) {
        return;
    }

    // **已知限制**：文字占位块不跟着 `transform` 走（按钮转了、方块还在原地）。
    // 原因是它现在用 `SDL_RenderFillRect` 画，而 FillRect 只能画轴对齐矩形。
    // 等字形图集 + 纹理层接上，文字会走"形状填充 + 顶点变换"这条路，
    // 那时它自然跟着转；现在为一个占位方块单独铺一套变换不划算
    // （示例配置里也没人配文字）。

    const float scale = Magnify(node);
    const float fontSize =
        data.text.fontSize > 0.0f ? data.text.fontSize : kDefaultFontSize;

    // 文字块的左上角：配置里给的是**距组件左上角的间距**，不是坐标。
    // 宽度按"字号 × 字数"估一个，够把区域钉住；真正的字形推进要等图集。
    const float textX = pixelX + data.text.leftSpace * scale;
    const float textY = pixelY + data.text.topSpace * scale;
    const float textWidth =
        fontSize * scale * static_cast<float>(data.label.size());
    const float textHeight = fontSize * scale;

    const SDL_FRect rect{textX, textY, textWidth, textHeight};
    setDrawColor(renderer, VisibleTextColor(data.textColor));
    SDL_RenderFillRect(renderer, &rect);
}

}  // namespace ink::detail
