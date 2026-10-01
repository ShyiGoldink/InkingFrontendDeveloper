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

    // ---- 形状：被磨掉的角不该有像素 ----
    //
    // (101,101) 落在按钮包围盒里，但按圆角定义它在**外面**
    // （到角心 (110,110) 的距离 ≈ 12.73 > 半径 10）。现在的填充还是直角矩形
    // （抗锯齿填充要等形状层第二步），所以这一条现在**必然失败**——
    // 它记录的是"形状与填充还没统一"这个已知状态，不是回归。
    // 形状层做完填充之后，这条要改成 check(...)。
    {
        const float distance =
            ink::SignedDistance(button.GetShape(), 1.0f, 1.0f, 200, 100);
        const std::uint32_t corner = pixelAt(pixels, 101, 101);
        std::printf("      [记录] 圆角外一点的 f = %.3f，该点像素 = %s\n",
                    distance, toHex(corner).c_str());
        check(distance > 0.0f, "命中判定：圆角外一点确实在形状外（f > 0）");
        check(corner != 0x00000000u,
              "填充仍是直角矩形：被磨掉的角现在照样有像素（形状层第二步修）");
    }

    SDL_SetRenderTarget(renderer, nullptr);
    ink::SceneLibrary::SetActiveScene(nullptr);
    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
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

    std::printf("\n失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
