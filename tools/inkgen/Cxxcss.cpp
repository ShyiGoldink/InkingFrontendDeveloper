#include "Cxxcss.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace inkgen {

namespace {

/// 接受的全部字段名。不在这里出现的一律报错（CXXCSS.md §2.1）。
const char* const kKnownRootFields[] = {
    "$comment",    "name",        "width",        "height",
    "normal",      "hover",       "onclicked",    "text",
    "selfAnchorX", "selfAnchorY", "traceAnchorX", "traceAnchorY",
    "offsetX",     "offsetY",     "zIndex",       "visible",
    "shape",       "dynamic",
};

const char* const kKnownTextFields[] = {
    "fontSize", "leftSpace", "topSpace", "path", "content",
};

bool IsKnownField(const char* const* table, std::size_t count,
                  const std::string& key) {
    for (std::size_t i = 0; i < count; ++i) {
        if (key == table[i]) {
            return true;
        }
    }
    return false;
}

/// 把"已知字段"拼成一句给用户看的提示。
std::string JoinKnownFields(const char* const* table, std::size_t count) {
    std::string joined;
    for (std::size_t i = 0; i < count; ++i) {
        if (i != 0) {
            joined += "、";
        }
        joined += table[i];
    }
    return joined;
}

/**
 * @brief 一个文件的加载器：一次收齐所有错误，不早退。
 *
 * 早退会让用户"改一条、跑一次、又冒一条"；配置错误往往是一次写错一片，
 * 所以这里攒着一起报。
 *
 * 它直接往调用方给的 `LoadResult` 上追加（成功进 `buttons`、问题进 `errors`），
 * 不自己攒一份再合并——重名检查要看的是**这一批文件到目前为止**的认领情况，
 * 一份一份地"各自校验完再合并"就会漏掉"第二个文件撞上第一个"（踩过：
 * 那份实现里每个文件都只看得到自己，于是重名永远检不出来）。
 */
class ButtonLoader {
public:
    ButtonLoader(std::string path, LoadResult& result)
        : _path(std::move(path)), _result(result) {
        _config.filePath = _path;
        // 先用文件名当默认值：配置里没写 name 时也还有话可说。
        _config.name = FileStem(_path);
    }

    void Run(const JsonValue& root) {
        if (!root.IsObject()) {
            Error(root, std::string("顶层必须是对象，这里是") + root.KindName());
            _failed = true;
            Finish();
            return;
        }

        for (const auto& field : root.fields) {
            const std::string& key = field.first;
            const JsonValue& value = field.second;

            if (key == "$comment") {
                continue;  // CXXCSS.md §2.1：注释字段，生成器跳过
            }
            if (!IsKnownField(kKnownRootFields,
                              std::size(kKnownRootFields), key)) {
                Error(value, "字段 \"" + key + "\" 不认识（已知字段："
                                 + JoinKnownFields(kKnownRootFields,
                                                   std::size(kKnownRootFields))
                                 + "）");
                continue;
            }

            if (key == "name") {
                if (value.IsString()) {
                    _config.name = value.text;
                    _sawNameField = true;
                } else {
                    Error(value, "name 必须是字符串");
                }
            } else if (key == "width") {
                _config.data.width = ReadSize(value, "width");
                _sawWidth = true;
            } else if (key == "height") {
                _config.data.height = ReadSize(value, "height");
                _sawHeight = true;
            } else if (key == "normal") {
                ReadAppearance(value, "normal", _config.data.normal,
                               &_hasNormal);
            } else if (key == "hover") {
                ReadAppearance(value, "hover", _config.data.hover, nullptr);
                _config.data.hoverInheritsNormal = false;
            } else if (key == "onclicked") {
                ReadAppearance(value, "onclicked", _config.data.onclicked,
                               nullptr);
                _config.data.onclickedInheritsNormal = false;
            } else if (key == "text") {
                ReadText(value);
            } else if (key == "selfAnchorX") {
                ReadAnchor(value, "selfAnchorX", _config.data.selfAnchor.x);
            } else if (key == "selfAnchorY") {
                ReadAnchor(value, "selfAnchorY", _config.data.selfAnchor.y);
            } else if (key == "traceAnchorX") {
                ReadAnchor(value, "traceAnchorX", _config.data.traceAnchor.x);
            } else if (key == "traceAnchorY") {
                ReadAnchor(value, "traceAnchorY", _config.data.traceAnchor.y);
            } else if (key == "offsetX") {
                _config.data.offsetX = ReadNumber(value, "offsetX");
            } else if (key == "offsetY") {
                _config.data.offsetY = ReadNumber(value, "offsetY");
            } else if (key == "zIndex") {
                _config.data.zIndex
                    = static_cast<int>(ReadNumber(value, "zIndex"));
            } else if (key == "visible") {
                if (value.IsBool()) {
                    _config.data.visible = value.boolean;
                } else {
                    Error(value, "visible 必须是 true 或 false");
                }
            } else if (key == "dynamic") {
                // 生成期的一个"选哪个基类"的开关，不进运行期数据结构。
                // 所以它只认 bool：写成字符串或数字都是笔误，
                // 而"悄悄按 truthy 处理"会让配置看着生效、其实没生效。
                if (value.IsBool()) {
                    _config.dynamic = value.boolean;
                } else {
                    Error(value, "dynamic 必须是 true 或 false（它决定生成的类"
                                     "继承 InkingStaticButton 还是 "
                                     "InkingDynamicButton）");
                }
            } else if (key == "shape") {
                ReadShape(value);
            }
        }

        Finish();
    }

private:
    std::string _path;
    LoadResult& _result;
    ButtonConfig _config;
    bool _failed = false;
    bool _hasNormal = false;
    bool _sawNameField = false;
    bool _sawWidth = false;
    bool _sawHeight = false;

    void Error(const JsonValue& where, const std::string& message) {
        _result.errors.push_back(
            CxxcssError{_path, where.where.line, message});
        _failed = true;
    }

    void Warn(const std::string& message) {
        _config.warnings.push_back(message);
    }

    /// 数字（只认 JSON 数字，不认字符串）。
    double ReadNumber(const JsonValue& value, const std::string& field) {
        if (!value.IsNumber()) {
            Error(value, field + " 必须是数字（不要加引号），这里是"
                             + value.KindName());
            return 0.0;
        }
        return value.number;
    }

    /// 尺寸：数字或数字字符串（历史遗留写法，CXXCSS.md §2.1 两种都收）。
    int ReadSize(const JsonValue& value, const std::string& field) {
        double number = 0.0;
        if (value.IsNumber()) {
            number = value.number;
        } else if (value.IsString()) {
            char* end = nullptr;
            number = std::strtod(value.text.c_str(), &end);
            if (end == nullptr || *end != '\0' || value.text.empty()) {
                Error(value, field + " 的字符串写法必须是纯数字，这里是 \""
                                 + value.text + "\"");
                return 0;
            }
        } else {
            Error(value, field + " 必须是数字，这里是" + value.KindName());
            return 0;
        }

        if (number < 1.0) {
            Error(value, field + " 必须大于 0（现在是 "
                             + std::to_string(number) + "）");
            return 0;
        }
        return static_cast<int>(number + 0.5);
    }

    void ReadAnchor(const JsonValue& value, const std::string& field,
                    float& out) {
        const double number = ReadNumber(value, field);
        out = static_cast<float>(number);
        // 锚点允许任意浮点，但超出 [0,1] 的行为未定义（CXXCSS.md §2.2），提醒一句。
        if (number < 0.0 || number > 1.0) {
            Warn(field + " = " + std::to_string(number)
                 + " 落在 [0,1] 之外，越界行为未定义");
        }
    }

    /// `"0|0|0|0.75"` → 0xAARRGGBB。
    std::uint32_t ReadColorSource(const JsonValue& value,
                                  const std::string& field) {
        if (!value.IsString()) {
            Error(value, field + ".source 必须是字符串，这里是"
                             + value.KindName());
            return 0;
        }

        std::vector<double> parts;
        std::string token;
        const std::string& text = value.text;
        for (std::size_t i = 0; i <= text.size(); ++i) {
            if (i == text.size() || text[i] == '|') {
                if (token.empty()) {
                    Error(value, field + ".source \"" + text
                                     + "\" 里有空的通道");
                    return 0;
                }
                char* end = nullptr;
                const double parsed = std::strtod(token.c_str(), &end);
                if (end == nullptr || *end != '\0') {
                    Error(value, field + ".source \"" + text
                                     + "\" 里的 \"" + token + "\" 不是数字");
                    return 0;
                }
                parts.push_back(parsed);
                token.clear();
            } else {
                token.push_back(text[i]);
            }
        }

        if (parts.size() != 3 && parts.size() != 4) {
            Error(value, field + ".source \"" + text + "\" 要有 3 或 4 个通道"
                             "（r|g|b 或 r|g|b|a），现在有 "
                             + std::to_string(parts.size()) + " 个");
            return 0;
        }

        for (double part : parts) {
            if (part < 0.0 || part > 1.0) {
                Warn(field + ".source 里的通道 " + std::to_string(part)
                     + " 落在 [0,1] 之外，会被夹住");
            }
        }

        const float alpha = parts.size() == 4 ? static_cast<float>(parts[3])
                                              : 1.0f;
        return ink::FromUnitRgba(static_cast<float>(parts[0]),
                                 static_cast<float>(parts[1]),
                                 static_cast<float>(parts[2]), alpha);
    }

    void ReadAppearance(const JsonValue& value, const std::string& field,
                        ink::ButtonAppearance& out, bool* sawNormal) {
        if (!value.IsObject()) {
            Error(value, field + " 必须是对象，形如 "
                             "{\"type\":\"color\",\"source\":\"0|0|0|0.75\"}");
            return;
        }

        for (const auto& entry : value.fields) {
            if (entry.first != "type" && entry.first != "source"
                && entry.first != "transition" && entry.first != "transform") {
                Error(entry.second,
                      field + " 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 type、source、transition、"
                            "transform）");
            }
        }

        const JsonValue* type = value.Find("type");
        const JsonValue* source = value.Find("source");

        if (type == nullptr) {
            Error(value, field + " 缺少 type（color / img / svg）");
            return;
        }
        if (!type->IsString()) {
            Error(*type, field + ".type 必须是字符串");
            return;
        }

        std::string typeName = type->text;
        std::transform(typeName.begin(), typeName.end(), typeName.begin(),
                       [](unsigned char ch) {
                           return static_cast<char>(std::tolower(ch));
                       });

        if (typeName == "color") {
            out.type = ink::AppearanceType::Color;
            if (source == nullptr) {
                Error(value, field + " 缺少 source（color 要写 \"r|g|b|a\"）");
                return;
            }
            out.color = ReadColorSource(*source, field);
            out.source = source->IsString() ? source->text : std::string();
        } else if (typeName == "img") {
            out.type = ink::AppearanceType::Image;
            if (source == nullptr || !source->IsString()) {
                Error(value, field + " 的 img 需要 source（图像绝对路径）");
                return;
            }
            out.source = source->text;
            Warn(field + " 用的是 img，但纹理层还没落地：现在这个类型的按钮"
                         "不会画出图片");
        } else if (typeName == "svg") {
            out.type = ink::AppearanceType::Svg;
            if (source == nullptr || !source->IsString()) {
                Error(value, field + " 的 svg 需要 source（SVG 代码）");
                return;
            }
            out.source = source->text;
            Warn(field + " 用的是 svg，但它还没落地：现在这个类型的按钮"
                         "不会画出图形");
        } else {
            Error(*type, field + ".type \"" + type->text
                             + "\" 不认识（只支持 color、img、svg）");
            return;
        }

        // 过渡写在状态自己身上（TaskGuide 的 D-1）：进了这个状态就用它的时长。
        // 外观本身有错（上面已经 return）时不再解析它——那条错更值得先修。
        if (const JsonValue* transition = value.Find("transition")) {
            ReadTransition(*transition, field, out);
        }

        // 变换同样是"这个状态自己带一份"：进这个状态就朝这份变换走。
        if (const JsonValue* transform = value.Find("transform")) {
            ReadTransform(*transform, field, out);
        }

        if (sawNormal != nullptr) {
            *sawNormal = true;
        }
    }

    /**
     * `"transition": { "duration": 0.15 }` —— 进入这个状态时的过渡时长（秒）。
     *
     * 只认 `duration`：**缓动曲线还没做**，现在一律 linear。所以这里**不给**
     * `easing` 留位——一个只能填一个值的开关，只会让人以为有得选。
     * 真要曲线时再加，那时它是新字段，不会和现在这份配置冲突。
     */
    void ReadTransition(const JsonValue& value, const std::string& field,
                        ink::ButtonAppearance& out) {
        if (!value.IsObject()) {
            Error(value, field
                             + ".transition 必须是对象，形如 {\"duration\":0.15}");
            return;
        }

        for (const auto& entry : value.fields) {
            if (entry.first != "duration") {
                Error(entry.second,
                      field + ".transition 里的字段 \"" + entry.first
                          + "\" 不认识（这一层只认 duration；缓动曲线还没做，"
                            "现在一律 linear）");
                continue;
            }

            const double seconds
                = ReadNumber(entry.second, field + ".transition.duration");
            if (seconds <= 0.0) {
                Error(entry.second,
                      field + ".transition.duration 必须大于 0（现在是 "
                          + std::to_string(seconds)
                          + "）；不想要过渡就别写 transition");
                continue;
            }
            out.transitionSeconds = static_cast<float>(seconds);
        }
    }

    /**
     * `"transform": { "translateX": 0, "translateY": -4, "rotate": 0, "scale": 1.05 }`
     * —— **变换通道**（平移 / 旋转 / 缩放）。四个字段都可选，不写就是单位变换。
     *
     * 它**不改本地几何、也不改命中表**（`docs/InputDesign.md` §11：表按本地形状烘，
     * 查询时把点反变换回本地空间），所以**静态档也能配**——这是"静态按钮也能
     * 移动 / 旋转"的根据。
     *
     * 两条约定（运行期同一份定义在 `include/ink/dataStruct/InkingTransform.h`）：
     *   - 原点是**组件中心**（等价 CSS 的 `transform-origin: 50% 50%`）；
     *   - `rotate` 的单位是**度**，顺时针为正。
     */
    void ReadTransform(const JsonValue& value, const std::string& field,
                       ink::ButtonAppearance& out) {
        if (!value.IsObject()) {
            Error(value,
                  field + ".transform 必须是对象，形如 "
                          "{\"translateY\":-4,\"scale\":1.05}");
            return;
        }

        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;

            if (key == "translateX") {
                out.transform.translateX = static_cast<float>(
                    ReadNumber(child, field + ".transform.translateX"));
            } else if (key == "translateY") {
                out.transform.translateY = static_cast<float>(
                    ReadNumber(child, field + ".transform.translateY"));
            } else if (key == "rotate") {
                out.transform.rotate = static_cast<float>(
                    ReadNumber(child, field + ".transform.rotate"));
            } else if (key == "scale") {
                const double scale = ReadNumber(child, field + ".transform.scale");
                if (scale == 0.0) {
                    Error(child, field + ".transform.scale 不能是 0"
                                     "（那会把形状压成一个点；要「消失」"
                                     "请缩到 0.01 之类）");
                    continue;
                }
                out.transform.scale = static_cast<float>(scale);
            } else {
                Error(child,
                      field + ".transform 里的字段 \"" + key
                          + "\" 不认识（只认 translateX、translateY、rotate、scale）");
            }
        }
    }

    void ReadText(const JsonValue& value) {
        if (!value.IsObject()) {
            Error(value, "text 必须是对象");
            return;
        }

        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            const JsonValue& child = entry.second;
            if (!IsKnownField(kKnownTextFields, std::size(kKnownTextFields),
                              key)) {
                Error(child, "text 里的字段 \"" + key + "\" 不认识（已知："
                                 + JoinKnownFields(kKnownTextFields,
                                                   std::size(kKnownTextFields))
                                 + "）");
                continue;
            }

            if (key == "fontSize") {
                _config.data.text.fontSize
                    = static_cast<float>(ReadNumber(child, "text.fontSize"));
            } else if (key == "leftSpace") {
                _config.data.text.leftSpace
                    = static_cast<float>(ReadNumber(child, "text.leftSpace"));
            } else if (key == "topSpace") {
                _config.data.text.topSpace
                    = static_cast<float>(ReadNumber(child, "text.topSpace"));
            } else if (key == "path") {
                if (child.IsString()) {
                    _config.data.text.path = child.text;
                } else {
                    Error(child, "text.path 必须是字符串");
                }
            } else if (key == "content") {
                ReadTextContent(child);
            }
        }
    }

    void ReadTextContent(const JsonValue& value) {
        if (!value.IsObject()) {
            Error(value, "text.content 必须是对象，形如 "
                         "{\"normal\":\"确定\"}");
            return;
        }
        for (const auto& entry : value.fields) {
            const std::string& key = entry.first;
            if (key != "normal" && key != "hover" && key != "onClicked"
                && key != "onclicked") {
                Error(entry.second, "text.content 里的字段 \"" + key
                                        + "\" 不认识（只认 normal / hover / "
                                          "onClicked）");
                continue;
            }
            if (!entry.second.IsString()) {
                Error(entry.second,
                      "text.content." + key + " 必须是字符串");
                continue;
            }

            if (key == "normal") {
                // 三态文字里今天只有 normal 能画（hover / onClicked 要等换字形的
                // 通道）。看到另外两态与 normal 不同时提醒一句，别让人以为生效了。
                _config.data.label = entry.second.text;
            } else if (!_config.data.label.empty()
                       && entry.second.text != _config.data.label) {
                Warn("text.content." + key
                     + " 与 normal 不同，但运行时只画 normal 那一份"
                       "（换字形的通道还没做）");
            }
        }
    }

    void ReadShape(const JsonValue& value) {
        if (!value.IsObject()) {
            Error(value, "shape 必须是对象，形如 "
                         "{\"type\":\"roundedRect\",\"radius\":10}");
            return;
        }

        const JsonValue* type = value.Find("type");
        if (type == nullptr) {
            // shape 写成 {} 等价于直角矩形（CXXCSS.md §3.2）。
            _config.data.shape = ink::ShapeSpec::Rect();
            return;
        }
        if (!type->IsString()) {
            Error(*type, "shape.type 必须是字符串");
            return;
        }

        for (const auto& entry : value.fields) {
            if (entry.first == "type" || entry.first == "radius") {
                continue;
            }
            Error(entry.second, "shape 里的字段 \"" + entry.first
                                    + "\" 不认识（这一层只认 type 和 radius）");
        }

        const std::string& name = type->text;
        if (name == "rect") {
            _config.data.shape = ink::ShapeSpec::Rect();
        } else if (name == "roundedRect") {
            const JsonValue* radius = value.Find("radius");
            if (radius == nullptr) {
                Error(*type, "roundedRect 需要 radius（设计坐标，不能为负）");
                return;
            }
            const double value = ReadNumber(*radius, "shape.radius");
            if (value < 0.0) {
                Error(*radius, "shape.radius 不能为负（现在是 "
                                   + std::to_string(value) + "）");
                return;
            }
            // 上限由形状自己夹（min(w,h)/2），这是数学要求不是配置错误。
            _config.data.shape
                = ink::ShapeSpec::RoundedRect(static_cast<float>(value));
        } else if (name == "circle") {
            _config.data.shape = ink::ShapeSpec::Circle();
        } else if (name == "ellipse") {
            _config.data.shape = ink::ShapeSpec::Ellipse();
        } else {
            // 第二 / 第三档：格式里有定义，但形状层还没实现——报"尚未实现"
            // 比报"不认识"更准确，也告诉用户是等实现而不是写错了。
            static const char* const kLaterTiers[] = {
                "capsule", "ring", "line", "polygon",
                "arc",     "star", "chamferRect", "superellipse",
            };
            for (const char* later : kLaterTiers) {
                if (name == later) {
                    Error(*type, "shape.type \"" + name
                                     + "\" 在格式里有定义，但形状层还没实现"
                                       "（现在支持：rect、roundedRect、circle、"
                                       "ellipse）");
                    return;
                }
            }
            Error(*type, "shape.type \"" + name
                             + "\" 不认识（现在支持：rect、roundedRect、"
                               "circle、ellipse）");
        }
    }

    /// 收尾：必填项、重名、文件名与 name 的一致性。
    void Finish() {
        if (!_sawNameField) {
            // 配置里根本没写 name。文件名虽然能兜出名字，但 CXXCSS.md §1 第 2 条
            // 要的是两处**都写着且一致**——缺一处就是缺一处，不能悄悄替它决定。
            _result.errors.push_back(CxxcssError{
                _path, 0,
                "缺少 name（必须显式写出，且与文件名 \"" + FileStem(_path)
                    + "\" 一致）"});
            _failed = true;
        } else if (_config.name != FileStem(_path)) {
            // -------------------------------------------------------------------
            // 先看是不是"重名"。两个文件重名时**必然**也会报"与文件名不一致"，
            // 但那条话对着 dupOne.json 说，用户不一定看得出真正的问题在"重名"；
            // 所以重名优先报，而且这时不再重复报文件名（同一处报两条更难读）。
            // -------------------------------------------------------------------
            std::string owner;
            for (const auto& claimed : _result.claimedNames) {
                if (claimed.first == _config.name) {
                    owner = claimed.second;
                    break;
                }
            }
            if (!owner.empty()) {
                _result.errors.push_back(CxxcssError{
                    _path, 0,
                    "name \"" + _config.name + "\" 已经被 " + owner
                        + " 用了（名字必须唯一，见 CXXCSS.md §1）"});
            } else {
                _result.errors.push_back(CxxcssError{
                    _path, 0,
                    "name \"" + _config.name + "\" 与文件名 \""
                        + FileStem(_path)
                        + "\" 不一致（生成器按名字生成类与查找键，"
                          "两处对不上就查不到，见 CXXCSS.md §1）"});
            }
            _failed = true;
        }

        if (!_sawWidth) {
            _result.errors.push_back(
                CxxcssError{_path, 0, "缺少 width（设计坐标，必须大于 0）"});
            _failed = true;
        }
        if (!_sawHeight) {
            _result.errors.push_back(
                CxxcssError{_path, 0, "缺少 height（设计坐标，必须大于 0）"});
            _failed = true;
        }
        if (!_hasNormal) {
            _result.errors.push_back(
                CxxcssError{_path, 0, "缺少 normal（通常状态的外观）"});
            _failed = true;
        }

        _config.identifier = MakeNameConstant(_config.name);

        // 两处都要写：ButtonConfig::name 是"这份文件叫什么"（生成器自己用），
        // ButtonData::name 是运行时 ButtonLibrary 的查找键。生成器里漏掉后面
        // 这个，运行期按名字就查不到——而且库那边只会报"没登记过"。
        _config.data.name = _config.name;

        // 认领这个名字。**不过滤 _failed**：这份文件可能因为别的问题被拒绝、
        // 因而不进 buttons，但它认领了某个名字这件事仍然成立——下一个撞上它的
        // 文件必须看得见。顺序也有意如此：重名由**后到者**报出来。
        _result.claimedNames.push_back({_config.name, _path});

        if (!_failed) {
            _result.buttons.push_back(std::move(_config));
        }
    }
};

}  // namespace

std::string CxxcssError::Format() const {
    // file 为空表示"message 里已经带了定位"（例如 JSON 语法错，ParseJson 给的
    // 错误串本身就带文件名和行列）。这种情况下不要再补一个空前缀加冒号。
    if (file.empty()) {
        return message;
    }

    std::string text = file;
    if (line > 0) {
        text += ":" + std::to_string(line);
    }
    text += ": " + message;
    return text;
}

std::string FileStem(const std::string& path) {
    std::size_t begin = path.find_last_of("/\\");
    begin = (begin == std::string::npos) ? 0 : begin + 1;

    std::string name = path.substr(begin);
    const std::string suffix = ".json";
    if (name.size() > suffix.size()
        && name.compare(name.size() - suffix.size(), suffix.size(), suffix)
               == 0) {
        name.erase(name.size() - suffix.size());
    }
    return name;
}

bool IsCxxIdentifier(const std::string& name) {
    if (name.empty()) {
        return false;
    }
    if (name[0] >= '0' && name[0] <= '9') {
        return false;
    }
    for (char ch : name) {
        const bool letter = (ch >= 'a' && ch <= 'z')
                         || (ch >= 'A' && ch <= 'Z') || ch == '_';
        const bool digit = ch >= '0' && ch <= '9';
        if (!letter && !digit) {
            return false;
        }
    }
    return true;
}

std::string MakeNameConstant(const std::string& name) {
    if (!IsCxxIdentifier(name)) {
        // 名字里有点或连字符（例如示例里的 button.example）：生成不出类，
        // 这个按钮就只能按字符串名字使用（数据照样会进 RegisterAllButtons）。
        return std::string();
    }

    // `normalButton` → `NormalButton`：首字母大写，变成一个**类名**。
    //
    // 为什么用类名而不是 `kNormalButtonName` 常量：生成的是完整的一个类，
    // 数据烘在类里。用类名当查找键，"拼错名字"就等于"类型不存在"，
    // 比"拼错一个字符串常量"更早、更硬地拦住。
    std::string type = name;
    type[0] = static_cast<char>(
        std::toupper(static_cast<unsigned char>(type[0])));
    return type;
}

LoadResult LoadButtonConfig(const std::string& path) {
    LoadResult result;
    LoadButtonConfigInto(path, result);
    return result;
}

void LoadButtonConfigInto(const std::string& path, LoadResult& result) {
    const ParseResult parsed = LoadJsonFile(path);
    if (!parsed.ok) {
        // parsed.error 已经带了文件名（LoadJsonFile 补的），所以这里不再补，
        // 用空 file 让 Format() 只输出"文件: 行: 说明"里的说明部分。
        result.errors.push_back(CxxcssError{std::string(), 0, parsed.error});
        return;
    }

    ButtonLoader loader(path, result);
    loader.Run(parsed.value);
}

LoadResult LoadButtonConfigs(const std::vector<std::string>& paths) {
    LoadResult result;

    std::vector<std::string> sorted = paths;
    // 排序：生成出来的代码顺序稳定，构建可复现（文件系统的顺序不该影响产物）。
    std::sort(sorted.begin(), sorted.end());

    for (const std::string& path : sorted) {
        const std::string stem = FileStem(path);
        const std::string suffix = ".example";
        const bool isExample = stem.size() > suffix.size()
                            && stem.compare(stem.size() - suffix.size(),
                                            suffix.size(), suffix) == 0;
        if (isExample) {
            continue;  // CXXCSS.md §1 第 3 条：示例不参与生成
        }

        // 逐个往**同一个** result 上追加：重名检查要看到前面所有文件的认领。
        LoadButtonConfigInto(path, result);
    }

    return result;
}

}  // namespace inkgen
