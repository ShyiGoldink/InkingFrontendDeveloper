#pragma once

// Button 的**纯数据**定义，对应 CXXCSS/Button/<名字>.json 的全部字段。
//
// 为什么数据与组件分开（CodeStyleRule §3.1）：
//   - 这份结构没有需要维护的不变量，就是个"配置长什么样"的记录 → struct；
//   - 代码生成器将来生成的就是**填这个结构的代码**，它跟"按钮怎么响应鼠标"
//     一点关系都没有；
//   - InkingStaticButton 只持有它、不解释它，于是"字段名对不上配置"
//     这类错最后全都收在 .cpp 里那几行映射上（见 InkingStaticButton.cpp）。
//
// 字段名与 CXXCSS.md / button.example.json 一一对应，不在这里改名：
// 名字对不上时的排查成本，比"换个更漂亮的命名"值钱得多。

#include <ink/basic/InkingAnchor.h>
#include <ink/dataStruct/InkingColor.h>
#include <ink/dataStruct/InkingShapeSpec.h>

#include <cstdint>
#include <string>

namespace ink {

/// 模块名常量：日志和自检要用（CodeStyleRule §5 要求每个模块都暴露一个）。
inline constexpr const char* kButtonModuleName = "InkingStaticButton";

/// 三态外观的类型，对应配置里的 `"type": "color" | "img" | "svg"`。
enum class AppearanceType : std::uint8_t {
    Color = 0,  ///< source 是 `"r|g|b|a"`（0~1 浮点）
    Image,      ///< source 是图像绝对路径，编译期转成二进制纹理
    Svg,        ///< source 是 SVG 代码
};

/**
 * @brief 一种状态下的外观。
 *
 * `color` 是 `source` 解析后的结果，渲染直接用这个字段，不在每帧去解析字符串。
 * Image / Svg 两种类型**还没落地**（纹理层没有），但类型与字段先留着：
 * 配置里能写的东西，运行期的数据结构就得能表达，否则生成器一落地就要改接口。
 */
struct ButtonAppearance {
    AppearanceType type = AppearanceType::Color;
    std::uint32_t color = 0xFF000000u;  ///< type == Color 时有效，0xAARRGGBB
    std::string source;                 ///< 原样的配置串，留给纹理层用

    ButtonAppearance() = default;
    ButtonAppearance(AppearanceType appearanceType, std::uint32_t argb)
        : type(appearanceType), color(argb) {}

    /// CXXCSS 里 `"0|0|0|0.75"` 那种 0~1 写法。
    static ButtonAppearance FromUnitRgba(float r, float g, float b,
                                         float a = 1.0f) {
        // 必须写 ink::——本类里有个同名静态函数，不加限定会找到自己。
        return ButtonAppearance(AppearanceType::Color,
                                ink::FromUnitRgba(r, g, b, a));
    }

    bool IsColor() const noexcept { return type == AppearanceType::Color; }
};

/// 文字段（配置里的 `text`）。`path` 为空表示用机器默认字体。
struct ButtonTextData {
    float fontSize = 0.0f;   ///< 设计坐标的字号
    float leftSpace = 0.0f;  ///< 距左侧，设计坐标
    float topSpace = 0.0f;   ///< 距顶部，设计坐标
    std::string path;        ///< 字体绝对路径；空 = 机器默认字体
};

/**
 * @brief 一个 Button 的全部静态配置。
 *
 * 三态外观（normal / hover / onclicked）按配置写法平铺成三个字段，
 * 而不是塞一个 `ButtonAppearance[3]`：配置里就是三个具名段，
 * 展开之后字段名能直接对上，找起来不用在脑子里做下标映射。
 */
struct ButtonData {
    std::string name;  ///< 与 json 文件名、代码里用的名字三处一致

    /// 设计坐标尺寸。配置里历史上有 `"200"` 这种字符串写法，
    /// 那是文件格式的事，生成器两种都接受；运行期一律是 int。
    int width = 0;
    int height = 0;

    ButtonAppearance normal{};     ///< 通常状态
    ButtonAppearance hover{};      ///< 悬停
    ButtonAppearance onclicked{};  ///< 按下

    /// 省略 hover / onclicked 时**继承 normal**。
    /// 这个收尾工作由 Normalize() 做，不在读取方那边各写一遍。
    bool hoverInheritsNormal = true;
    bool onclickedInheritsNormal = true;

    ButtonTextData text{};
    std::string label;  ///< text.content.normal 的运行时形态（当前按它画）

    /// 按钮文字的颜色。**配置里没有这个字段**（json 的 text 段没定义颜色），
    /// 这里按"浅色文字"给了个默认值，是为了让文字真的看得见。
    /// 等 text 段定稿，这个字段要么被配置覆盖，要么被删掉。
    std::uint32_t textColor = 0xFFFFFFFFu;

    Anchor selfAnchor{};   ///< 自身锚点
    Anchor traceAnchor{};  ///< 上级锚点
    float offsetX = 0.0f;  ///< 对齐之后再挪一点，设计坐标
    float offsetY = 0.0f;

    int zIndex = 0;          ///< 越大越靠上
    bool visible = true;     ///< 自己这一层的可见性

    ShapeSpec shape = ShapeSpec::Rect();  ///< 形状层，省略即直角矩形

    /**
     * 收尾：把"省略即继承"的默认值补齐。
     *
     * 必须在一个地方做，不能在渲染、命中、自检各判一次——那样三处的
     * 判断迟早会分叉。生成器生成的代码填完字段后调一次这个方法就行。
     * 幂等：重复调用结果不变。
     */
    void Normalize() {
        if (hoverInheritsNormal) {
            hover = normal;
        }
        if (onclickedInheritsNormal) {
            onclicked = normal;
        }
        // 三态都是纯透明的话，这个按钮在任何情况下都看不见——那多半是配置写漏了
        // （例如 source 写成了 "0|0|0"）。给个不透明的黑兜底，至少看得见。
        if (normal.IsColor() && ColorAlpha(normal.color) == 0u
            && ColorAlpha(hover.color) == 0u
            && ColorAlpha(onclicked.color) == 0u) {
            normal.color = 0xFF000000u;
            hover.color = 0xFF000000u;
            onclicked.color = 0xFF000000u;
        }
    }

    /// 有文字内容要画吗（当前只认 label）。
    bool HasLabel() const noexcept { return !label.empty(); }
};

}  // namespace ink
