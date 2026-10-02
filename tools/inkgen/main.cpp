// inkgen：CXXCSS 配置 → C++ 代码的生成器。
//
// 用法：
//   inkgen --out-dir <目录> [--namespace ink::cxxcss] [--header button_service.h] <file.json>...
//   inkgen --validate <file.json>...
//
// 它是**构建期工具**：只依赖标准库（连 SDL 都不碰），所以在任何环境下都能编。
// 产物只落 build 目录，源码树保持干净（docs/AGENTS.md §3 第 6 条）。
//
// 只生成**组件**（今天就是 Button）：配置烘成字面量、每个名字一个类，
// 类型即名字，构造不查表。**场景不生成**——评估过，收益（组件不传 parent、
// 场景名有编译期保险）用"成员 + 默认成员初始化器 `{this}`"和一行常量
// 手写就能拿到，而生成它的代价是固定的（见 TaskGuide 的 T-7 那段记录）。

#include "Cxxcss.h"
#include "Emit.h"
#include "Json.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

void PrintUsage() {
    std::printf(
        "inkgen —— 把 CXXCSS/Button/*.json 生成成 C++ 代码\n"
        "\n"
        "用法：\n"
        "  inkgen --out-dir <目录> [--namespace <ns>] [--header <名>] <file.json>...\n"
        "  inkgen --validate <file.json>...\n"
        "\n"
        "选项：\n"
        "  --out-dir <目录>   产物目录（约定指向 build/，不存在会创建）\n"
        "  --namespace <ns>   生成代码的命名空间，默认 ink::cxxcss\n"
        "  --header <名>      生成的头文件名，默认 button_service.h\n"
        "  --validate         只校验，不写文件\n"
        "  -h, --help         显示这段帮助\n"
        "\n"
        "退出码：0 全部成功；1 有错误（错误逐条打印）。\n"
        "注意：*.example.json 按约定不参与生成（CXXCSS.md §1）。\n");
}

}  // namespace

int main(int argc, char** argv) {
    inkgen::EmitOptions options;
    bool validateOnly = false;
    bool sawOutDir = false;
    std::vector<std::string> inputs;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        if (arg == "-h" || arg == "--help") {
            PrintUsage();
            return 0;
        }
        if (arg == "--validate") {
            validateOnly = true;
            continue;
        }
        if (arg == "--out-dir" || arg == "--namespace" || arg == "--header") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "[错误] %s 后面缺少值\n", arg.c_str());
                return 1;
            }
            const std::string value = argv[++i];
            if (arg == "--out-dir") {
                options.outDir = value;
                sawOutDir = true;
            } else if (arg == "--namespace") {
                options.nameSpace = value;
            } else {
                options.headerName = value;
            }
            continue;
        }
        if (!arg.empty() && arg[0] == '-' && arg != "-") {
            std::fprintf(stderr, "[错误] 不认识的选项：%s\n", arg.c_str());
            PrintUsage();
            return 1;
        }
        inputs.push_back(arg);
    }

    if (inputs.empty()) {
        std::fprintf(stderr, "[错误] 没有输入文件\n\n");
        PrintUsage();
        return 1;
    }
    if (!validateOnly && !sawOutDir) {
        std::fprintf(stderr,
                     "[错误] 生成模式必须给 --out-dir（只校验请加 --validate）\n\n");
        PrintUsage();
        return 1;
    }

    // 用户单独点着示例文件说"生成这个"时提醒一句：它会被跳过。
    for (const std::string& path : inputs) {
        const std::string stem = inkgen::FileStem(path);
        if (stem.size() > 8 && stem.compare(stem.size() - 8, 8, ".example") == 0) {
            std::printf("[提示] %s 以 .example.json 结尾，按约定不参与生成，已跳过\n",
                        path.c_str());
        }
    }

    const inkgen::LoadResult loaded = inkgen::LoadButtonConfigs(inputs);

    for (const inkgen::ButtonConfig& button : loaded.buttons) {
        std::printf("[配置] %s  %dx%d  %s  shape=%d  显式外观段=%d\n",
                    button.name.c_str(), button.data.width,
                    button.data.height,
                    button.dynamic ? "动态" : "静态",
                    static_cast<int>(button.data.shape.kind),
                    (button.data.hoverInheritsNormal ? 1 : 2));
        for (const std::string& warning : button.warnings) {
            std::printf("[警告] %s：%s\n", button.filePath.c_str(),
                        warning.c_str());
        }
    }

    for (const inkgen::CxxcssError& error : loaded.errors) {
        std::fprintf(stderr, "[错误] %s\n", error.Format().c_str());
    }

    if (!loaded.Ok()) {
        std::fprintf(stderr, "\n%d 个文件里共 %zu 条错误，没有生成任何文件。\n",
                     static_cast<int>(inputs.size()), loaded.errors.size());
        return 1;
    }

    if (validateOnly) {
        std::printf("[完成] 校验通过：%zu 个按钮配置，未写文件\n",
                    loaded.buttons.size());
        return 0;
    }

    const inkgen::EmitResult emitted = inkgen::Emit(loaded.buttons, options);
    if (!emitted.ok) {
        std::fprintf(stderr, "[错误] 生成失败：%s\n", emitted.error.c_str());
        return 1;
    }

    std::printf("[完成] 生成 %zu 个按钮配置：\n  %s\n  %s\n",
                loaded.buttons.size(), emitted.headerPath.c_str(),
                emitted.sourcePath.c_str());
    return 0;
}
