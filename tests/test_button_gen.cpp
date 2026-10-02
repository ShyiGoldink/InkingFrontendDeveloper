// 生成代码的运行时自检：验证 inkgen 产物**真的把配置登记进了 ButtonLibrary**，
// 而且登记是幂等的。
//
// 它 include 的是生成出来的那份头（路径由 CMake 用 INK_GENERATED_DIR 传进来），
// 所以链接的就是 examples/buttons_demo 那份生成对象——"配置 → 代码 → 库"
// 这条链路里，这一环单独被钉住。
//
// 为什么不做成"调 inkgen 可执行文件生成到临时目录再把产物编进来"：
// 自适应构建系统（把产物路径喂回 CMake）复杂度远大于收益，而且那样测的是
// "生成器能跑"，不是"生成出来的东西对"。生成器的正确性由 inkgen_test 负责，
// 这里负责产物在运行期的效果。
//
// 非 0 退出码表示有检查项失败。

#include <ink/ink.h>
#include <button/ButtonLibrary.h>
#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>

#include INK_GENERATED_HEADER

#include <cstdio>
#include <string>
#include <type_traits>

namespace {

int gFailed = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s %s\n", condition ? "[通过]" : "[失败]", what.c_str());
    if (!condition) {
        ++gFailed;
    }
}

}  // namespace

int main() {
    // 注意 INK_GENERATED_HEADER 展开成 `<inkgen/.../button_service.h>`（尖括号形式），
    // 所以它只能出现在 #include 里，不能当字符串打出来。
    std::printf("生成代码自检\n");

    // 1. 每个名字生成了一个**独立的类**，数据烘在类里。
    check(ink::cxxcss::kButtonCount == 2,
          "生成的头说本次有 2 个按钮配置（静态 normalButton + 动态 dynamicTestButton）");
    check(std::string(ink::cxxcss::NormalButton::kName) == "normalButton",
          "生成的类带着自己的名字（NormalButton::kName）");

    const ink::ButtonData& baked = ink::cxxcss::NormalButton::Data();
    check(baked.name == "normalButton", "烘在类里的配置名字对得上");
    check(baked.width == 200 && baked.height == 100, "尺寸与配置一致");

    // 颜色这里**只验关系，不复述数值**：把 0.35 / 0.85 抄进断言，等于给配置
    // 又留了一份副本——改 json 就会红，而那种红没有信息量（配置本来就可以随便调）。
    // 要验的是"三态真的分得开、悬停更实"，这才是示例配置存在的意义。
    check(baked.normal.color != baked.hover.color,
          "通常态与悬停态的颜色不一样（三态分得开）");
    check(ink::ColorAlpha(baked.hover.color) > ink::ColorAlpha(baked.normal.color),
          "悬停比通常更不透明（示例配置刻意拉大了差距）");
    check(ink::ColorAlpha(baked.normal.color) > 0
              && ink::ColorAlpha(baked.normal.color) < 255,
          "通常态是半透明的（不是全透明也不是不透明）");
    check(ink::ColorRed(baked.normal.color) == ink::ColorRed(baked.hover.color)
              && ink::ColorGreen(baked.normal.color)
                     == ink::ColorGreen(baked.hover.color)
              && ink::ColorBlue(baked.normal.color)
                     == ink::ColorBlue(baked.hover.color),
          "三态是同一个颜色、只有透明度不同（换色的话这条会红，那时改这里）");
    check(baked.shape.kind == ink::ShapeKind::RoundedRect
              && baked.shape.radius == 10.0f,
          "形状与配置一致（圆角 10）");
    check(baked.selfAnchor.x == 0.5f && baked.traceAnchor.x == 0.5f,
          "锚点与配置一致");

    // 1b. `"dynamic": true` 的配置生成出来的是**动态**按钮类。
    //
    //     "哪个基类"这件事在编译期就定了，所以这里用 static_assert 钉住——
    //     它同时说明"动态按钮在类型上就有几何写入口"（Resize / ChangeOffset
    //     来自 InkingDynamicAnchor），不用运行期再问一次。
    {
        static_assert(std::is_base_of_v<ink::InkingDynamicAnchor,
                                        ink::cxxcss::DynamicTestButton>,
                      "dynamic:true 的配置要生成 InkingDynamicButton 的子类");
        static_assert(!std::is_base_of_v<ink::InkingStaticAnchor,
                                         ink::cxxcss::DynamicTestButton>,
                      "静态 / 动态是二选一，不能既进表又不进表");
        check(std::string(ink::cxxcss::DynamicTestButton::kName)
                  == "dynamicTestButton",
              "动态按钮的类名与名字常量对得上");

        const ink::ButtonData& dynamic = ink::cxxcss::DynamicTestButton::Data();
        check(dynamic.name == "dynamicTestButton" && dynamic.width == 220
                  && dynamic.height == 100,
              "动态按钮烘的配置与它自己的 json 一致（不是抄了静态那份）");
        check(dynamic.shape.kind == ink::ShapeKind::RoundedRect,
              "动态按钮的形状也来自它自己的配置");
    }

    // 2. 类自己带着数据：**库是空的也照样能构造出正确的按钮**。
    //    这是"生成类"相对"运行期查表"最关键的一条差别。
    {
        ink::ButtonLibrary::ClearForTest();
        check(ink::ButtonLibrary::Count() == 0, "先把按名字查的表清空");

        ink::cxxcss::NormalButton button(nullptr);
        check(button.GetWidth() == 200 && button.GetHeight() == 100,
              "库是空的，生成类照样给出配置里的几何（数据烘在类里）");
        check(button.GetShape().kind == ink::ShapeKind::RoundedRect,
              "形状也来自类里烘的那份");
        check(ink::InkingStaticButton::UnknownNameCount() == 0,
              "走生成类这条路，不会碰到「按名字查不到」");
    }

    // 3. 按名字查这条路仍然可用（生成代码在启动期把同一份也登记了）。
    {
        ink::cxxcss::RegisterAllButtons();
        const ink::ButtonData* found =
            ink::ButtonLibrary::Find(ink::cxxcss::NormalButton::kName);
        check(found != nullptr, "RegisterAllButtons 之后按名字查得到");
        check(found != nullptr && found->width == baked.width
                  && found->normal.color == baked.normal.color,
              "按名字查到的那份与类里烘的那份一致");

        const std::size_t before = ink::ButtonLibrary::Count();
        ink::cxxcss::RegisterAllButtons();
        check(ink::ButtonLibrary::Count() == before,
              "再调一次 RegisterAllButtons 不会让库里的项数变化");
    }

    // 4. 对照：**手写字符串名字**这条路，查不到时要留下可断言的痕迹
    //    （以前是静默退回一份默认外观——按钮长得不对，却没有任何报错）。
    {
        const int before = ink::InkingStaticButton::UnknownNameCount();
        ink::InkingStaticButton orphan(nullptr, "这个名字根本没登记过");
        check(ink::InkingStaticButton::UnknownNameCount() == before + 1,
              "按名字构造查不到时计数加一（可断言，不是静默）");
        check(orphan.GetWidth() == 0 && orphan.GetHeight() == 0,
              "查不到时给的是空按钮（不画东西、不响应点击）");
        check(orphan.GetName() == "这个名字根本没登记过",
              "名字仍然记下来，便于日志定位");
    }

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
