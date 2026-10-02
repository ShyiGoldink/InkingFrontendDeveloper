#include <ink/basic/InkingDraw.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// 形状填充的实现：**把形状采样成多边形，交给 GPU 三角化**。
//
// 为什么不用逐像素：那是 CPU 光栅化，项目明令禁止（docs/AGENTS.md §3 第 3 条），
// 而且和"DrawCall 合批 + 静态烘焙"的方向相反。
//
// 为什么不用 SDL3 GPU API：那要把整个渲染后端换掉（TempTask 第 2 步的选项 a），
// 而 `SDL_RenderGeometry` 已经在现有后端上，够用且便宜（选项 b）。
//
// 抗锯齿的做法：不是多重采样，而是**边缘羽化**——最外一圈顶点 alpha=0、
// 往里 1 像素的顶点 alpha=源色 alpha，中间自然插值出一条 0→1 的过渡带。
// 实测（software 渲染器）顶点 alpha 确实参与混合、而且会插值，
// 详细数值记在 InkingDraw.h 的注释里。

namespace ink::detail {

namespace {

/**
 * 羽化带的宽度，**设备像素**。
 *
 * 1 像素是"刚好把硬台阶抹平"的量：再宽会让形状看起来发虚，
 * 再窄就盖不住多边形采样点的棱角。
 */
constexpr float kFeatherPixels = 1.0f;

constexpr float kPi = 3.14159265358979323846f;

/// 轮廓上的一个点，设备像素、相对组件左上角。
struct OutlinePoint {
    float x = 0.0f;
    float y = 0.0f;
};

/// 整圈（圆 / 椭圆）的采样段数上下限。
constexpr int kMinCircleSteps = 24;
constexpr int kMaxCircleSteps = 512;

/// 圆角矩形每段 1/4 弧的采样段数上下限。
constexpr int kMinArcSteps = 4;
constexpr int kMaxArcSteps = 64;

/**
 * 采样段数：按"每段大约 2 设备像素"估，再夹到上下限。
 *
 * 每段 2 像素是个折中：再密顶点白烧（一个按钮每帧重建顶点），
 * 再疏就有看得见的多边形棱角——1 像素的羽化带遮不住它。
 */
int StepsForLength(float pixels, int minSteps, int maxSteps) {
    const int raw = static_cast<int>(std::ceil(pixels / 2.0f));
    return std::clamp(raw, minSteps, maxSteps);
}

/**
 * 追加一段椭圆弧上的点。
 *
 * @param includeEnd 是否包含终点。圆 / 椭圆的**整圈**不包含（否则首尾重合）；
 *                   圆角矩形的每段 1/4 弧包含两端——相邻两段弧的端点就是
 *                   它们之间那段直边的两个端点，于是直边不用单独生成。
 */
void AppendArc(std::vector<OutlinePoint>& out, float cx, float cy, float rx,
               float ry, float startAngle, float endAngle, int steps,
               bool includeEnd) {
    const float safeSteps = static_cast<float>(steps > 0 ? steps : 1);
    const int count = includeEnd ? steps + 1 : steps;
    for (int i = 0; i < count; ++i) {
        const float t = startAngle
                      + (endAngle - startAngle)
                            * (static_cast<float>(i) / safeSteps);
        out.push_back(OutlinePoint{cx + std::cos(t) * rx,
                                   cy + std::sin(t) * ry});
    }
}

/**
 * @brief 生成一个形状的**外轮廓**（一圈闭合的点，顺时针）。
 *
 * 基准是 (0,0)-(w,h) 这个矩形；圆是居中的，所以"缩小后的形状"用同一套
 * 公式生成之后，只要把它平移到中心对齐，就等价于"沿法向向内缩"——
 * 这正是羽化带需要的内圈。
 *
 * **点数只由 kind 与 steps 决定，与尺寸无关**，因为外圈和内圈要按下标配对。
 * 半径退化（0）时所有弧上的点会重合在一起，那只是零面积三角形，无害。
 */
void AppendContour(std::vector<OutlinePoint>& out, const ShapeSpec& shape,
                   float w, float h, int steps) {
    switch (shape.kind) {
        case ShapeKind::RoundedRect: {
            // 半径夹取在这里就地做（float 版）：ShapeSpec::ClampRadius 收的是
            // 整数尺寸，而这里的 w/h 已经乘过展示倍率，不该再截断回整数。
            const float shorter = w < h ? w : h;
            const float r = std::clamp(shape.radius, 0.0f, shorter * 0.5f);

            // 四段 1/4 弧。相邻两段的端点连线就是四条直边，所以直边
            // 不需要单独生成点——多生成反而会和弧的端点重合。
            AppendArc(out, r, r, r, r, kPi, 1.5f * kPi, steps, true);          // 左上
            AppendArc(out, w - r, r, r, r, 1.5f * kPi, 2.0f * kPi, steps, true);  // 右上
            AppendArc(out, w - r, h - r, r, r, 0.0f, 0.5f * kPi, steps, true);  // 右下
            AppendArc(out, r, h - r, r, r, 0.5f * kPi, kPi, steps, true);      // 左下
            break;
        }

        case ShapeKind::Circle: {
            // 正圆：直径取 min(w,h)，**居中**（和 ShapeSpec 的语义一致）。
            const float diameter = w < h ? w : h;
            const float radius = diameter * 0.5f;
            AppendArc(out, w * 0.5f, h * 0.5f, radius, radius, 0.0f, 2.0f * kPi,
                      steps, false);
            break;
        }

        case ShapeKind::Ellipse: {
            AppendArc(out, w * 0.5f, h * 0.5f, w * 0.5f, h * 0.5f, 0.0f,
                      2.0f * kPi, steps, false);
            break;
        }

        case ShapeKind::Rect:
        default: {
            out.push_back(OutlinePoint{0.0f, 0.0f});
            out.push_back(OutlinePoint{w, 0.0f});
            out.push_back(OutlinePoint{w, h});
            out.push_back(OutlinePoint{0.0f, h});
            break;
        }
    }
}

/// 顶点色：rgb 取源色，alpha 单独给（羽化带的两圈就是靠它区分的）。
SDL_FColor VertexColor(std::uint32_t color, float alpha) {
    return SDL_FColor{static_cast<float>(ColorRed(color)) / 255.0f,
                      static_cast<float>(ColorGreen(color)) / 255.0f,
                      static_cast<float>(ColorBlue(color)) / 255.0f, alpha};
}

SDL_Vertex MakeVertex(float x, float y, const SDL_FColor& color) {
    SDL_Vertex vertex;
    vertex.position = SDL_FPoint{x, y};
    vertex.color = color;
    vertex.tex_coord = SDL_FPoint{0.0f, 0.0f};  // 不用纹理，但别留垃圾值
    return vertex;
}

// ---------------------------------------------------------------------------
// 复用缓冲
//
// 每个按钮每帧都要重建同一份顶点（形状其实没变），"每帧重新分配"是现在
// 最容易看见的浪费。用函数内静态量把容量留住，绘制全程在 UI 线程，不需要锁。
//
// 真正的解法是"静态形状烘一次网格"（DrawCall 合批 + 静态烘焙那一轮），
// 那时这几个缓冲会连同 RenderGeometry 一起收进提交层。
// ---------------------------------------------------------------------------

std::vector<OutlinePoint>& outerScratch() {
    static std::vector<OutlinePoint> points;
    return points;
}

std::vector<OutlinePoint>& innerScratch() {
    static std::vector<OutlinePoint> points;
    return points;
}

std::vector<SDL_Vertex>& vertexScratch() {
    static std::vector<SDL_Vertex> vertices;
    return vertices;
}

std::vector<int>& indexScratch() {
    static std::vector<int> indices;
    return indices;
}

}  // namespace

void fillShape(SDL_Renderer* renderer, const ShapeSpec& shape, int width,
               int height, float magnification, float pixelX, float pixelY,
               const TransformSpec& transform, std::uint32_t color) {
    if (renderer == nullptr || width <= 0 || height <= 0) {
        return;
    }

    const float mag = magnification > 0.0f ? magnification : 1.0f;
    const float w = static_cast<float>(width) * mag;
    const float h = static_cast<float>(height) * mag;
    if (w <= 0.0f || h <= 0.0f) {
        return;
    }

    // 只有半径还需要跟着展示倍率缩放：尺寸已经在 w/h 里了。
    ShapeSpec scaled = shape;
    scaled.radius = shape.radius * mag;

    const float shorter = w < h ? w : h;
    // 羽化带不许宽到把形状吃光：至少留一半实心区。
    const float feather = std::min(kFeatherPixels, shorter * 0.25f);

    // ---- 外轮廓与内轮廓 ----
    //
    // 羽化带**对称地跨在边界上**：外圈在形状外 half 像素处（alpha=0），
    // 内圈在形状内 half 像素处（alpha=1）。等价于标准 SDF 抗锯齿的
    // `alpha = clamp(0.5 - f, 0, 1)`（f 是到边界的带符号距离）。
    //
    // 为什么不把带子整条放在**边界内侧**：那样像素对齐的直边（面板、
    // 背景板、分割线这些最常见的矩形）最外面那一列像素会被算成覆盖一半，
    // 于是"一条本该实的边"变成半透明。对称放的话，边界正好落在像素边界上时
    // 覆盖率的判断是对的，斜边照样平滑。
    const float half = feather * 0.5f;

    // ---- 外轮廓与采样密度 ----
    int steps = kMinCircleSteps;
    switch (scaled.kind) {
        case ShapeKind::RoundedRect: {
            const float r = std::clamp(scaled.radius, 0.0f, shorter * 0.5f);
            // 每段 1/4 弧的弧长 = r * π/2。
            steps = StepsForLength(r * (kPi * 0.5f), kMinArcSteps, kMaxArcSteps);
            break;
        }
        case ShapeKind::Circle: {
            steps = StepsForLength(kPi * shorter, kMinCircleSteps,
                                   kMaxCircleSteps);
            break;
        }
        case ShapeKind::Ellipse: {
            // 椭圆周长没有初等闭式；π(a+b) 是个够用的近似（介于 2πa 与 2πb 之间）。
            steps = StepsForLength(kPi * (w + h) * 0.5f, kMinCircleSteps,
                                   kMaxCircleSteps);
            break;
        }
        case ShapeKind::Rect:
        default:
            steps = kMinCircleSteps;  // 矩形用不到，走的是 4 个角那条分支
            break;
    }

    ShapeSpec outerShape = scaled;
    if (outerShape.kind == ShapeKind::RoundedRect) {
        outerShape.radius = scaled.radius + half;
    }
    std::vector<OutlinePoint>& outer = outerScratch();
    outer.clear();
    AppendContour(outer, outerShape, w + 2.0f * half, h + 2.0f * half, steps);
    for (OutlinePoint& point : outer) {
        point.x -= half;
        point.y -= half;
    }

    ShapeSpec innerShape = scaled;
    if (innerShape.kind == ShapeKind::RoundedRect) {
        innerShape.radius = std::max(0.0f, scaled.radius - half);
    }
    const float innerW = w - 2.0f * half;
    const float innerH = h - 2.0f * half;

    std::vector<OutlinePoint>& inner = innerScratch();
    inner.clear();
    AppendContour(inner, innerShape, innerW, innerH, steps);
    // 缩小后的形状以 (0,0) 为基准，平移到与原形状同心。
    for (OutlinePoint& point : inner) {
        point.x += half;
        point.y += half;
    }

    const std::size_t count = outer.size();
    if (count < 3 || inner.size() != count) {
        return;  // 点数配对不上说明轮廓生成出了岔子，宁可不画也别画错
    }

    // ---- 顶点：外圈（alpha=0）、内圈（alpha=源色）、中心 ----
    const SDL_FColor edgeColor = VertexColor(color, 0.0f);
    const SDL_FColor solidColor = VertexColor(
        color, static_cast<float>(ColorAlpha(color)) / 255.0f);

    // 变换通道：顶点在**设备像素**空间里过一次仿射，绕的是组件中心。
    // 平移量按设计坐标写，所以先乘展示倍率；旋转与缩放跟单位无关。
    // 中心点（扇形三角化的那个点）也要过变换，否则形状会被拧歪。
    TransformSpec deviceTransform = transform;
    deviceTransform.translateX *= mag;
    deviceTransform.translateY *= mag;
    const float centerX = w * 0.5f;
    const float centerY = h * 0.5f;
    const auto transformPoint = [&](float x, float y) {
        return ApplyTransform(deviceTransform, x, y, centerX, centerY);
    };

    std::vector<SDL_Vertex>& vertices = vertexScratch();
    std::vector<int>& indices = indexScratch();
    vertices.clear();
    indices.clear();
    vertices.reserve(2 * count + 1);
    indices.reserve(9 * count);

    for (std::size_t i = 0; i < count; ++i) {
        const Point2 point = transformPoint(outer[i].x, outer[i].y);
        vertices.push_back(
            MakeVertex(pixelX + point.x, pixelY + point.y, edgeColor));
    }
    for (std::size_t i = 0; i < count; ++i) {
        const Point2 point = transformPoint(inner[i].x, inner[i].y);
        vertices.push_back(
            MakeVertex(pixelX + point.x, pixelY + point.y, solidColor));
    }
    const Point2 center = transformPoint(centerX, centerY);
    vertices.push_back(
        MakeVertex(pixelX + center.x, pixelY + center.y, solidColor));

    // ---- 索引：边缘一圈的带子 + 里面的扇形 ----
    const int centerIndex = static_cast<int>(2 * count);
    for (std::size_t i = 0; i < count; ++i) {
        const std::size_t next = (i + 1) % count;
        const int outerA = static_cast<int>(i);
        const int outerB = static_cast<int>(next);
        const int innerA = static_cast<int>(count + i);
        const int innerB = static_cast<int>(count + next);

        // 羽化带：外圈 → 内圈，两个三角形。
        indices.push_back(outerA);
        indices.push_back(outerB);
        indices.push_back(innerB);
        indices.push_back(outerA);
        indices.push_back(innerB);
        indices.push_back(innerA);

        // 实心区：中心 + 内圈相邻两点。形状都是凸的，扇形三角化成立。
        indices.push_back(centerIndex);
        indices.push_back(innerA);
        indices.push_back(innerB);
    }

    SDL_RenderGeometry(renderer, nullptr, vertices.data(),
                       static_cast<int>(vertices.size()), indices.data(),
                       static_cast<int>(indices.size()));
}

}  // namespace ink::detail
