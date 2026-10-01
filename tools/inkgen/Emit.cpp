#include "Emit.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace inkgen {

namespace {

/// 浮点字面量：整数值也写成 `10.0f`，免得在 C++ 里变成 int 再隐式转换。
std::string Float(double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.6ff", value);
    return buffer;
}

/// 0xAARRGGBB → C++ 字面量。
std::string Color(std::uint32_t argb) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "0x%08Xu", argb);
    return buffer;
}

/// 带引号的字符串字面量（转义反斜杠和双引号；UTF-8 中文原样留着）。
std::string Quote(const std::string& text) {
    std::string out = "\"";
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"':  out += "\\\""; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(ch); break;
        }
    }
    out += "\"";
    return out;
}

std::vector<std::string> SplitNamespace(const std::string& nameSpace) {
    std::vector<std::string> parts;
    std::string current;
    for (char ch : nameSpace) {
        if (ch == ':') {
            if (!current.empty()) {
                parts.push_back(current);
                current.clear();
            }
            continue;
        }
        current.push_back(ch);
    }
    if (!current.empty()) {
        parts.push_back(current);
    }
    return parts;
}

/// `ink::cxxcss::RegisterAllButtons` —— 生成代码里引用自己的完整名字时用。
std::string Qualified(const std::string& nameSpace, const std::string& symbol) {
    return nameSpace + "::" + symbol;
}

std::string OpenNamespaces(const std::vector<std::string>& parts,
                           int indentSpaces = 0) {
    std::string out;
    std::string indent(static_cast<std::size_t>(indentSpaces), ' ');
    for (const std::string& part : parts) {
        out += indent + "namespace " + part + " {\n";
    }
    return out;
}

std::string CloseNamespaces(const std::vector<std::string>& parts,
                            int indentSpaces = 0) {
    std::string out;
    std::string indent(static_cast<std::size_t>(indentSpaces), ' ');
    for (std::size_t i = 0; i < parts.size(); ++i) {
        out += indent + "}  // namespace " + parts[parts.size() - 1 - i] + "\n";
    }
    return out;
}

/// 缩进多行文本；空行不缩进（免得行尾留空格）。
std::string Indent(const std::string& text, int spaces) {
    const std::string pad(static_cast<std::size_t>(spaces), ' ');
    std::string out;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t end = text.find('\n', start);
        const std::string line = (end == std::string::npos)
                                     ? text.substr(start)
                                     : text.substr(start, end - start);
        if (!line.empty()) {
            out += pad;
        }
        out += line;
        if (end == std::string::npos) {
            break;
        }
        out += '\n';
        start = end + 1;
    }
    return out;
}

/**
 * 一份配置 → 一个"造 ButtonData"的表达式块。
 *
 * 用**逐字段赋值**而不是聚合初始化 `{...}`：字段多、顺序敏感，聚合初始化一旦
 * 有人插了新成员就会静默错位（值都还在，只是落到了别的字段上）。
 * 赋值写法还顺手给生成代码加了可读性——看输出就知道哪个值对应哪个字段。
 */
std::string ButtonBody(const ButtonConfig& button, const std::string& indent) {
    const ink::ButtonData& data = button.data;
    std::ostringstream out;

    out << indent << "ink::ButtonData data;\n";
    out << indent << "data.name = " << Quote(data.name) << ";\n";
    out << indent << "data.width = " << data.width << ";\n";
    out << indent << "data.height = " << data.height << ";\n";

    const auto appearance = [&](const std::string& field,
                                const ink::ButtonAppearance& value) {
        out << indent << "data." << field << ".type = "
            << (value.type == ink::AppearanceType::Color
                    ? "ink::AppearanceType::Color"
                    : (value.type == ink::AppearanceType::Image
                           ? "ink::AppearanceType::Image"
                           : "ink::AppearanceType::Svg"))
            << ";\n";
        out << indent << "data." << field << ".color = " << Color(value.color)
            << ";\n";
        if (!value.source.empty()) {
            out << indent << "data." << field << ".source = "
                << Quote(value.source) << ";\n";
        }
    };

    appearance("normal", data.normal);
    if (!data.hoverInheritsNormal) {
        appearance("hover", data.hover);
    }
    if (!data.onclickedInheritsNormal) {
        appearance("onclicked", data.onclicked);
    }

    out << indent << "data.hoverInheritsNormal = "
        << (data.hoverInheritsNormal ? "true" : "false") << ";\n";
    out << indent << "data.onclickedInheritsNormal = "
        << (data.onclickedInheritsNormal ? "true" : "false") << ";\n";

    out << indent << "data.text.fontSize = " << Float(data.text.fontSize)
        << ";\n";
    out << indent << "data.text.leftSpace = " << Float(data.text.leftSpace)
        << ";\n";
    out << indent << "data.text.topSpace = " << Float(data.text.topSpace)
        << ";\n";
    if (!data.text.path.empty()) {
        out << indent << "data.text.path = " << Quote(data.text.path) << ";\n";
    }
    if (!data.label.empty()) {
        out << indent << "data.label = " << Quote(data.label) << ";\n";
    }

    out << indent << "data.selfAnchor = ink::Anchor{" << Float(data.selfAnchor.x)
        << ", " << Float(data.selfAnchor.y) << "};\n";
    out << indent << "data.traceAnchor = ink::Anchor{"
        << Float(data.traceAnchor.x) << ", " << Float(data.traceAnchor.y)
        << "};\n";
    out << indent << "data.offsetX = " << Float(data.offsetX) << ";\n";
    out << indent << "data.offsetY = " << Float(data.offsetY) << ";\n";
    out << indent << "data.zIndex = " << data.zIndex << ";\n";
    out << indent << "data.visible = " << (data.visible ? "true" : "false")
        << ";\n";

    switch (data.shape.kind) {
        case ink::ShapeKind::RoundedRect:
            out << indent << "data.shape = ink::ShapeSpec::RoundedRect("
                << Float(data.shape.radius) << ");\n";
            break;
        case ink::ShapeKind::Circle:
            out << indent << "data.shape = ink::ShapeSpec::Circle();\n";
            break;
        case ink::ShapeKind::Ellipse:
            out << indent << "data.shape = ink::ShapeSpec::Ellipse();\n";
            break;
        case ink::ShapeKind::Rect:
        default:
            out << indent << "data.shape = ink::ShapeSpec::Rect();\n";
            break;
    }

    out << indent << "return data;\n";
    return out.str();
}

std::string MakeHeader(const std::vector<ButtonConfig>& buttons,
                       const EmitOptions& options) {
    const std::vector<std::string> parts = SplitNamespace(options.nameSpace);
    std::ostringstream out;

    out << "// 本文件由 inkgen 生成，不要手改。\n";
    out << "// 来源：CXXCSS/Button/*.json\n";
    out << "// 改配置请改 json；改生成规则请改 tools/inkgen/Emit.cpp。\n";
    out << "#pragma once\n\n";
    out << "#include <button/ButtonData.h>\n\n";
    out << "#include <cstddef>\n\n";
    out << OpenNamespaces(parts);
    out << "\n/// 每个按钮的查找键。写代码时用这些常量，拼错名字编译期就红。\n";

    for (const ButtonConfig& button : buttons) {
        if (button.identifier.empty()) {
            out << "// 名字 \"" << button.name
                << "\" 不是合法标识符，没有生成常量；请用字符串字面量按名构造。\n";
            continue;
        }
        out << "inline constexpr const char* " << button.identifier << " = "
            << Quote(button.name) << ";\n";
    }

    out << "\n/// 本次生成里有多少个按钮配置。\n";
    out << "inline constexpr std::size_t kButtonCount = " << buttons.size()
        << ";\n";

    out << "\n/// 把本文件里的全部按钮配置登记进 ink::ButtonLibrary。\n";
    out << "/// 幂等：重复调用会先把上一次登记的这批撤下再重新登记。\n";
    out << "void RegisterAllButtons();\n";

    out << CloseNamespaces(parts);
    return out.str();
}

std::string MakeSource(const std::vector<ButtonConfig>& buttons,
                       const EmitOptions& options) {
    const std::vector<std::string> parts = SplitNamespace(options.nameSpace);
    const std::string registerAll = Qualified(options.nameSpace,
                                              "RegisterAllButtons");
    std::ostringstream out;

    out << "// 本文件由 inkgen 生成，不要手改。\n";
    out << "#include <" << options.headerName << ">\n\n";
    out << "#include <button/ButtonLibrary.h>\n\n";
    out << "#include <vector>\n\n";

    // ---- 每个按钮一个工厂函数（内部链接，不导出符号） ----
    if (!buttons.empty()) {
        out << "namespace {\n\n";
        for (std::size_t i = 0; i < buttons.size(); ++i) {
            const ButtonConfig& button = buttons[i];
            out << "/// " << button.name << "（来自 " << button.filePath
                << "）\n";
            out << "ink::ButtonData Make" << i << "() {\n";
            out << ButtonBody(button, "    ");
            out << "}\n\n";
        }
        out << "}  // namespace\n\n";
    }

    out << OpenNamespaces(parts);
    out << "\nvoid RegisterAllButtons() {\n";
    out << "    // 先把上一批（同一个生成集合）撤下，再重新登记：这样重复调用、\n";
    out << "    // 运行期重建都不报 NameTaken，也不会碰到别的库内容。\n";
    out << "    ink::ButtonLibrary::BeginRegistrationBatch();\n\n";
    out << "    std::vector<ink::ButtonData> configs;\n";
    out << "    configs.reserve(kButtonCount);\n";
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        out << "    configs.push_back(Make" << i << "());\n";
    }
    out << "\n    for (const ink::ButtonData& config : configs) {\n";
    out << "        ink::ButtonLibrary::Register(config);\n";
    out << "    }\n\n";
    out << "    ink::ButtonLibrary::EndRegistrationBatch();\n";
    out << "}\n";

    out << CloseNamespaces(parts);
    out << "\nnamespace {\n\n";
    out << "/**\n";
    out << " * 启动期就把配置登记好。\n";
    out << " *\n";
    out << " * 为什么不靠\"第一次构造按钮时再登记\"：那要求每个构造函数都记得调一次，\n";
    out << " * 漏一次就退化成\"按名字构造拿到默认外观\"——一个静默的错。\n";
    out << " * 依赖静态初始化顺序的写法在这里是**安全**的：库本身用函数内静态量，\n";
    out << " * 谁先碰它谁把它拉起来，所以这个对象的构造一定发生在库可用之后。\n";
    out << " */\n";
    out << "struct RegisterOnStartup {\n";
    out << "    RegisterOnStartup() { " << registerAll << "(); }\n";
    out << "} gRegisterOnStartup;\n\n";
    out << "}  // namespace\n";
    return out.str();
}

bool WriteFile(const std::string& path, const std::string& text,
               std::string& error) {
    // 只在内容真的变了时才写：产物时间戳不动，下游就不会被无谓地重新编译。
    {
        std::ifstream existing(path, std::ios::binary);
        if (existing) {
            std::ostringstream buffer;
            buffer << existing.rdbuf();
            if (buffer.str() == text) {
                return true;
            }
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        error = "写不了文件：" + path;
        return false;
    }
    out << text;
    if (!out) {
        error = "写入失败：" + path;
        return false;
    }
    return true;
}

}  // namespace

EmitResult Emit(const std::vector<ButtonConfig>& buttons,
                const EmitOptions& options) {
    EmitResult result;

    if (options.outDir.empty()) {
        result.error = "没有指定 --out-dir";
        return result;
    }

    const std::vector<std::string> parts = SplitNamespace(options.nameSpace);
    if (parts.empty()) {
        result.error = "命名空间为空（--namespace 形如 ink::cxxcss）";
        return result;
    }
    for (const std::string& part : parts) {
        if (!IsCxxIdentifier(part)) {
            result.error = "命名空间段 \"" + part + "\" 不是合法标识符";
            return result;
        }
    }
    // 头文件名：相对路径，默认 `inkgen/button_service.h`。
    // 目录那几段会出现在生成源文件的 `#include` 里，也是 CMake 那边加进
    // include 路径的东西——这么设计是为了让产物永远写成
    // `#include <inkgen/xxx/button_service.h>`，和别的头文件放在一起也不撞名。
    // 只拦两件真会出事的事：绝对路径、以及".."这种会写到源码树里去的路径。
    const std::filesystem::path headerRel(options.headerName);
    if (headerRel.is_absolute()) {
        result.error = "头文件名 \"" + options.headerName
                     + "\" 不合法（要相对路径，产物必须落在 build 目录里）";
        return result;
    }
    for (const auto& part : headerRel) {
        if (part == "..") {
            result.error = "头文件名 \"" + options.headerName
                         + "\" 不合法（不允许 ..，产物必须落在 build 目录里）";
            return result;
        }
    }

    std::string stem = headerRel.filename().string();
    for (const char* suffix : {".hpp", ".h"}) {
        const std::size_t length = std::char_traits<char>::length(suffix);
        if (stem.size() > length
            && stem.compare(stem.size() - length, length, suffix) == 0) {
            stem.erase(stem.size() - length);
            break;
        }
    }
    if (stem.empty() || !IsCxxIdentifier(stem)) {
        result.error = "头文件名 \"" + options.headerName
                     + "\" 不合法（主体要是合法标识符，例如 button_service.h）";
        return result;
    }

    std::error_code code;
    // 产物目录 = --out-dir + HEADER 的目录部分。--out-dir 是"include 根"，
    // 而 HEADER 可能带子目录（`inkgen/buttons_demo/button_service.h`）。
    // 生成源文件里 include 的是 HEADER 原样的相对路径，所以两者必须对齐。
    const std::filesystem::path dir =
        headerRel.has_parent_path()
            ? std::filesystem::path(options.outDir) / headerRel.parent_path()
            : std::filesystem::path(options.outDir);
    std::filesystem::create_directories(dir, code);
    if (code) {
        result.error = "建不了输出目录 " + dir.string() + "：" + code.message();
        return result;
    }

    result.headerPath = (dir / headerRel.filename()).string();
    result.sourcePath = (dir / "button_register.cpp").string();

    if (!WriteFile(result.headerPath, MakeHeader(buttons, options),
                   result.error)) {
        return result;
    }
    if (!WriteFile(result.sourcePath, MakeSource(buttons, options),
                   result.error)) {
        return result;
    }

    result.ok = true;
    return result;
}

}  // namespace inkgen
