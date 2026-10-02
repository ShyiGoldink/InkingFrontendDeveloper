// inkgen 自检：解析、校验、映射、报错，各来一遍。
//
// 这一层直接调 gen 的库（`inkgen_lib`），不 fork 进程：
//   - 报错信息是**字符串**，断言"消息里有没有关键信息"比断言退出码更有意义；
//   - fixture 用源码树里的文件（`INK_SOURCE_DIR` 是编译期定义，不靠工作目录）。
//
// 非 0 退出码表示有检查项失败。

#include "Cxxcss.h"
#include "Emit.h"
#include "Json.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
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

std::string sourceDir() {
#ifdef INK_SOURCE_DIR
    return INK_SOURCE_DIR;
#else
    return ".";
#endif
}

std::string fixture(const std::string& relative) {
    return sourceDir() + "/tests/fixtures/cxxcss/" + relative;
}

/** 一批错误里有没有一条包含这些关键信息的（文件 / 行号 / 词）。 */
bool HasError(const inkgen::LoadResult& result, const std::string& needle) {
    for (const inkgen::CxxcssError& error : result.errors) {
        if (error.Format().find(needle) != std::string::npos) {
            return true;
        }
    }
    return false;
}

/// 断言某份坏配置报错，并且报错信息里有 `needle`。
void expectError(const std::string& relative, const std::string& needle,
                 const std::string& what) {
    const inkgen::LoadResult result = inkgen::LoadButtonConfig(fixture(relative));
    const bool ok = !result.Ok() && HasError(result, needle);
    check(ok, what);
    if (!ok) {
        std::printf("      [期望] 报错里含：%s\n", needle.c_str());
        for (const inkgen::CxxcssError& error : result.errors) {
            std::printf("      [实得] %s\n", error.Format().c_str());
        }
    }
}

// ---------------------------------------------------------------------------
// 1. JSON 解析器本身
// ---------------------------------------------------------------------------

void testJson() {
    std::printf("\n== JSON 解析 ==\n");

    {
        const inkgen::ParseResult parsed =
            inkgen::ParseJson(R"({"a": 1, "b": [true, null, "x\n"], "c": {"d": 2.5}})");
        check(parsed.ok, "基本结构解析成功");
        const inkgen::JsonValue* a = parsed.value.Find("a");
        check(a != nullptr && a->IsNumber() && a->number == 1.0, "数字字段");
        const inkgen::JsonValue* b = parsed.value.Find("b");
        check(b != nullptr && b->IsArray() && b->items.size() == 3,
              "数组字段");
        check(b != nullptr && b->items[2].IsString()
                  && b->items[2].text == "x\n",
              "字符串转义（\\n）");
        const inkgen::JsonValue* d = parsed.value.Find("c")->Find("d");
        check(d != nullptr && d->number == 2.5, "嵌套对象");
    }

    {
        const inkgen::ParseResult parsed = inkgen::ParseJson(R"({"a":1,"a":2})");
        check(!parsed.ok && parsed.error.find("重复") != std::string::npos,
              "重复键报错（并指出第一次写在第几行）");
    }

    {
        const inkgen::ParseResult parsed =
            inkgen::ParseJson("{\n  \"a\": 1,\n  \"b\": tru\n}");
        check(!parsed.ok, "坏关键字报错");
        check(parsed.error.find("第 3 行") != std::string::npos,
              "报错带行号（实得：" + parsed.error + "）");
    }

    check(!inkgen::ParseJson("{\"a\": 1,}").ok, "尾随逗号报错");
    check(!inkgen::ParseJson("[1, 2").ok, "数组没闭合报错");
    check(!inkgen::ParseJson("\"abc").ok, "字符串没闭合报错");
    check(!inkgen::ParseJson("{\"a\":1} extra").ok, "末尾多余内容报错");

    // UTF-8 BOM：记事本另存为 UTF-8 会带上，不能因此解析失败。
    {
        const std::string withBom = "\xEF\xBB\xBF{\"a\":1}";
        check(inkgen::ParseJson(withBom).ok, "带 BOM 的文本能解析");
    }

    // 中文原样收进来（配置里的注释和文字都是中文）
    {
        const inkgen::ParseResult parsed =
            inkgen::ParseJson("{\"content\": {\"normal\": \"确定\"}}");
        const inkgen::JsonValue* normal =
            parsed.value.Find("content")->Find("normal");
        check(parsed.ok && normal != nullptr && normal->text == "确定",
              "中文原样保留");
    }
}

// ---------------------------------------------------------------------------
// 2. 合法配置 → ButtonData
// ---------------------------------------------------------------------------

void testGoodConfig() {
    std::printf("\n== 合法配置的映射 ==\n");

    const inkgen::LoadResult result =
        inkgen::LoadButtonConfig(fixture("Button/goodButton.json"));
    check(result.Ok(), "示例配置校验通过");
    if (!result.Ok()) {
        for (const inkgen::CxxcssError& error : result.errors) {
            std::printf("      [错误] %s\n", error.Format().c_str());
        }
        return;
    }

    check(result.buttons.size() == 1, "解析出 1 个按钮");
    if (result.buttons.empty()) {
        return;
    }

    const inkgen::ButtonConfig& button = result.buttons.front();
    const ink::ButtonData& data = button.data;

    check(data.name == "goodButton",
          "name 与文件名一致（实得 \"" + data.name + "\"，长度 "
              + std::to_string(data.name.size()) + "）");
    check(button.identifier == "GoodButton",
          "生成了类名 GoodButton（实得：" + button.identifier + "）");
    check(data.width == 200 && data.height == 100, "尺寸取自配置");
    check(!data.hoverInheritsNormal && !data.onclickedInheritsNormal,
          "三态都显式写了，不继承");
    check(ink::ColorAlpha(data.normal.color) == 191, "normal 0.75 → alpha 191");
    check(ink::ColorAlpha(data.hover.color) == 217, "hover 0.85 → alpha 217");
    check(ink::ColorAlpha(data.onclicked.color) == 191, "onclicked 0.75");
    check(ink::ColorRed(data.normal.color) == 0
              && ink::ColorGreen(data.normal.color) == 0
              && ink::ColorBlue(data.normal.color) == 0,
          "黑色三通道都是 0");
    check(data.shape.kind == ink::ShapeKind::RoundedRect
              && data.shape.radius == 10.0f,
          "形状是圆角 10");
    check(data.selfAnchor.x == 0.5f && data.selfAnchor.y == 0.5f
              && data.traceAnchor.x == 0.5f && data.traceAnchor.y == 0.5f,
          "锚点取自配置");
    check(data.label == "确定", "text.content.normal → label");
    check(data.text.fontSize == 16.0f && data.text.leftSpace == 12.0f,
          "text 的字号与左侧间距");
}

/// 尺寸写成字符串（历史写法）、没有 hover/onclicked、有 $comment。
void testInheritanceAndLegacy() {
    std::printf("\n== 省略三态 / 字符串尺寸 / 注释 ==\n");

    const inkgen::LoadResult result =
        inkgen::LoadButtonConfig(fixture("Button/legacyButton.json"));
    check(result.Ok(), "老写法（字符串尺寸）能过");
    if (!result.Ok() || result.buttons.empty()) {
        for (const inkgen::CxxcssError& error : result.errors) {
            std::printf("      [错误] %s\n", error.Format().c_str());
        }
        return;
    }

    const ink::ButtonData& data = result.buttons.front().data;
    check(data.width == 320 && data.height == 48, "字符串尺寸解析成整数");
    check(data.hoverInheritsNormal && data.onclickedInheritsNormal,
          "没写 hover/onclicked → 标记为继承");
    check(data.shape.kind == ink::ShapeKind::Rect, "没写 shape → 直角矩形");
}

// ---------------------------------------------------------------------------
// 3. 坏配置：一条一条都得报错，而且要说清哪里
// ---------------------------------------------------------------------------

void testBadConfigs() {
    std::printf("\n== 坏配置必须报错 ==\n");

    expectError("Button/badUnknownField.json", "不认识",
                "未知字段报错（并列出已知字段）");
    expectError("Button/badUnknownField.json", "widht",
                "报错信息里指出了那个拼错的字段名");

    expectError("Button/badNameMismatch.json", "文件名",
                "name 与文件名不一致时报错");

    expectError("Button/badUnknownShape.json", "shape.type",
                "不认识的形状类型报错");

    expectError("Button/badLaterTierShape.json", "还没实现",
                "第二档形状报「尚未实现」而不是「不认识」");

    expectError("Button/badColorChannels.json", "3 或 4 个通道",
                "颜色段数不对报错");

    expectError("Button/badNegativeRadius.json", "radius 不能为负",
                "负半径报错（不夹取）");

    expectError("Button/badRadiusString.json", "必须是数字",
                "radius 写成字符串报错");

    expectError("Button/badMissingSize.json", "缺少 width",
                "缺尺寸报错");

    expectError("Button/badSyntax.json", "第",
                "JSON 语法错报错（带行号）");

    expectError("Button/badUnknownTextField.json", "text 里的字段",
                "text 里的未知字段报错");

    // 没有 name 字段：配置里的名字必须与文件名一致，缺了就报"对不上"
    // （名字在文件里没有第二处来源，所以这条也是"缺 name"的报错）。
    expectError("Button/badMissingName.json", "name",
                "缺 name 报错");

    // img / svg：能过，但必须提醒"还没落地"（不算错误）
    {
        const inkgen::LoadResult result =
            inkgen::LoadButtonConfig(fixture("Button/imageButton.json"));
        check(result.Ok(), "img 类型能过（不是错误）");
        const bool warned = !result.buttons.empty()
                         && !result.buttons.front().warnings.empty();
        check(warned, "img 类型给出「还没落地」的提醒");
    }
}

// ---------------------------------------------------------------------------
// 4. 批量：example 跳过、重名拦下
// ---------------------------------------------------------------------------

void testBatch() {
    std::printf("\n== 批量加载 ==\n");

    const std::vector<std::string> inputs = {
        fixture("Button/goodButton.json"),
        fixture("Button/example.only.example.json"),  // .example.json → 跳过
        fixture("Button/legacyButton.json"),
    };
    const inkgen::LoadResult result = inkgen::LoadButtonConfigs(inputs);
    check(result.Ok(), "批量加载无误");
    check(result.buttons.size() == 2, "example 文件被跳过（实得 "
                                          + std::to_string(result.buttons.size())
                                          + " 个）");
    for (const inkgen::ButtonConfig& button : result.buttons) {
        check(button.name != "example.only", "example.only 确实没被收进来");
    }

    // 两个文件写同一个 name：必须拦下来（运行期会被库驳回，那是静默的错）
    // 两个文件认领同一个 name：必须拦下来（运行期会被库驳回，那是静默的错）
    const inkgen::LoadResult duplicate = inkgen::LoadButtonConfigs(
        {fixture("Button/dupOne.json"), fixture("Button/dupTwo.json")});
    check(!duplicate.Ok() && HasError(duplicate, "已经被"),
          "重名配置在生成期就被拦下（报的是「被谁用了」）");
    check(!duplicate.Ok() && HasError(duplicate, "dupOne.json"),
          "重名报错里指出了先占用的那个文件");
}

// ---------------------------------------------------------------------------
// 5. 发射：产物长什么样
// ---------------------------------------------------------------------------

void testEmit() {
    std::printf("\n== 生成产物 ==\n");

    const inkgen::LoadResult loaded = inkgen::LoadButtonConfigs(
        {fixture("Button/goodButton.json"), fixture("Button/legacyButton.json")});
    if (!loaded.Ok()) {
        check(false, "前置条件：配置能加载");
        return;
    }

    const std::filesystem::path outDir =
        std::filesystem::temp_directory_path() / "inkgen_test_out";
    std::filesystem::remove_all(outDir);

    inkgen::EmitOptions options;
    options.outDir = outDir.string();
    options.nameSpace = "ink::test_ns";

    const inkgen::EmitResult emitted = inkgen::Emit(loaded.buttons, options);
    check(emitted.ok, "生成成功" + (emitted.ok ? "" : ("：" + emitted.error)));
    if (!emitted.ok) {
        return;
    }

    const auto readAll = [](const std::string& path) {
        std::ifstream stream(path, std::ios::binary);
        std::string text;
        if (stream) {
            text.assign(std::istreambuf_iterator<char>(stream),
                        std::istreambuf_iterator<char>());
        }
        return text;
    };

    const std::string header = readAll(emitted.headerPath);
    const std::string source = readAll(emitted.sourcePath);

    check(header.find("namespace ink") != std::string::npos
              && header.find("namespace test_ns") != std::string::npos,
          "头文件用的是指定的命名空间");
    // 每个名字一个**独立的类**，数据烘在类里（不是运行期查表）。
    check(header.find("class GoodButton : public ink::InkingStaticButton")
              != std::string::npos,
          "头文件为每个名字生成了一个独立的类");
    check(header.find("static constexpr const char* kName = \"goodButton\"")
              != std::string::npos,
          "类里带着自己的名字（kName）");
    check(header.find("class LegacyButton") != std::string::npos,
          "第二个按钮的类也在");
    check(header.find("explicit GoodButton(ink::InkingAnchor* parent)")
              != std::string::npos,
          "类只要求父级——配置烘在里面，不需要外部传数据");
    check(header.find("static const ink::ButtonData& Data();")
              != std::string::npos,
          "类暴露 Data()（函数内静态量，避开静态初始化顺序问题）");
    check(header.find("RegisterAllButtons") != std::string::npos
              && header.find("void RegisterAllButtons();")
                     != std::string::npos,
          "头文件声明了登记入口");

    check(source.find("ink::ShapeSpec::RoundedRect(10.") != std::string::npos,
          "圆角形状生成成工厂调用");
    check(source.find("0xbf000000") != std::string::npos
              || source.find("0xBF000000") != std::string::npos,
          "颜色生成成 0xAARRGGBB 字面量");
    check(source.find("const ink::ButtonData& GoodButton::Data()")
              != std::string::npos
              && source.find("static const ink::ButtonData data = Build")
                     != std::string::npos,
          "Data() 用函数内静态量：只构造一次、拿到的永远是同一份");
    check(source.find("void GoodButton::Register()") != std::string::npos,
          "每个类有 Register()，登记表由 RegisterAllButtons 汇总");
    check(source.find("BeginRegistrationBatch") != std::string::npos
              && source.find("EndRegistrationBatch") != std::string::npos,
          "登记夹在批次入口之间（重复登记幂等）");
    check(source.find("gRegisterOnStartup") != std::string::npos,
          "有启动期就位的静态对象（不需要调用方记得手调）");
    check(source.find("#include <button_service.h>") != std::string::npos,
          "源文件 include 了自己那份头");

    // 重复生成必须幂等：内容一样时连文件都不重写（时间戳不动，下游不重编）。
    const auto stampBefore =
        std::filesystem::last_write_time(emitted.headerPath);
    const inkgen::EmitResult again = inkgen::Emit(loaded.buttons, options);
    const auto stampAfter =
        std::filesystem::last_write_time(emitted.headerPath);
    check(again.ok && stampBefore == stampAfter,
          "重复生成内容一样时不重写文件");

    // 命名空间非法要报错而不是生成出编译不过的代码。
    inkgen::EmitOptions badOptions = options;
    badOptions.nameSpace = "ink::9bad";
    check(!inkgen::Emit(loaded.buttons, badOptions).ok,
          "非法命名空间被拒绝");

    std::filesystem::remove_all(outDir);
}

// ---------------------------------------------------------------------------
// 6. 真实配置（CXXCSS/Button/*.json）也要能过
// ---------------------------------------------------------------------------

void testRealConfigs() {
    std::printf("\n== 仓库里的真实配置 ==\n");

    const std::string dir = sourceDir() + "/CXXCSS/Button";
    std::vector<std::string> inputs;
    if (std::filesystem::exists(dir)) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string path = entry.path().string();
            if (path.size() > 5 && path.compare(path.size() - 5, 5, ".json") == 0) {
                inputs.push_back(path);
            }
        }
    }

    check(!inputs.empty(), "CXXCSS/Button 下有配置文件");
    if (inputs.empty()) {
        return;
    }

    const inkgen::LoadResult result = inkgen::LoadButtonConfigs(inputs);
    for (const inkgen::CxxcssError& error : result.errors) {
        std::printf("      [错误] %s\n", error.Format().c_str());
    }
    check(result.Ok(), "仓库里的配置全部通过校验");
    check(!result.buttons.empty(),
          "至少有一个真实配置参与生成（实得 "
              + std::to_string(result.buttons.size()) + " 个）");
}

}  // namespace

int main() {
    std::printf("inkgen 自检（CXXCSS → C++）\n");

    testJson();
    testGoodConfig();
    testInheritanceAndLegacy();
    testBadConfigs();
    testBatch();
    testEmit();
    testRealConfigs();

    std::printf("\n失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
