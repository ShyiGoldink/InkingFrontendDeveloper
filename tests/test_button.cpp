// Button 组件自检：形状层（SDF / 命中 / 包围盒）、颜色换算、ButtonLibrary、
// 三态状态机、点击回调，最后用**离屏渲染 + 读回像素**确认三态真的画出了差别。
//
// 为什么渲染那部分要离屏：开窗截图会被别的窗口遮挡，结果随桌面状态漂移
// （AGENTS §6 第 20 条）。离屏走的是同一条提交路径
// （SceneLibrary::RenderScene → InkingScene::Render → onRender → SDL_RenderFillRect），
// 但结果完全由自己掌控。
//
// 非 0 退出码表示有检查项失败。

#include <ink/ink.h>
#include <ink/basic/InkingAnchor.h>
#include <ink/dataStruct/InkingColor.h>
#include <ink/dataStruct/InkingShapeSpec.h>
#include <button/ButtonData.h>
#include <button/ButtonLibrary.h>
#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailed = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s %s\n", condition ? "[通过]" : "[失败]", what.c_str());
    if (!condition) {
        ++gFailed;
    }
}

/** 浮点比较：形状的判定结果是算出来的，不能直接 == 。 */
bool near(float a, float b, float epsilon = 0.01f) {
    return std::fabs(a - b) <= epsilon;
}

std::string toHex(std::uint32_t color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", color);
    return buffer;
}

// ---------------------------------------------------------------------------
// 一组"照着 CXXCSS/Button/button.example.json 填出来"的测试数据
//
// 用的就是示例里那三态：0.75 / 0.85 / 0.75 的黑色 + 圆角 10。
// 故意用示例的值：示例改了、这条自检跟着红，才能发现"文档与实现分叉"。
// ---------------------------------------------------------------------------

constexpr float kAlphaNormal = 0.75f;
constexpr float kAlphaHover = 0.85f;

ink::ButtonData exampleButton() {
    ink::ButtonData data;
    data.name = "button.example";
    data.width = 200;
    data.height = 100;
    data.normal = ink::ButtonAppearance::FromUnitRgba(0.0f, 0.0f, 0.0f,
                                                      kAlphaNormal);
    data.hover = ink::ButtonAppearance::FromUnitRgba(0.0f, 0.0f, 0.0f,
                                                     kAlphaHover);
    data.onclicked = ink::ButtonAppearance::FromUnitRgba(0.0f, 0.0f, 0.0f,
                                                         kAlphaNormal);
    data.selfAnchor = ink::Anchor{0.5f, 0.5f};
    data.traceAnchor = ink::Anchor{0.0f, 0.0f};
    // 三态是**分别显式写了**的，所以关掉"省略即继承"。不关的话
    // Normalize() 会用 normal 把 hover / onclicked 覆盖掉——
    // 而示例里 hover 的透明度本来就是不一样的（0.85 vs 0.75）。
    data.hoverInheritsNormal = false;
    data.onclickedInheritsNormal = false;
    data.shape = ink::ShapeSpec::RoundedRect(10.0f);
    return data;
}

// ---------------------------------------------------------------------------
// 1. 颜色：0~1 的配置写法 → 0xAARRGGBB
// ---------------------------------------------------------------------------

void testColor() {
    std::printf("\n== 颜色换算 ==\n");

    const std::uint32_t black75 = ink::FromUnitRgba(0.0f, 0.0f, 0.0f, 0.75f);
    check(near(ink::byteChannelToUnit(ink::ColorAlpha(black75)), 0.75f),
          "0.75 的 alpha 能往返（实得 "
              + std::to_string(ink::byteChannelToUnit(ink::ColorAlpha(black75)))
              + "）");
    check(ink::ColorAlpha(black75) == 191, "0.75 → 191（0.75×255 四舍五入）");
    check(ink::ColorRed(black75) == 0 && ink::ColorGreen(black75) == 0
              && ink::ColorBlue(black75) == 0,
          "黑色三通道都是 0");

    const std::uint32_t white = ink::FromUnitRgba(1.0f, 1.0f, 1.0f, 1.0f);
    check(white == 0xFFFFFFFFu, "1,1,1,1 → 0xFFFFFFFF");

    // 超出 [0,1] 的写法要被夹住，而不是溢出成一个怪颜色。
    const std::uint32_t clamped = ink::FromUnitRgba(2.0f, -1.0f, 0.5f, 1.0f);
    check(ink::ColorRed(clamped) == 255 && ink::ColorGreen(clamped) == 0,
          "超出 [0,1] 的通道被夹住，不是溢出");

    check(ink::MakeColor(0x56, 0x9C, 0xD6, 0xFF) == 0xFF569CD6u,
          "MakeColor 的通道顺序是 AARRGGBB");
}

// ---------------------------------------------------------------------------
// 2. 形状层：SDF 符号、命中、包围盒、半径夹取
// ---------------------------------------------------------------------------

void testShape() {
    std::printf("\n== 形状层（SDF / 命中 / 包围盒）==\n");

    const int width = 200;
    const int height = 100;

    // ---- 直角矩形 ----
    {
        const ink::ShapeSpec rect = ink::ShapeSpec::Rect();
        check(ink::SignedDistance(rect, 100.0f, 50.0f, width, height) < 0.0f,
              "rect：中心 f < 0（负 = 内）");
        check(near(ink::SignedDistance(rect, 0.0f, 50.0f, width, height), 0.0f),
              "rect：左边线上 f = 0");
        check(near(ink::SignedDistance(rect, 100.0f, 0.0f, width, height), 0.0f),
              "rect：上边线上 f = 0");
        // (120,50) 在**内部**：到最近边线（下边 y=100）是 50，所以 f = -50。
        // 别按"离右边 80"去算成 +80——SDF 取的是到最近边界的距离。
        check(near(ink::SignedDistance(rect, 120.0f, 50.0f, width, height),
                   -50.0f),
              "rect：内部点到最近边线是负距离（-50，不是到右边的 80）");
        // 外部点：右下角外侧 (220,120)，横竖各越界 20，f = 20√2。
        check(near(ink::SignedDistance(rect, 220.0f, 120.0f, width, height),
                   28.284f, 0.01f),
              "rect：外部角点到角顶点是正距离（20√2）");
        check(near(ink::SignedDistance(rect, 220.0f, 50.0f, width, height),
                   20.0f),
              "rect：正右边外侧 20px，f = +20");
        check(ink::ShapeContains(rect, 199.0f, 99.0f, width, height),
              "rect：内部靠边的点算命中");
        check(!ink::ShapeContains(rect, -0.5f, 50.0f, width, height),
              "rect：左外侧不算命中");
    }

    // ---- 圆角矩形：最能说明"形状真的在判"的一组 ----
    {
        const ink::ShapeSpec rounded = ink::ShapeSpec::RoundedRect(10.0f);
        // 中心到最近边线 = h/2 - r = 50 - 10 = 40。圆角不吃掉直边，
        // 但它确实是"到边界"的距离——所以是 40 而不是 50。
        check(near(ink::SignedDistance(rounded, 100.0f, 50.0f, width, height),
                   -40.0f),
              "roundedRect：中心到最近边线是 -40（h/2 - r）");
        check(near(ink::SignedDistance(rounded, 100.0f, 0.0f, width, height),
                   0.0f),
              "roundedRect：上边中点仍是边界（圆角只磨四个角）");
        check(ink::ShapeContains(rounded, 100.0f, 2.0f, width, height),
              "roundedRect：上边中点算命中");

        // 左上角 (1,1)：到角心 (10,10) 的距离是 9√2 ≈ 12.728，减半径 10 →
        // 约 +2.728，所以在**外面**——这正是"点到的是圆角而不是直角"。
        const float corner =
            ink::SignedDistance(rounded, 1.0f, 1.0f, width, height);
        check(near(corner, 2.728f, 0.01f),
              "roundedRect：左上角外一点 f ≈ +2.73（实得 "
                  + std::to_string(corner) + "）");
        check(!ink::ShapeContains(rounded, 1.0f, 1.0f, width, height),
              "roundedRect：被磨掉的角不算命中（直角矩形会算）");
        check(ink::ShapeContains(ink::ShapeSpec::Rect(), 1.0f, 1.0f, width,
                                 height),
              "同一个点，直角矩形算命中——差异只来自形状定义");
        check(ink::ShapeContains(rounded, 10.0f, 10.0f, width, height),
              "roundedRect：角心处算命中");
    }

    // ---- 半径夹取：min(w,h)/2 是上限，这是形状自己的数学要求 ----
    {
        const ink::ShapeSpec huge = ink::ShapeSpec::RoundedRect(500.0f);
        check(near(huge.EffectiveRadius(width, height), 50.0f),
              "radius 超过 min(w,h)/2 时夹到 50");
        check(near(ink::ShapeSpec::ClampRadius(-3.0f, width, height), 0.0f),
              "负半径夹到 0（配置错误另有断言拦）");
        // 夹到上限之后，形状退化成胶囊：左右两端中点应该正好在边界上。
        check(near(ink::SignedDistance(huge, 0.0f, 50.0f, width, height), 0.0f),
              "夹到上限后左边界中点仍是 f = 0");
    }

    // ---- 圆与椭圆 ----
    {
        const ink::ShapeSpec circle = ink::ShapeSpec::Circle();
        check(near(ink::SignedDistance(circle, 100.0f, 50.0f, width, height),
                   -50.0f),
              "circle：直径取 min(w,h)=100，圆心的 f = -50");
        check(!ink::ShapeContains(circle, 5.0f, 50.0f, width, height),
              "circle：200×100 里左端 5px 处在圆外（圆是居中、直径 100）");
        check(ink::ShapeContains(circle, 55.0f, 50.0f, width, height),
              "circle：圆内 5px 处算命中");

        const ink::ShapeBounds circleBounds =
            ink::GetShapeBounds(circle, width, height);
        check(near(circleBounds.x, 50.0f) && near(circleBounds.width, 100.0f),
              "circle：包围盒是居中的 100×100，不是整个矩形");

        const ink::ShapeSpec ellipse = ink::ShapeSpec::Ellipse();
        check(ink::SignedDistance(ellipse, 100.0f, 50.0f, width, height) < 0.0f,
              "ellipse：中心 f < 0");
        check(ink::ShapeContains(ellipse, 5.0f, 50.0f, width, height),
              "ellipse：用满整个矩形，左端 5px 处在椭圆内");
        check(!ink::ShapeContains(ellipse, 5.0f, 5.0f, width, height),
              "ellipse：左上角落不算命中");
        // 边界方向：长轴端点在边界上，短轴端点在边界上。
        check(near(ink::SignedDistance(ellipse, 0.0f, 50.0f, width, height),
                   0.0f, 0.01f),
              "ellipse：长轴端点在边界上（f ≈ 0）");
        check(near(ink::SignedDistance(ellipse, 100.0f, 0.0f, width, height),
                   0.0f, 0.01f),
              "ellipse：短轴端点在边界上（f ≈ 0）");
    }

    // ---- 尺寸为 0 不能崩、也不能命中 ----
    {
        const ink::ShapeSpec rounded = ink::ShapeSpec::RoundedRect(10.0f);
        const float distance = ink::SignedDistance(rounded, 0.0f, 0.0f, 0, 0);
        check(distance >= 0.0f, "0×0 的圆角矩形退化成点，只有它自己算在内");
        check(!ink::ShapeContains(rounded, 1.0f, 1.0f, 0, 0),
              "0×0 时任何点都不命中");
    }
}

// ---------------------------------------------------------------------------
// 3. ButtonLibrary：名字 → 配置
// ---------------------------------------------------------------------------

void testLibrary() {
    std::printf("\n== ButtonLibrary ==\n");

    ink::ButtonLibrary::ClearForTest();
    check(ink::ButtonLibrary::Count() == 0, "自检开始时表是空的");

    check(ink::ButtonLibrary::Register(ink::ButtonData{})
              == ink::ButtonLibrary::RegisterResult::Invalid,
          "空名字登记被驳回（Invalid）");

    const ink::ButtonData data = exampleButton();
    check(ink::ButtonLibrary::Register(data)
              == ink::ButtonLibrary::RegisterResult::Ok,
          "登记 button.example 成功");

    // 只写了 normal 的那两个字段继承规则：这里 hover/onclicked 是显式写的，
    // 所以不该被 normal 覆盖。
    const ink::ButtonData* found = ink::ButtonLibrary::Find("button.example");
    check(found != nullptr, "按名字查得到");
    if (found != nullptr) {
        check(ink::ColorAlpha(found->normal.color) == 191,
              "normal 的 alpha 是 191（0.75）");
        check(ink::ColorAlpha(found->hover.color) == 217,
              "显式写了 hover 时不会被 normal 覆盖（0.85 → 217）");
        check(found->shape.kind == ink::ShapeKind::RoundedRect,
              "形状跟着配置走（roundedRect）");
    }

    check(ink::ButtonLibrary::Find("没登记过的名字") == nullptr,
          "查不到时返回 nullptr");
    check(ink::ButtonLibrary::IsNameTaken("button.example"),
          "IsNameTaken 认得出已登记的名字");

    // 同名驳回：不覆盖，第一个还在。
    ink::ButtonData other = exampleButton();
    other.width = 999;
    check(ink::ButtonLibrary::Register(other)
              == ink::ButtonLibrary::RegisterResult::NameTaken,
          "同名登记被驳回（NameTaken）");
    check(ink::ButtonLibrary::Find("button.example")->width == 200,
          "驳回之后先登记的那份没被改掉");

    check(ink::ButtonLibrary::Count() == 1, "表里只有一项");
    const std::vector<std::string> names = ink::ButtonLibrary::GetAllNames();
    check(names.size() == 1 && names.front() == "button.example",
          "GetAllNames 给出登记过的名字");

    // ---- 批次登记：生成器生成的 RegisterAllButtons 就是靠这对入口做到幂等的 ----
    {
        // 批次内登记走的是 Register，所以先要一张干净的表（上面那些用例
        // 已经把 button.example 登记过了）。
        ink::ButtonLibrary::ClearForTest();

        // 自己塞一个"别人的"条目：批次入口绝不能把它清掉。
        ink::ButtonData outsider;
        outsider.name = "outsider.button";
        outsider.width = 10;
        outsider.height = 10;
        check(ink::ButtonLibrary::Register(outsider)
                  == ink::ButtonLibrary::RegisterResult::Ok,
              "先塞一个批次外的条目");

        ink::ButtonLibrary::BeginRegistrationBatch();
        check(ink::ButtonLibrary::Register(data)
                  == ink::ButtonLibrary::RegisterResult::Ok,
              "批次内登记成功");
        ink::ButtonLibrary::EndRegistrationBatch();

        // 再来一轮：上一批会被撤下，所以同一个名字还能再登记。
        ink::ButtonLibrary::BeginRegistrationBatch();
        check(ink::ButtonLibrary::Register(data)
                  == ink::ButtonLibrary::RegisterResult::Ok,
              "重复走一轮批次不会撞 NameTaken");
        ink::ButtonLibrary::EndRegistrationBatch();

        check(ink::ButtonLibrary::IsNameTaken("outsider.button"),
              "批次登记不会碰别人登记的条目");
        check(ink::ButtonLibrary::Count() == 2, "两轮批次之后表里还是 2 项");
    }

    // 省略 hover / onclicked 时继承 normal —— 入库前就该补齐。
    ink::ButtonLibrary::ClearForTest();
    ink::ButtonData inherited;
    inherited.name = "button.inherit";
    inherited.width = 10;
    inherited.height = 10;
    inherited.normal = ink::ButtonAppearance::FromUnitRgba(1.0f, 0.0f, 0.0f,
                                                          1.0f);
    check(ink::ButtonLibrary::Register(inherited)
              == ink::ButtonLibrary::RegisterResult::Ok,
          "登记只有 normal 的配置");
    const ink::ButtonData* inheritedFound =
        ink::ButtonLibrary::Find("button.inherit");
    check(inheritedFound != nullptr
              && inheritedFound->hover.color == inheritedFound->normal.color
              && inheritedFound->onclicked.color
                     == inheritedFound->normal.color,
          "省略 hover / onclicked 时继承 normal（入库时已补齐）");
}

// ---------------------------------------------------------------------------
// 4. 组件：getter、三态状态机、点击回调
// ---------------------------------------------------------------------------

void testButtonState() {
    std::printf("\n== 组件：三态与回调 ==\n");

    ink::ButtonLibrary::ClearForTest();
    ink::ButtonLibrary::Register(exampleButton());

    // 还没接进任何场景的"孤儿"按钮：只验状态机，不涉及渲染。
    ink::InkingStaticButton button(nullptr, "button.example");

    check(button.GetName() == "button.example", "名字来自配置");
    check(button.GetWidth() == 200 && button.GetHeight() == 100,
          "尺寸来自配置（200×100）");
    check(button.GetState() == ink::ButtonState::Normal, "初始状态是 Normal");
    check(button.GetShape().kind == ink::ShapeKind::RoundedRect
              && near(button.GetShape().radius, 10.0f),
          "形状来自配置（圆角 10）");
    check(button.GetAppearance().color == button.GetData().normal.color,
          "初始外观就是 normal 那一份");
    check(!button.IsDirty(), "构造完成后没有被标脏");
    check(button.IsVisible(), "默认可见");

    // ---- 状态机 ----
    check(!button.MouseHover(false), "重复进入同一状态不算变化");
    check(button.MouseHover(true), "悬停：状态变了");
    check(button.GetState() == ink::ButtonState::Hover, "现在在 Hover");
    check(button.GetAppearance().color == button.GetAppearance(ink::ButtonState::Hover).color,
          "悬停时取到的是 hover 外观");

    check(button.MousePress(true), "按下：状态变了");
    check(button.GetState() == ink::ButtonState::Pressed, "现在在 Pressed");

    // 拖拽：按着不放滑出去，仍然是按下态。
    check(!button.MouseHover(false), "按下期间滑出：三态没变");
    check(button.GetState() == ink::ButtonState::Pressed,
          "按着不放滑出去，仍然是 Pressed");

    check(button.MousePress(false), "抬起：状态变了");
    check(button.GetState() == ink::ButtonState::Normal,
          "抬起时指针已经不在按钮里 → 回到 Normal");

    // 再来一次，这次抬起时指针还在里面。
    button.MouseHover(true);
    button.MousePress(true);
    check(button.MouseRelease(), "抬起（MouseRelease 等价于 MousePress(false)）");
    check(button.GetState() == ink::ButtonState::Hover,
          "抬起时指针还在按钮里 → 回到 Hover");

    // 状态切换不该标脏：颜色不属于会标脏的四件事。
    check(!button.IsDirty(), "换三态不标脏（颜色不标脏）");

    // ---- 点击回调 ----
    check(!button.HasOnClicked(), "还没挂回调");
    check(!button.TriggerClick(), "没有回调时 TriggerClick 是空操作");

    int clickCount = 0;
    button.SetOnClicked([&clickCount] { ++clickCount; });
    check(button.HasOnClicked(), "回调挂上了");
    check(button.TriggerClick(), "TriggerClick 调用了回调");
    check(clickCount == 1, "回调真的跑了一次");
    button.TriggerClick();
    check(clickCount == 2, "可以重复触发");

    // ---- 文字：运行期可改，不标脏 ----
    check(button.GetText().empty(), "配置里没写文字时是空的");
    button.SetText("确定");
    check(button.GetText() == "确定", "SetText 生效（多语言该走这条路）");
    check(!button.IsDirty(), "改文字不标脏");
}

// ---------------------------------------------------------------------------
// 5. 按名字构造：取不到时退回默认值，但名字与几何都还在
// ---------------------------------------------------------------------------

void testNameConstruction() {
    std::printf("\n== 按名字构造 ==\n");

    ink::ButtonLibrary::ClearForTest();
    ink::ButtonLibrary::Register(exampleButton());

    ink::InkingStaticButton fromLibrary(nullptr, "button.example");
    check(fromLibrary.GetWidth() == 200 && fromLibrary.GetHeight() == 100,
          "按名字构造时几何取自库里的配置");
    check(fromLibrary.GetShape().kind == ink::ShapeKind::RoundedRect,
          "按名字构造时形状取自库里的配置");

    ink::InkingStaticButton unknown(nullptr, "没登记过的名字");
    check(unknown.GetName() == "没登记过的名字", "取不到配置时名字仍然记下来");
    check(unknown.GetWidth() == 0 && unknown.GetHeight() == 0,
          "取不到配置时几何是 0（并会打一条警告）");
    check(unknown.GetState() == ink::ButtonState::Normal,
          "取不到配置时状态机照常工作");
}

// ---------------------------------------------------------------------------
// 6. 离屏渲染：三态真的画出差别了吗；圆角区域真的没画吗
// ---------------------------------------------------------------------------

constexpr int kTargetWidth = 1920;
constexpr int kTargetHeight = 1080;

class ButtonProbeScene : public ink::InkingScene {
public:
    ButtonProbeScene() : ink::InkingScene("ButtonProbe") {}
};

/** 把离屏目标读成 0xAARRGGBB 的紧凑缓冲（和 render_probe 同一套读法）。 */
bool readPixelsAsArgb(SDL_Renderer* renderer, std::vector<std::uint32_t>& out) {
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr) {
        return false;
    }

    bool ok = false;
    if (SDL_LockSurface(surface)) {
        const SDL_PixelFormat format = surface->format;
        const int bytesPerPixel = SDL_BYTESPERPIXEL(format);
        const SDL_PixelFormatDetails* details =
            SDL_GetPixelFormatDetails(format);
        if (details == nullptr || bytesPerPixel <= 0 || bytesPerPixel > 4) {
            SDL_UnlockSurface(surface);
            SDL_DestroySurface(surface);
            return false;
        }

        out.assign(static_cast<std::size_t>(kTargetWidth) * kTargetHeight, 0u);
        for (int y = 0; y < kTargetHeight && y < surface->h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(surface->pixels)
                            + static_cast<std::size_t>(y) * surface->pitch;
            for (int x = 0; x < kTargetWidth && x < surface->w; ++x) {
                const std::uint8_t* pixel = row
                    + static_cast<std::size_t>(x) * bytesPerPixel;

                std::uint32_t value = 0;
                for (int i = 0; i < bytesPerPixel; ++i) {
                    value |= static_cast<std::uint32_t>(pixel[i]) << (8 * i);
                }

                const auto channel = [&](std::uint32_t mask) -> std::uint8_t {
                    if (mask == 0u) {
                        return 0xFFu;
                    }
                    std::uint32_t shift = 0;
                    while (((mask >> shift) & 1u) == 0u) {
                        ++shift;
                    }
                    const std::uint32_t max = mask >> shift;
                    const std::uint32_t raw = (value & mask) >> shift;
                    return static_cast<std::uint8_t>((raw * 255u) / max);
                };

                out[static_cast<std::size_t>(y) * kTargetWidth + x] =
                    (static_cast<std::uint32_t>(channel(details->Amask)) << 24)
                    | (static_cast<std::uint32_t>(channel(details->Rmask)) << 16)
                    | (static_cast<std::uint32_t>(channel(details->Gmask)) << 8)
                    | static_cast<std::uint32_t>(channel(details->Bmask));
            }
        }
        ok = true;
        SDL_UnlockSurface(surface);
    }
    SDL_DestroySurface(surface);
    return ok;
}

std::uint32_t pixelAt(const std::vector<std::uint32_t>& pixels, int x, int y) {
    return pixels[static_cast<std::size_t>(y) * kTargetWidth + x];
}

void testRender() {
    std::printf("\n== 离屏渲染：三态与形状 ==\n");

    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("[失败] SDL_Init 失败：%s\n", SDL_GetError());
        ++gFailed;
        return;
    }

    SDL_Window* window = SDL_CreateWindow("offscreen", 64, 64, 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    SDL_Texture* target =
        renderer != nullptr
            ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, kTargetWidth,
                                kTargetHeight)
            : nullptr;

    if (target == nullptr) {
        std::printf("[失败] 建离屏目标失败：%s\n", SDL_GetError());
        ++gFailed;
        if (renderer != nullptr) {
            SDL_DestroyRenderer(renderer);
        }
        if (window != nullptr) {
            SDL_DestroyWindow(window);
        }
        SDL_Quit();
        return;
    }

    check(SDL_SetRenderTarget(renderer, target), "渲染目标切到离屏纹理");

    // 和窗口层保持一致：绘制混合打开，颜色的 alpha 才会参与合成。
    // 不打开的话这里会读到"原样写进去的 0xBF000000"，而窗口上是合成后的颜色——
    // 测试和生产看到两套行为，正是这个 bug 藏了很久的原因。
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    ink::ButtonLibrary::ClearForTest();
    ink::ButtonLibrary::Register(exampleButton());

    ButtonProbeScene scene;
    check(scene.IsRegistered(), "探针场景注册成功");
    ink::SceneLibrary::SetActiveScene(&scene);

    // 按钮放在 (100,100)，200×100，圆角 10。
    //
    // 注意锚点怎么算：位置 = 父级尺寸 × 上级锚点 − 自身尺寸 × 自身锚点 + 偏移。
    // 示例配置里 selfAnchor 是 (0.5,0.5)，所以只给 offset 的话按钮会
    // **左移半个身位**。这里把 selfAnchor 改成左上角，偏移才是"左上角的位置"。
    ink::ButtonData data = exampleButton();
    data.selfAnchor = ink::Anchor{0.0f, 0.0f};
    data.traceAnchor = ink::Anchor{0.0f, 0.0f};
    data.offsetX = 100.0f;
    data.offsetY = 100.0f;
    ink::InkingStaticButton button(&scene, data);

    check(near(button.GetAbsX(), 100.0f) && near(button.GetAbsY(), 100.0f),
          "按钮的绝对位置是 (100,100)（锚点 + 偏移算出来的）");

    std::vector<std::uint32_t> pixels;

    // 清成**不透明的**底色再画按钮：这样"alpha 有没有参与合成"能直接从像素上看出来。
    //
    // 为什么不能用透明黑清屏（早期版本就是这么写的）：混合到 alpha=0 的目标上时
    // 结果恰好和"原样写入"一样（rgb 都被乘成 0，只剩 alpha），
    // 于是**混合开没开都读出 0xBF000000**，测试全绿却什么都没验到——
    // 这个 bug 就是这么藏下来的。
    constexpr std::uint32_t kBackdrop = 0xFF1A1A22u;

    const auto renderAndRead = [&]() -> bool {
        SDL_SetRenderDrawColor(renderer, ink::ColorRed(kBackdrop),
                               ink::ColorGreen(kBackdrop),
                               ink::ColorBlue(kBackdrop), 255);
        SDL_RenderClear(renderer);
        ink::SceneLibrary::RenderScene(renderer);
        SDL_RenderPresent(renderer);
        return readPixelsAsArgb(renderer, pixels);
    };

    check(renderAndRead(), "离屏像素读回成功");

    // 期望值是算出来的：0.75 的黑叠在 0xFF1A1A22 上
    //   a = 0.75    → 191/255
    //   rgb = 0x1A × (1 − 0.75) ≈ 6.5 → 6（0x06）
    //   alpha 通道：源 alpha + 目标 alpha × (1 − 源 alpha) → 不透明，读回 0xFF
    // 所以是 0xFF060608 而不是 0xBF000000。
    //
    // 悬停那组是**实测值**：0x1A × 0.15 ≈ 3.9，SDL 的整数混合算出的是 0x03
    // 而不是 0x04——差一个最低位，不写清楚的话下一个人会以为这里写错了。
    constexpr std::uint32_t kNormalOverBackdrop = 0xFF060608u;
    constexpr std::uint32_t kHoverOverBackdrop = 0xFF030305u;

    // ---- 通常态：0.75 的黑叠在底色上 ----
    {
        const std::uint32_t actual = pixelAt(pixels, 200, 150);
        check(actual != 0xBF000000u,
              "alpha 真的参与了合成（不是把 0xBF000000 原样写进去），实得 "
                  + toHex(actual));
    }
    check(pixelAt(pixels, 50, 50) == kBackdrop, "按钮之外只有底色");

    // ---- 悬停态：0.85 的黑 ----
    check(button.MouseHover(true), "切到悬停");
    check(renderAndRead(), "悬停帧读回成功");
    {
        const std::uint32_t actual = pixelAt(pixels, 200, 150);
        check(actual == kHoverOverBackdrop,
              "悬停态画出 0.85 的黑（合成后应为 " + toHex(kHoverOverBackdrop)
                  + "，实得 " + toHex(actual) + "）");
        check(actual != kNormalOverBackdrop,
              "悬停态与通常态在像素上确实不同");
    }

    // ---- 按下态：回到 0.75 ----
    check(button.MousePress(true), "切到按下");
    check(renderAndRead(), "按下帧读回成功");
    {
        const std::uint32_t actual = pixelAt(pixels, 200, 150);
        check(actual == kNormalOverBackdrop,
              "按下态画出 0.75 的黑（合成后应为 "
                  + toHex(kNormalOverBackdrop) + "，实得 " + toHex(actual)
                  + "）");
    }

    button.MouseRelease();
    button.MouseHover(false);

    // ---- 形状：画出来的区域必须和命中的区域是同一份定义 ----
    //
    // (101,101) 落在按钮包围盒里，但按圆角定义它在**外面**
    // （到角心 (110,110) 的距离 ≈ 12.73 > 半径 10）。
    //
    // 形状层做完抗锯齿填充之前，这里记的是"填充仍是直角矩形、角上照样有像素"
    // 那条已知不一致；现在填充走的是同一份形状定义，所以它必须是**底色**。
    {
        const float distance =
            ink::SignedDistance(button.GetShape(), 1.0f, 1.0f, 200, 100);
        const std::uint32_t corner = pixelAt(pixels, 101, 101);
        std::printf("      [实测] 圆角外一点的 f = %.3f，该点像素 = %s（底色 %s）\n",
                    distance, toHex(corner).c_str(), toHex(kBackdrop).c_str());
        check(distance > 0.0f, "命中判定：圆角外一点确实在形状外（f > 0）");
        check(corner == kBackdrop,
              "填充按形状走了：被磨掉的角现在是底色，实得 " + toHex(corner));
    }

    // ---- 抗锯齿：边缘要有半透明过渡，不是硬台阶 ----
    //
    // 拿一个大圆来看：圆的边界是斜穿像素网格的，1 像素宽的羽化带必然落在
    // 某些像素上。完全不透明白叠在深色底上，过渡像素**既不是白也不是底色**，
    // 一眼就能分出来——这条断言不依赖任何抗锯齿的内部实现细节。
    {
        ink::ButtonData circleData = exampleButton();
        circleData.shape = ink::ShapeSpec::Circle();
        circleData.width = 121;
        circleData.height = 121;
        circleData.selfAnchor = ink::Anchor{0.0f, 0.0f};
        circleData.traceAnchor = ink::Anchor{0.0f, 0.0f};
        circleData.offsetX = 600.0f;
        circleData.offsetY = 400.0f;
        // 不透明纯白，和底色拉开最大对比；三态无所谓，这里只看边缘。
        circleData.normal = ink::ButtonAppearance::FromUnitRgba(1.0f, 1.0f, 1.0f);
        ink::InkingStaticButton circle(&scene, circleData);
        // 同一帧里再放一个**像素对齐的矩形**按钮：它的边界正好落在像素边界上，
        // 这时最外一圈像素的覆盖率只能是 0 或 1，不能是"半个"。
        // 这一条钉的是"羽化带**对称**跨在边界上"——如果整条带子压在边界内侧，
        // 面板 / 背景板 / 分割线这类矩形的最外一列像素会集体变成半透明。
        ink::ButtonData rectData = circleData;
        rectData.shape = ink::ShapeSpec::Rect();
        rectData.width = 100;
        rectData.height = 100;
        rectData.offsetX = 900.0f;
        rectData.offsetY = 400.0f;
        ink::InkingStaticButton sharpCorner(&scene, rectData);
        check(renderAndRead(), "圆形 + 矩形这一帧读回成功");

        // 圆占 (600,400)-(721,521)，圆心 (660.5,460.5)；沿圆心行扫一遍。
        constexpr int kScanY = 460;
        constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
        int solid = 0;
        int backdropPixels = 0;
        int feathered = 0;
        std::uint32_t featheredSample = 0u;
        for (int x = 560; x < 780; ++x) {
            const std::uint32_t color = pixelAt(pixels, x, kScanY);
            if (color == kWhite) {
                ++solid;
            } else if (color == kBackdrop) {
                ++backdropPixels;
            } else {
                ++feathered;
                if (featheredSample == 0u) {
                    featheredSample = color;
                }
            }
        }
        const auto brightness = [](std::uint32_t color) {
            return static_cast<int>(ink::ColorRed(color))
                 + static_cast<int>(ink::ColorGreen(color))
                 + static_cast<int>(ink::ColorBlue(color));
        };
        std::printf("      [实测] 圆扫描线 y=%d：实心 %d，底色 %d，过渡 %d（样本 %s）\n",
                    kScanY, solid, backdropPixels, feathered,
                    toHex(featheredSample).c_str());
        check(solid > 80, "圆内部是大片实心色");
        check(backdropPixels > 80, "圆外是底色");
        check(feathered >= 2,
              "圆的左右边缘各有半透明过渡像素（抗锯齿真的在，实得 "
                  + std::to_string(feathered) + " 个过渡像素）");
        // 过渡像素是"半个像素被覆盖"的结果：亮度必须严格落在底色与实心色之间。
        // 这一条把"抗锯齿"和"边缘偏了/形状缩水了"分开——后者会给出纯底色或纯实心色。
        check(feathered > 0 && brightness(featheredSample) > brightness(kBackdrop)
                  && brightness(featheredSample) < brightness(kWhite),
              "过渡像素的亮度严格介于底色与实心色之间（实得 "
                  + toHex(featheredSample) + "）");

        // 像素对齐的矩形：(900,400)-(1000,500)。
        //
        // 这里**故意不**断言"最外一圈像素必须完全是实心 / 底色"。
        // 实测：SDL 的 software 渲染器在 `SDL_RenderGeometry` 里的采样点落在
        // 像素的**右下角**（GPU 后端用像素中心，那才是标准约定），于是
        // "边界正好穿过像素"的那一圈会读出 50% 的混合色（实测 `0xFF8C8C90`）。
        // 那是**后端的光栅化约定**，不是形状的契约——形状的契约是
        // "离边界 1 像素以上的地方，该实就实、该透就透"，这一条跨后端都成立。
        check(pixelAt(pixels, 901, 401) == kWhite,
              "像素对齐的矩形：内部（离边界 1 像素以上）是实心（实得 "
                  + toHex(pixelAt(pixels, 901, 401)) + "）");
        check(pixelAt(pixels, 995, 495) == kWhite,
              "像素对齐的矩形：右下内部也是实心");
        check(pixelAt(pixels, 898, 400) == kBackdrop,
              "像素对齐的矩形：左边界外 1 像素以上是底色（实得 "
                  + toHex(pixelAt(pixels, 898, 400)) + "）");
        check(pixelAt(pixels, 900, 501) == kBackdrop,
              "像素对齐的矩形：下边界外 1 像素以上是底色（实得 "
                  + toHex(pixelAt(pixels, 900, 501)) + "）");
        // 位置与尺寸没跑偏：右边 1 像素以上也必须在形状内。
        check(pixelAt(pixels, 999, 450) == kWhite
                  || pixelAt(pixels, 998, 450) == kWhite,
              "像素对齐的矩形：右边界附近仍是实心（形状没整体缩水）");
    }

    // ---- 变换通道：**画出来的地方 == 点得中的地方** ----
    //
    // 这是变换的核心契约：绘制过正变换、命中过逆变换，两者必须严格互逆。
    // 验证方式**不手算几何**，而是交叉对账：扫一片区域，凡"明显有像素"的点必须
    // 命中、凡"纯底色"的点必须不命中。抗锯齿边缘（半透明那一圈）不算数——
    // 所以只统计周围 3×3 同色的点，把边界整圈排除掉。
    {
        const auto countMismatch = [&](const ink::InkingStaticButton& button,
                                       int x0, int y0, int x1, int y1) {
            int checked = 0;
            int mismatch = 0;
            for (int y = y0; y <= y1; y += 2) {
                for (int x = x0; x <= x1; x += 2) {
                    const bool painted = pixelAt(pixels, x, y) != kBackdrop;

                    bool uniform = true;
                    for (int dy = -1; dy <= 1 && uniform; ++dy) {
                        for (int dx = -1; dx <= 1; ++dx) {
                            if ((pixelAt(pixels, x + dx, y + dy) != kBackdrop)
                                != painted) {
                                uniform = false;
                                break;
                            }
                        }
                    }
                    if (!uniform) {
                        continue;  // 挨着边界，可能有抗锯齿，跳过
                    }

                    ++checked;
                    // 用**像素中心**去问命中——那正是鼠标坐标的语义。
                    if (button.HitTest(static_cast<float>(x) + 0.5f,
                                       static_cast<float>(y) + 0.5f)
                        != painted) {
                        ++mismatch;
                    }
                }
            }
            std::printf("      [实测] 变换一致性：查了 %d 个点，不一致 %d 个\n",
                        checked, mismatch);
            return mismatch;
        };

        // 变换这一段要在"干净背景"上验：前面那个 `button` 还在 (100,100) 原地，
        // 而 `turned` 也放在 (100,100)——它转成竖条之后，露出来的正是那个没旋转的
        // 旧按钮，于是扫描区里混进别人的像素，"画与点"的对账当场就失真了
        // （第一次跑就是 2468 个不一致，全来自这里）。藏掉它，扫描区只剩被测对象。
        button.SetVisible(false);

        // ① 瞬变 90°：横条变成竖条。hover 没配 transition，所以变换立刻到位。
        ink::ButtonData turnedData = exampleButton();
        turnedData.selfAnchor = ink::Anchor{0.0f, 0.0f};
        turnedData.traceAnchor = ink::Anchor{0.0f, 0.0f};
        turnedData.offsetX = 100.0f;
        turnedData.offsetY = 100.0f;
        turnedData.hover.transform = ink::TransformSpec{0.0f, 0.0f, 90.0f, 1.0f};
        ink::InkingStaticButton turned(&scene, turnedData);
        turned.MouseHover(true);
        check(near(turned.GetDisplayTransform().rotate, 90.0f),
              "没配过渡时变换瞬间到位（实得 "
                  + std::to_string(turned.GetDisplayTransform().rotate) + "°）");
        check(renderAndRead(), "旋转 90° 这一帧读回成功");
        check(countMismatch(turned, 60, 20, 340, 280) == 0,
              "旋转 90°：画出来的地方都能点到、点到的地方都画出来了");
        // 竖条：原来左边那一块现在应当空出来（画与点都空）。
        check(pixelAt(pixels, 110, 150) == kBackdrop,
              "旋转 90° 后 (110,150) 已经没有像素");
        check(!turned.HitTest(110.5f, 150.5f),
              "旋转 90° 后 (110,150) 也点不中");

        // ② 过渡中途（45°）：斜边那一档也要一致——这一档最容易出"半像素错位"。
        ink::ButtonData turningData = turnedData;
        turningData.offsetX = 380.0f;
        turningData.offsetY = 660.0f;
        turningData.hover.transitionSeconds = 0.2f;
        ink::InkingStaticButton turning(&scene, turningData);
        turning.MouseHover(true);
        scene.TickFrame(0.1);  // 0.2 的一半
        const float midRotate = turning.GetDisplayTransform().rotate;
        check(near(midRotate, 45.0f),
              "过渡中途的显示变换是 45°（实得 " + std::to_string(midRotate)
                  + "°）");
        check(renderAndRead(), "旋转 45° 这一帧读回成功");
        check(countMismatch(turning, 300, 560, 700, 880) == 0,
              "旋转 45° 中途：画出来的地方也都能点到");
    }

    SDL_SetRenderTarget(renderer, nullptr);
    ink::SceneLibrary::SetActiveScene(nullptr);
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

// ---------------------------------------------------------------------------
// 7. 标脏协议（T-3）：结构 / 重绘 / 指针三种脏，各走各的消费
//
// 这一段的重点不是"值对不对"，而是**三种脏互不牵连**：
//   - 动画每帧改颜色/变换 → 只该标重绘脏（+ 变换的命中脏），**绝不该**惊动结构；
//   - 静置够帧数就该自己清掉，否则"画面稳了"这个信号永远为真、等于没有；
//   - 指针移动只标指针脏，它不改任何几何。
// 前两条一旦写错，表现都是"功能正常、但每帧白干一遍活"，只有计数能钉住。
// ---------------------------------------------------------------------------

class DirtyProbeScene : public ink::InkingScene {
public:
    DirtyProbeScene() : ink::InkingScene("DirtyProbe") {}
};

void testDirtyProtocol() {
    std::printf("\n-- 标脏协议（结构 / 重绘 / 指针）--\n");

    DirtyProbeScene scene;
    // 过渡推进（TickFrame → onAnimationTick）只在**活跃**场景上有；所以这里必须点亮。
    ink::SceneLibrary::SetActiveScene(&scene);

    ink::ButtonData data = exampleButton();
    data.selfAnchor = ink::Anchor{0.0f, 0.0f};
    data.traceAnchor = ink::Anchor{0.0f, 0.0f};
    data.offsetX = 100.0f;
    data.offsetY = 100.0f;
    data.hover.transitionSeconds = 0.2f;
    data.hover.transform = ink::TransformSpec{0.0f, 0.0f, 30.0f, 1.0f};
    ink::InkingStaticButton animated(&scene, data);

    // 构造出来的节点初始就是脏的（第一帧总得画一次），先空转几帧把这份历史脏
    // 消费干净——后面的计数才只反映"这一段动画"，不会把别人的账算进来。
    for (int i = 0; i <= ink::InkingAnchor::kRepaintQuietFrames; ++i) {
        scene.ConsumeFrameDirty();
    }
    check(!animated.IsRepaintDirty() && !animated.IsDirty(),
          "静置几帧后，按钮不再是脏的");
    check(scene.GetRepaintingNodeCount() == 0,
          "静置几帧后，全场没有节点还在重绘（实得 "
              + std::to_string(scene.GetRepaintingNodeCount()) + "）");

    const std::uint64_t generationBefore = scene.GetStructureGeneration();

    // 1) 过渡推进一帧：画面变了 → 重绘脏；变换在变 → 命中脏（悬停要重算）。
    animated.MouseHover(true);
    scene.TickFrame(0.05);
    check(animated.IsRepaintDirty(), "过渡推进了一帧 → 重绘脏");
    check(animated.IsDirty(), "变换在变 → 命中脏（同一个点可能落到别的组件上）");
    check(scene.GetStructureGeneration() == generationBefore,
          "颜色/变换动画**不该**惊动结构（惊动了就是每帧白重建一次绘制列表）");

    // 2) 消费一次：过渡还没走完，所以仍然脏；场景能看见"画面在动"。
    scene.ConsumeFrameDirty();
    check(animated.IsRepaintDirty(), "过渡没走完，消费一次之后仍然脏");
    check(scene.GetRepaintingNodeCount() == 1,
          "场景看到「有 1 个节点的画面在动」（实得 "
              + std::to_string(scene.GetRepaintingNodeCount()) + "）");

    // 3) 把过渡推完，再连着消费够帧数 → 清掉。
    scene.TickFrame(0.5);
    for (int i = 0; i < ink::InkingAnchor::kRepaintQuietFrames; ++i) {
        scene.ConsumeFrameDirty();
    }
    check(!animated.IsRepaintDirty(),
          "连续 " + std::to_string(ink::InkingAnchor::kRepaintQuietFrames)
              + " 帧没有新的重绘脏 → 自己清掉");
    check(!animated.IsDirty(),
          "命中脏也会在 " + std::to_string(ink::InkingAnchor::kHitQuietFrames)
              + " 帧之后清掉");
    check(scene.GetRepaintingNodeCount() == 0, "画面稳了：全场不再有节点重绘");
    check(scene.GetStructureGeneration() == generationBefore,
          "整段动画下来，结构脏一次都没被标过");

    // 3b) 没配过渡的三态切换（瞬变）：显示值当场就变，也必须标出重绘脏来。
    //     （这条曾经漏过：标脏只写在 Advance 里，而瞬变根本不经过 Advance。）
    {
        ink::ButtonData instant = exampleButton();
        instant.selfAnchor = ink::Anchor{0.0f, 0.0f};
        instant.traceAnchor = ink::Anchor{0.0f, 0.0f};
        instant.offsetX = 400.0f;
        instant.offsetY = 400.0f;
        instant.hover.transform = ink::TransformSpec{0.0f, 0.0f, 0.0f, 1.2f};
        ink::InkingStaticButton snapped(&scene, instant);

        for (int i = 0; i <= ink::InkingAnchor::kRepaintQuietFrames; ++i) {
            scene.ConsumeFrameDirty();
        }
        check(!snapped.IsRepaintDirty(), "瞬变按钮：静置之后不脏");

        snapped.MouseHover(true);  // 没配 transition → 显示值当场换掉
        scene.TickFrame(0.0);      // 空推一帧，只为了拿到那笔"当场换掉"的账
        check(snapped.IsRepaintDirty(),
              "没配过渡的三态切换也要标重绘脏（画面当场就变了）");
        check(snapped.IsDirty(), "它同时改了缩放 → 命中脏也要标");
    }

    // 4) 指针脏：只有真的变了才算，帧末消费一次就清。
    scene.SetPointerState(500.0f, 500.0f, /*down=*/false, /*inside=*/true);
    check(scene.IsPointerDirty(), "指针动了 → 指针脏");
    scene.ConsumeFrameDirty();
    check(!scene.IsPointerDirty(), "帧末消费之后指针脏清掉");
    scene.SetPointerState(500.0f, 500.0f, /*down=*/false, /*inside=*/true);
    check(!scene.IsPointerDirty(),
          "原地喂同一个坐标不算脏（窗口层每帧都会喂一次）");

    // 5) 几何写入口：结构脏 + 重绘脏都要标——这是"命中表要变"那一类。
    {
        ink::AnchorData moverData;
        moverData.selfAnchor = ink::Anchor{0.0f, 0.0f};
        moverData.traceAnchor = ink::Anchor{0.0f, 0.0f};
        moverData.offsetX = 800.0f;
        moverData.offsetY = 500.0f;
        moverData.width = 80;
        moverData.height = 40;
        ink::InkingDynamicAnchor mover(&scene, moverData);

        for (int i = 0; i <= ink::InkingAnchor::kRepaintQuietFrames; ++i) {
            scene.ConsumeFrameDirty();
        }

        const std::uint64_t beforeMove = scene.GetStructureGeneration();
        check(mover.Resize(60, 40), "Resize 真的改了");
        check(mover.IsRepaintDirty(), "几何变化 → 重绘脏");
        check(scene.GetStructureGeneration() > beforeMove,
              "几何变化 → 结构脏（帧首要重算静态坐标快照）");
        check(!mover.Resize(60, 40), "传同一个尺寸不算改");
    }
}

// ---------------------------------------------------------------------------
// 8. 命中表（T-4）：换实现不能换语义
//
// 命中表最难的地方不是"能不能查"，而是"换掉线性扫之后，答案还是不是同一个"。
// 所以这一段的核心是一条**逐点对账**：拿表查一遍、拿线性扫查一遍，全图每个点
// 都必须给出同一个目标。参考实现写在自检里（框架里只留表）——两份实现各自
// 独立，只有互相印证过才敢说"换的是实现，不是规则"。
// ---------------------------------------------------------------------------

class HitProbeScene : public ink::InkingScene {
public:
    HitProbeScene() : ink::InkingScene("HitProbe") {}
};

/** 参考实现：从绘制列表尾往前线性扫（= 没有表的时候那条路），三态规则照旧。 */
ink::InkingAnchor* linearTopmostHit(ink::InkingScene& scene, float x, float y) {
    const std::vector<ink::InkingScene::DrawItem>& items =
        scene.BuildDrawList();

    ink::InkingAnchor* passTarget = nullptr;
    for (std::size_t i = items.size(); i > 0; --i) {
        ink::InkingAnchor* node = items[i - 1].node;
        if (node == nullptr || !node->IsVisibleInTree()) {
            continue;
        }
        if (!node->HitTest(x, y)) {
            continue;
        }
        if (node->IsHitPassThrough()) {
            if (passTarget == nullptr) {
                passTarget = node;
            }
            continue;
        }
        return node;
    }
    return passTarget;
}

void testHitTable() {
    std::printf("\n-- 命中表（簇 + 格子 + CSR）--\n");

    HitProbeScene scene;
    ink::SceneLibrary::SetActiveScene(&scene);

    const auto place = [](ink::ButtonData& data, float x, float y) {
        data.selfAnchor = ink::Anchor{0.0f, 0.0f};
        data.traceAnchor = ink::Anchor{0.0f, 0.0f};
        data.offsetX = x;
        data.offsetY = y;
    };

    // A / B：两个重叠的普通按钮（B 的 z 更高，压在 A 上）。
    ink::ButtonData dataA = exampleButton();
    place(dataA, 100.0f, 100.0f);
    ink::InkingStaticButton a(&scene, dataA);

    ink::ButtonData dataB = exampleButton();
    place(dataB, 200.0f, 150.0f);
    dataB.zIndex = 1;
    ink::InkingStaticButton b(&scene, dataB);

    // C / D：C 标了"让过"（PassThrough），压在 D 上——点 C 应该穿到 D。
    ink::ButtonData dataC = exampleButton();
    place(dataC, 100.0f, 320.0f);
    dataC.zIndex = 5;
    ink::InkingStaticButton c(&scene, dataC);
    c.SetHitPassThrough(true);

    ink::ButtonData dataD = exampleButton();
    place(dataD, 100.0f, 320.0f);
    ink::InkingStaticButton d(&scene, dataD);

    // E：隐藏的按钮，点它中心应该是 Miss（隐藏的既不进表也不留洞）。
    ink::ButtonData dataE = exampleButton();
    place(dataE, 600.0f, 500.0f);
    ink::InkingStaticButton e(&scene, dataE);
    e.SetVisible(false);

    // F：动态按钮（不进表，只留洞），G 是压在它下面的静态按钮——
    //    用来看"静态与动态按 z 序合并"。F 的 z 高一点，否则同 z 时
    //    后构造的 G 会赢（注册序号更大），那就测不到"动态赢"这一侧了。
    ink::ButtonData dataF = exampleButton();
    place(dataF, 900.0f, 200.0f);
    dataF.zIndex = 3;
    ink::InkingDynamicButton f(&scene, dataF);

    ink::ButtonData dataG = exampleButton();
    place(dataG, 900.0f, 200.0f);
    ink::InkingStaticButton g(&scene, dataG);

    // H：带旋转的静态按钮（悬停转 30°）——检验"包络冻结、精判用当前变换"。
    ink::ButtonData dataH = exampleButton();
    place(dataH, 500.0f, 800.0f);
    dataH.hover.transitionSeconds = 0.1f;
    dataH.hover.transform = ink::TransformSpec{0.0f, 0.0f, 30.0f, 1.0f};
    ink::InkingStaticButton h(&scene, dataH);

    // 表建好了吗：静态可见的有 A / B / C / D / G / H 六个（E 隐藏、F 动态不算）。
    check(scene.GetHitTableEntryCount() == 6,
          "表里只装静态可见组件（实得 "
              + std::to_string(scene.GetHitTableEntryCount()) + "）");
    check(scene.GetHitTableClusterCount() >= 1, "至少建出一个簇");
    check(scene.GetHitTableCellCount() > 0, "簇内切出了格子");

    // ---- 逐点对账：表 == 线性扫 ----
    {
        int checked = 0;
        int mismatch = 0;
        for (int y = 40; y <= 1000; y += 7) {
            for (int x = 40; x <= 1300; x += 7) {
                const float fx = static_cast<float>(x);
                const float fy = static_cast<float>(y);

                ink::InkingAnchor* const tableHit = scene.QueryHit(fx, fy).target;
                // 参考实现里动态组件也参与（它自己判命中），规则与 QueryHit 一致。
                ink::InkingAnchor* linearHit = linearTopmostHit(scene, fx, fy);

                // 动态层：QueryHit 只在"点在洞里"时才去问动态层，
                // 所以对账时把动态命中按 z 序合进来，两边才是同一套规则。
                ink::InkingAnchor* dynamicHit = nullptr;
                {
                    const std::vector<ink::InkingScene::DrawItem>& items =
                        scene.BuildDrawList();
                    for (std::size_t i = items.size(); i > 0; --i) {
                        ink::InkingAnchor* node = items[i - 1].node;
                        if (node == nullptr || !node->IsDynamic()
                            || !node->IsVisibleInTree()) {
                            continue;
                        }
                        if (node->HitTest(fx, fy)) {
                            dynamicHit = node;
                            break;
                        }
                    }
                }
                if (dynamicHit != nullptr
                    && (linearHit == nullptr
                        || ink::InkingAnchor::IsAbove(*dynamicHit, *linearHit))) {
                    linearHit = dynamicHit;
                }

                ++checked;
                if (tableHit != linearHit) {
                    ++mismatch;
                }
            }
        }
        std::printf("      [实测] 命中表对账：查了 %d 个点，不一致 %d 个\n",
                    checked, mismatch);
        check(mismatch == 0, "表查出来的目标和线性扫逐点一致");
    }

    // ---- 三态 ----
    {
        // 穿透：C 在上且标了让过，点它应该穿到下面的 D。
        const ink::HitResult throughHit = scene.QueryHit(150.0f, 360.0f);
        check(throughHit.kind == ink::HitKind::Block && throughHit.target == &d,
              "PassThrough 让过：点 C 穿到了下面的 D");

        // 普通命中：B 压在 A 上，点重叠区应该是 B。
        const ink::HitResult overlap = scene.QueryHit(250.0f, 200.0f);
        check(overlap.kind == ink::HitKind::Block && overlap.target == &b,
              "重叠区取 z 更高的那个");

        // 空白：场景没画到的地方是 Miss。
        const ink::HitResult empty = scene.QueryHit(1800.0f, 1000.0f);
        check(empty.kind == ink::HitKind::Miss && empty.target == nullptr,
              "空白处是 Miss");

        // 隐藏的：点 E 中心什么都不该有。
        const ink::HitResult hidden = scene.QueryHit(700.0f, 550.0f);
        check(hidden.kind == ink::HitKind::Miss,
              "隐藏的组件既命不中也留不下洞");
    }

    // ---- 动态层合并：同一块地方，靠 z 序决定谁说了算 ----
    {
        // F（动态）压在 G（静态）上面：点在重叠区，归动态组件。
        const ink::HitResult onDynamic = scene.QueryHit(950.0f, 250.0f);
        check(onDynamic.target == &f, "动态压在静态上时，动态赢");

        // 把 G 抬到 F 上面：同一块地方改成静态赢。
        g.ChangeZIndex(20);
        const ink::HitResult onStatic = scene.QueryHit(950.0f, 250.0f);
        check(onStatic.target == &g, "静态抬到更高 z 之后，静态赢");
        g.ChangeZIndex(0);

        // 洞之外：不该问动态层（这条只能靠"结果对"间接验，但很值——
        // 洞一旦算错，表现就是"动态组件忽然抢不到点击"）。
        // 取 A 的中心而不是角：A 是圆角矩形，角上是空的。
        const ink::HitResult awayFromDynamic = scene.QueryHit(150.0f, 150.0f);
        check(awayFromDynamic.target == &a, "远离动态组件的地方不受它影响");
    }

    // ---- 冻结与重烘：几何变了表跟着变，变换变了表不动但命中照样对 ----
    {
        ink::ButtonData dataJ = exampleButton();
        place(dataJ, 1400.0f, 700.0f);
        ink::InkingStaticButton j(&scene, dataJ);
        check(scene.QueryHit(1450.0f, 750.0f).target == &j,
              "静态按钮：原本就点得到");

        // 变换：H 悬停转 30°，旋转后它的角会伸到原来的表外一点点。
        h.MouseHover(true);
        scene.TickFrame(0.5);
        const std::size_t entriesBefore = scene.GetHitTableEntryCount();
        const float rotate = h.GetDisplayTransform().rotate;
        check(rotate > 29.0f && rotate < 31.0f,
              "H 已经转到 30°（实得 " + std::to_string(rotate) + "）");
        check(scene.GetHitTableEntryCount() == entriesBefore,
              "变换不触发重烘（条目数没变）");

        // 旋转之后，落在"原表内、旋转后仍在包络内"的点必须照样命中：
        // 包络是保守的，所以这里查得到；精判用当前变换，所以答案是对的。
        const ink::HitResult rotated = scene.QueryHit(600.0f, 850.0f);
        check(rotated.target == &h,
              "旋转后的按钮照样点得到（包络冻结 + 精判用当前变换）");
    }
}

}  // namespace

int main() {
    std::printf("Button 组件自检（SDL3 %s）\n", ink::sdl3_version().c_str());

    testColor();
    testShape();
    testLibrary();
    testButtonState();
    testNameConstruction();
    testRender();
    testDirtyProtocol();
    testHitTable();

    std::printf("\n失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
