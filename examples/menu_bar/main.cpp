// 最小完整测试：一个窗口，顶部一条 3 个按钮的菜单栏。
//
// 按 docs 的方案把这条件走通：
//
//   Scene 静态层  Bake 出「命中索引 + 平铺绘制表」
//     → InputRouter  isDirty + 三态 query（点击单独查一次）
//     → Button       点击回调；悬停 / 按下的外观变化明确调 makeDirty()
//     → RedrawScheduler  重绘是独立的一路，静止时整帧不画
//
// 点击只写日志与调试输出，不做别的事。窗口退出时会打印这一趟的账：
// 静态表重烘了几次、hover query 了几次、重绘了几帧、跳过了几帧。

#include <ink/ink.h>
#include <scene/Button.h>
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>

#include <string>

namespace {

constexpr const char* kModuleName = "菜单栏示例";

constexpr float kMenuBarHeight = 72.0f;
constexpr float kButtonWidth   = 200.0f;
constexpr float kButtonHeight  = 56.0f;
constexpr float kButtonGap     = 24.0f;
constexpr float kButtonLeft    = 40.0f;

// 一套手写的配色。等 CXXCSS + 代码生成器接手，这些都会变成配置项。
constexpr ink::Color kBackdrop{18, 18, 24, 255};
constexpr ink::Color kBarFill{28, 30, 40, 255};
constexpr ink::Color kBarEdge{58, 62, 78, 255};
constexpr ink::Color kButtonFill{44, 47, 60, 255};
constexpr ink::Color kButtonHover{72, 78, 100, 255};
constexpr ink::Color kButtonPress{26, 28, 36, 255};
constexpr ink::Color kButtonEdge{80, 86, 108, 255};
constexpr ink::Color kPlaceholderText{150, 158, 180, 255};

/// 字形系统还没落地：用一根色条把「这里本该有字」画出来。
void drawLabelPlaceholder(ink::Canvas& canvas, const ink::Rect& box, ink::Color color) {
    const ink::Rect bar = box.inset(box.w * 0.22f, box.h * 0.38f);
    canvas.fillRect(bar, color);
}

/// 根场景：整页。点在空白处由它兜底（表里的兜底条目就是它）。
class PageScene : public ink::InkingScene {
public:
    PageScene() : InkingScene("菜单栏示例页") {}

    void onDraw(ink::Canvas& canvas) const override {
        canvas.fillRect(GetRect(), kBackdrop);
    }
};

/// 菜单栏：一条等宽等距的行 → 命中用纯算术，连表都不建（docs §10.4）。
class MenuBarScene : public ink::InkingScene {
public:
    MenuBarScene() : InkingScene("菜单栏") {
        UseSlotLayout();  // 声明：静态子节点等宽等距
    }

    void onDraw(ink::Canvas& canvas) const override {
        const ink::Rect bar = GetRect();
        canvas.fillRect(bar, kBarFill);
        canvas.fillRect(ink::Rect{bar.x, bar.bottom() - 2.0f, bar.w, 2.0f}, kBarEdge);
    }
};

/// 带占位标签的按钮：字形到位之前，标签先用色条表示。
class MenuButton : public ink::Button {
public:
    using Button::Button;

    void onDraw(ink::Canvas& canvas) const override {
        Button::onDraw(canvas);
        // 悬停 / 按下时标签跟着提亮：这三档只改颜色，几何一点不动，
        // 所以静态表一直有效，只有重绘被标脏。
        drawLabelPlaceholder(canvas, GetRect(),
                             IsHovered() ? ink::Color{226, 230, 240, 255} : kPlaceholderText);
    }
};

/// 点击：调试输出 + 日志，就这两件事。
void onMenuClicked(ink::Button& button) {
    INK_LOG_DEBUG(kModuleName, "点击：" + button.GetLabel() + "（" + button.GetName() + "）");
    INK_LOG_INFO(kModuleName, "点击：" + button.GetLabel());
}

}  // namespace

int main() {
    INK_LOG_INFO(kModuleName, "启动：最小完整测试（窗口 + 顶部 3 按钮菜单栏）");

    // 根场景。尺寸与锚点在构造阶段定下，之后不挪动——场景永远静态。
    // （写入口现在还能调，因为这一版是手写的；生成器接手后，配置驱动的场景
    //   会在编译期就把这些定死，运行期不再有位置 / 尺寸写入口。）
    PageScene page;
    page.Resize(inking::kDesignWidth, inking::kDesignHeight);

    // 菜单栏：贴顶、占满宽度，压在根场景上面。
    MenuBarScene& bar = page.Make<MenuBarScene>();
    bar.ChangeSelfAnchor(ink::InkingChangeAnchor::LeftTop);
    bar.ChangeTraceAnchor(ink::InkingChangeAnchor::LeftTop);
    bar.Resize(inking::kDesignWidth, static_cast<int>(kMenuBarHeight));
    bar.ChangeZIndex(1);

    // 3 个按钮：等宽等距排在菜单栏左侧，垂直居中（自身 Center 对上父级 Center）。
    const char* labels[] = {"文件", "编辑", "帮助"};
    for (int index = 0; index < 3; ++index) {
        MenuButton& button =
            bar.Make<MenuButton>("菜单按钮" + std::to_string(index + 1), labels[index]);
        button.ChangeSelfAnchor(ink::InkingChangeAnchor::LeftCenter);
        button.ChangeTraceAnchor(ink::InkingChangeAnchor::LeftCenter);
        button.ChangeOffset(kButtonLeft
                                + static_cast<float>(index) * (kButtonWidth + kButtonGap),
                            0.0f);
        button.Resize(static_cast<int>(kButtonWidth), static_cast<int>(kButtonHeight));
        button.ChangeZIndex(2);
        button.SetFill(kButtonFill);
        button.SetHoverFill(kButtonHover);
        button.SetPressFill(kButtonPress);
        button.SetBorder(kButtonEdge);
        button.action = onMenuClicked;
    }

    INK_LOG_INFO(kModuleName, "场景树就绪：1 个根场景 + 1 条菜单栏 + 3 个按钮，"
                              "窗口打开后点按钮只会写日志");

    ink::InkingWindow::Instance().Show(page.GetName());  // 阻塞到窗口关闭（或 ESC）

    INK_LOG_INFO(kModuleName, "退出");
    return 0;
}
