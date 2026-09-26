// 基于文档的按钮与场景：纯白场景 + 半透明黑按钮。
//
// 悬停时两件事同时发生，正好是两条不同的路（docs/InputDesign.md §3 / §4）：
//
//   · 黑色加重 —— 只影响重绘 → 写入口明确调 makeDirty()；
//   · 稍微变大 —— 真的改了尺寸 → 走 Resize() 这个写入口 → 内部标脏
//                  → 上层命中表重烘一次。
//
// 变大用「自身锚点 Center 对上父级 Center」，所以是从中心长大：中心点不动，
// 四周对称外扩，命中范围也跟着扩。Log.html 里能看到每次 makeDirty 的原因
// （Scene 模块）和每次重烘（Scene 模块的「烘焙 …」），退出时还会打印这一趟
// 的账：重烘几次、hover query 几次、重绘几帧、跳过几帧。
//
// 点击只写日志与调试输出，没有别的功能。

#include <ink/ink.h>
#include <scene/Button.h>
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>

#include <string>

namespace {

constexpr const char* kModuleName = "白场按钮示例";

constexpr float kButtonWidth  = 240.0f;
constexpr float kButtonHeight = 120.0f;
constexpr float kButtonGap    = 180.0f;

/// 悬停放大倍率：1.15 倍。0.15 × 240 = 36 像素，肉眼看得出来，
/// 又小到不会和邻居撞上（间距 120）。
constexpr float kHoverGrowth = 1.15f;

constexpr ink::Color kPageWhite{255, 255, 255, 255};
constexpr ink::Color kButtonFill{0, 0, 0, 120};       ///< 半透明黑
constexpr ink::Color kButtonHoverFill{0, 0, 0, 200};  ///< 悬停：黑加重
constexpr ink::Color kButtonPressFill{0, 0, 0, 235};  ///< 按下：再重一点
constexpr ink::Color kLabelWhite{255, 255, 255, 220};

/// 场景：纯白，占满整页。点在空白处由它兜底。
class WhiteScene : public ink::InkingScene {
public:
    WhiteScene() : InkingScene("纯白场景") {}

    void onDraw(ink::Canvas& canvas) const override {
        canvas.fillRect(GetRect(), kPageWhite);
    }
};

/// 会变大的按钮。
///
/// 重写的是 [√] 钩子 onSizeChanged（只挂附加逻辑，不标脏）：它替我们把
/// 「这一下真的改了几何」打到日志里，正好对上「变大会导致重新烘焙」这条。
class GrowingButton : public ink::Button {
public:
    using Button::Button;

protected:
    void onSizeChanged() override {
        INK_LOG_DEBUG(kModuleName,
                      GetName() + " 尺寸变成 " + std::to_string(GetWidth()) + "×"
                          + std::to_string(GetHeight()) + " → 命中表要重烘");
    }
};

/// 点击：调试输出 + 日志，就这两件事。
void onButtonClicked(ink::Button& button) {
    INK_LOG_DEBUG(kModuleName, "点击：" + button.GetLabel() + "（" + button.GetName() + "）");
    INK_LOG_INFO(kModuleName, "点击：" + button.GetLabel());
}

}  // namespace

int main() {
    INK_LOG_INFO(kModuleName, "启动：纯白场景 + 半透明黑按钮（悬停加重并变大）");

    // 根场景：纯白整页。尺寸与锚点在构造阶段定下，之后不挪动。
    WhiteScene page;
    page.Resize(inking::kDesignWidth, inking::kDesignHeight);

    // 三颗按钮：自身 Center 对上父级 Center——偏移 0 就是"正中央"，
    // 再往左右各推一格。这样放大是从中心长出来，中心点不动，
    // 命中范围四周对称地变大。
    const char* labels[] = {"一号按钮", "二号按钮", "三号按钮"};
    const float spacing = kButtonWidth + kButtonGap;
    const float shifts[] = {-spacing, 0.0f, spacing};

    for (int index = 0; index < 3; ++index) {
        GrowingButton& button =
            page.Make<GrowingButton>("白场按钮" + std::to_string(index + 1), labels[index]);
        button.ChangeSelfAnchor(ink::InkingChangeAnchor::Center);
        button.ChangeTraceAnchor(ink::InkingChangeAnchor::Center);
        button.ChangeOffset(shifts[index], 0.0f);
        button.Resize(static_cast<int>(kButtonWidth), static_cast<int>(kButtonHeight));
        button.ChangeZIndex(1 + index);

        button.SetFill(kButtonFill);
        button.SetHoverFill(kButtonHoverFill);
        button.SetPressFill(kButtonPressFill);
        button.SetBorder(ink::kTransparent);  // 半透明黑不需要描边
        button.SetLabelColor(kLabelWhite);
        button.SetHoverGrowth(kHoverGrowth);
        button.action = onButtonClicked;
    }

    INK_LOG_INFO(kModuleName,
                 "场景树就绪：1 个纯白场景 + 3 颗半透明黑按钮（悬停 "
                     + std::to_string(kHoverGrowth) + " 倍放大）");

    ink::InkingWindow::Instance().Show(page.GetName());  // 阻塞到窗口关闭（或 ESC）

    INK_LOG_INFO(kModuleName, "退出");
    return 0;
}
