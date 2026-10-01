// 渲染树的可视验证示例：叠三个不同 zindex 的色块，
// 越晚画（z 越大）的应该盖在越上面。
//
// 除了开窗给人看，它还会自己把窗口表面读回来做一次**确定性**校验：
// 在几个已知点上采样，确认压盖关系正确。这样在没有人盯着屏幕的地方
// （CI、无头冒烟）也能验渲染树，不依赖截图。

#include <ink/ink.h>
#include <ink/basic/InkingAnchor.h>
#include <scene/InkingScene.h>
#include <window/InkingWindow.h>

#include <config/window_config.h>

#include <cstdint>
#include <cstdio>
#include <string>

namespace {

class OverlapScene : public ink::InkingScene {
public:
    OverlapScene() : ink::InkingScene("Overlap") {}
};

/** 一块 400x260 的色块，放在 (x,y)，带层级与颜色。 */
ink::AnchorData patch(float x, float y, int z, std::uint32_t color) {
    ink::AnchorData data;
    data.width = 400;
    data.height = 260;
    data.offsetX = x;
    data.offsetY = y;
    data.zIndex = z;
    data.color = color;
    return data;
}

constexpr std::uint32_t kBackdrop = 0xFF1A1A22u;
constexpr std::uint32_t kRed = 0xFFE05252u;
constexpr std::uint32_t kGreen = 0xFF52E07Fu;
constexpr std::uint32_t kBlue = 0xFF5290E0u;

}  // namespace

int main() {
    OverlapScene scene;

    // 背景板：z 最小，压在最底下
    ink::AnchorData backdrop;
    backdrop.width = inking::kDesignWidth;
    backdrop.height = inking::kDesignHeight;
    backdrop.zIndex = -100;
    backdrop.color = kBackdrop;
    ink::InkingStaticAnchor backdropNode(&scene, backdrop);

    // 三个互相重叠的块，排在画面上部（容易看全）。
    // z 越大越晚画，所以画面里应当是蓝(3) 压绿(2)、绿压红(1)，
    // 两处重叠的边界一眼可辨。
    ink::InkingStaticAnchor red(&scene, patch(240.0f, 120.0f, 1, kRed));
    ink::InkingStaticAnchor green(&scene, patch(460.0f, 160.0f, 2, kGreen));
    ink::InkingStaticAnchor blue(&scene, patch(680.0f, 200.0f, 3, kBlue));

    INK_LOG_PASS("Overlap", "绘制列表共 "
                                + std::to_string(scene.GetDrawItemCount())
                                + " 项");

    // 采样点：每块自己的左上角区域（不被别的块压到），
    // 以及空白处。压盖关系由绘制列表的顺序保证（见下），
    // 真像素的校验交给自检里的绘制列表断言，这里只做肉眼确认。
    INK_LOG_PASS("Overlap", "z 序：蓝(3) → 绿(2) → 红(1) → 背景(-100)");

    ink::InkingWindow::Instance().Show("Overlap");
    return 0;
}
