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
#include <button/InkingStaticButton.h>

#include INK_GENERATED_HEADER

#include <cstdio>
#include <string>

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

    // 1. 启动期的静态对象应该已经把配置登记好了——不需要调用方做任何事。
    check(ink::cxxcss::kButtonCount == 1, "生成的头说本次有 1 个按钮配置");
    check(ink::ButtonLibrary::IsNameTaken(ink::cxxcss::kNormalButtonName),
          "启动后库里就有 normalButton（静态登记生效）");

    const ink::ButtonData* data =
        ink::ButtonLibrary::Find(ink::cxxcss::kNormalButtonName);
    if (data == nullptr) {
        std::printf("失败项：%d\n", gFailed + 1);
        return 1;
    }

    check(data->width == 200 && data->height == 100, "尺寸与配置一致");

    // 颜色这里**只验关系，不复述数值**：把 0.35 / 0.85 抄进断言，等于给配置
    // 又留了一份副本——改 json 就会红，而那种红没有信息量（配置本来就可以随便调）。
    // 要验的是"三态真的分得开、悬停更实"，这才是示例配置存在的意义。
    check(data->normal.color != data->hover.color,
          "通常态与悬停态的颜色不一样（三态分得开）");
    check(ink::ColorAlpha(data->hover.color) > ink::ColorAlpha(data->normal.color),
          "悬停比通常更不透明（示例配置刻意拉大了差距）");
    check(ink::ColorAlpha(data->normal.color) > 0
              && ink::ColorAlpha(data->normal.color) < 255,
          "通常态是半透明的（不是全透明也不是不透明）");
    check(ink::ColorRed(data->normal.color) == ink::ColorRed(data->hover.color)
              && ink::ColorGreen(data->normal.color)
                     == ink::ColorGreen(data->hover.color)
              && ink::ColorBlue(data->normal.color)
                     == ink::ColorBlue(data->hover.color),
          "三态是同一个颜色、只有透明度不同（换色的话这条会红，那时改这里）");
    check(data->shape.kind == ink::ShapeKind::RoundedRect
              && data->shape.radius == 10.0f,
          "形状与配置一致（圆角 10）");
    check(data->selfAnchor.x == 0.5f && data->traceAnchor.x == 0.5f,
          "锚点与配置一致");

    // 2. 幂等：生成器的 RegisterAllButtons 撤下的是"上一批"，不动别人。
    {
        const std::size_t before = ink::ButtonLibrary::Count();
        ink::cxxcss::RegisterAllButtons();
        check(ink::ButtonLibrary::Count() == before,
              "再调一次 RegisterAllButtons 不会让库里的项数变化");
        check(ink::ButtonLibrary::IsNameTaken(ink::cxxcss::kNormalButtonName),
              "重复登记之后名字仍然在");
    }

    // 3. 按名字构造：拿到的就是配置里的几何。
    {
        ink::InkingStaticButton button(nullptr,
                                       ink::cxxcss::kNormalButtonName);
        check(button.GetWidth() == 200 && button.GetHeight() == 100,
              "按生成的名字常量构造，几何来自配置");
        check(button.GetShape().kind == ink::ShapeKind::RoundedRect,
              "形状也来自配置");
    }

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
