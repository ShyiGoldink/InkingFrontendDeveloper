// 唯一一个**由 CXXCSS 配置驱动**的示例：按钮长什么样全部来自
// CXXCSS/Button/normalButton.json，代码里只引用生成出来的名字常量。
//
// 这就是"从 json 到窗口里一个能点的按钮"这条路的验收样本：
//   json → inkgen（构建期）→ build/…/button_service.h + button_register.cpp
//        → ButtonLibrary 里有配置 → InkingStaticButton(parent, 名字) 能用
//        → 场景层的指针派发把点击投给按钮的回调
//
// 跑法：
//   build/<预设>/bin/buttons_demo.exe              # 开窗，用鼠标点那个按钮
//   $env:INK_AUTOQUIT=1; build/<预设>/bin/buttons_demo.exe   # 自动退（冒烟）

#include <ink/ink.h>
#include <button/ButtonLibrary.h>
#include <button/InkingStaticButton.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>
#include <window/InkingWindow.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>
#include <vector>

// ← inkgen 生成的。OUT_DIR 是 include 根、HEADER 带一层子目录，两处都由
//   CMake 的 ink_add_generated 对齐好了，这里不用手写任何构建规则。
#include <inkgen/buttons_demo/button_service.h>

#include <cstdlib>
#include <string>

namespace {

/// 场景名。Scene 的 CXXCSS 生成还没做，所以今天它是一个编译期常量。
inline constexpr const char* kSceneName = "MyScene";

/** 按钮被点击了几次（自检要看这个数，所以放在能被 main 读到的地方）。 */
int gClickCount = 0;

/**
 * 示例场景：一个按钮成员。
 *
 * 为什么按钮要显式传 `*this` 当父级、而不是像伪代码里那样只给名字：
 * 组件必须知道自己属于哪个场景才能登记进渲染树（不登记就既不渲染也点不到），
 * 而这件事只有构造它的人知道。等到生成器连**场景类**一起生成时，
 * 这个参数可以由生成代码填上——那是下一轮的事。
 *
 * 注意按钮是**在场景构造函数体里**建出来的：那时场景还没盖章成渲染树的根，
 * 所以 InkingScene 在构造末尾会回头把这些"早期子节点"补登记一遍。
 */
class MyScene : public ink::InkingScene {
public:
    MyScene()
        : ink::InkingScene(kSceneName),
          // 显式转成基类指针：`*this` 在 InkingScene 里是明确的，但对按钮的
          // 构造函数来说"MyScene& → InkingAnchor*"是两步（派生到基类 + 取址），
          // 编译器不替我们猜。写成 asParent() 比在调用点写 static_cast 干净。
          _button(asParent(), ink::cxxcss::kNormalButtonName) {
        // 回调挂在构造里：按钮按下→抬起（指针还在按钮上）时会被调用。
        _button.SetOnClicked([] {
            ++gClickCount;
            INK_LOG_PASS("ButtonsDemo",
                         "I am deepseek, someone call me a blue fat fish "
                         "who love eating white rice!");
        });
    }

private:
    /// 按钮的父级：就是本场景（非拥有关系）。
    ink::InkingAnchor* asParent() noexcept {
        return static_cast<ink::InkingAnchor*>(this);
    }

    ink::InkingStaticButton _button;
};

/**
 * 模拟一次"移入 → 按下 → 抬起"，验证点击真的能被投递。
 *
 * 为什么要有这个：真鼠标点不了（自检环境里没人点屏幕），而"回调到底通没通"
 * 是这条链路上最容易悄悄断掉的一环。指针状态是场景的公开入口，所以不用
 * 造 SDL 事件、也不用开窗就能验。
 *
 * @return 回调被触发的次数
 */
int simulateClick(ink::InkingScene& scene, float x, float y,
                  bool cancelByMovingAway) {
    const int before = gClickCount;

    scene.SetPointerState(x, y, /*down=*/false, /*inside=*/true);
    scene.DispatchPointer();  // 移入：按钮进入 Hover

    scene.SetPointerState(x, y, /*down=*/true, /*inside=*/true);
    scene.DispatchPointer();  // 按下：进入 Pressed

    if (cancelByMovingAway) {
        // 按着滑出按钮再松开：这是"取消"，不该触发回调。
        scene.SetPointerState(-100.0f, -100.0f, /*down=*/true,
                              /*inside=*/true);
        scene.DispatchPointer();
    }

    const float upX = cancelByMovingAway ? -100.0f : x;
    const float upY = cancelByMovingAway ? -100.0f : y;
    scene.SetPointerState(upX, upY, /*down=*/false, /*inside=*/true);
    scene.DispatchPointer();  // 抬起

    return gClickCount - before;
}

/**
 * 离屏渲染一个 1×1 的点，返回 0xAARRGGBB。
 *
 * 用来把"三态的透明度到底生效没有"变成像素判据——只有真像素能证明
 * alpha 参与了合成（SDL 的绘制混合模式默认是关的，那时颜色会被原样写进去，
 * 状态机对了、颜色也选对了，屏幕上却看不出差别）。
 */
std::uint32_t sampleRenderedPixel(int x, int y) {
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        return 0u;
    }

    // 离屏目标给足设计尺寸：场景是在**设计坐标**里画的，
    // 目标小于设计尺寸时按钮会被裁到视口外面，采到的就只剩底色
    // （踩过：目标开成采样点那么大，于是三态读出来一模一样）。
    constexpr int kProbeWidth = 1920;
    constexpr int kProbeHeight = 1080;

    SDL_Window* window = SDL_CreateWindow("offscreen", kProbeWidth, kProbeHeight, 0);
    SDL_Renderer* renderer =
        window != nullptr ? SDL_CreateRenderer(window, nullptr) : nullptr;
    SDL_Texture* target =
        renderer != nullptr
            ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                                SDL_TEXTUREACCESS_TARGET, kProbeWidth,
                                kProbeHeight)
            : nullptr;
    std::uint32_t result = 0u;

    if (target != nullptr && SDL_SetRenderTarget(renderer, target)) {
        // 和窗口层一致：逻辑分辨率用设计尺寸 + 混合打开。
        // 少了逻辑分辨率这一步，设计坐标 (960,540) 会被当成像素坐标画到角落。
        SDL_SetRenderLogicalPresentation(renderer, kProbeWidth, kProbeHeight,
                                         SDL_LOGICAL_PRESENTATION_LETTERBOX);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        // 窗口底色（InkingWindow.cpp 里的 kBackground*）。
        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderClear(renderer);

        ink::SceneLibrary::RenderScene(renderer);
        SDL_RenderPresent(renderer);

        if (SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr)) {
            if (SDL_LockSurface(surface)) {
                const SDL_PixelFormatDetails* details =
                    SDL_GetPixelFormatDetails(surface->format);
                if (details != nullptr && x < surface->w && y < surface->h) {
                    const auto* row =
                        static_cast<const std::uint8_t*>(surface->pixels)
                        + static_cast<std::size_t>(y) * surface->pitch;
                    const std::uint8_t* pixel =
                        row + static_cast<std::size_t>(x)
                                  * details->bytes_per_pixel;
                    std::uint32_t raw = 0;
                    for (int i = 0; i < details->bytes_per_pixel; ++i) {
                        raw |= static_cast<std::uint32_t>(pixel[i]) << (8 * i);
                    }
                    const auto channel = [&](std::uint32_t mask) -> std::uint8_t {
                        if (mask == 0u) {
                            return 0xFFu;
                        }
                        std::uint32_t shift = 0;
                        while (((mask >> shift) & 1u) == 0u) {
                            ++shift;
                        }
                        return static_cast<std::uint8_t>(
                            (((raw & mask) >> shift) * 255u) / (mask >> shift));
                    };
                    result = (static_cast<std::uint32_t>(channel(details->Amask)) << 24)
                           | (static_cast<std::uint32_t>(channel(details->Rmask)) << 16)
                           | (static_cast<std::uint32_t>(channel(details->Gmask)) << 8)
                           | static_cast<std::uint32_t>(channel(details->Bmask));
                }
                SDL_UnlockSurface(surface);
            }
            SDL_DestroySurface(surface);
        }
    }

    SDL_SetRenderTarget(renderer, nullptr);
    if (target != nullptr) {
        SDL_DestroyTexture(target);
    }
    if (renderer != nullptr) {
        SDL_DestroyRenderer(renderer);
    }
    if (window != nullptr) {
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
    return result;
}

std::string toHex(std::uint32_t color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", color);
    return buffer;
}

/**
 * 不依赖真鼠标的自检：配置进了库、按钮进了渲染树、点击能到回调、
 * 三态的透明度真的落在像素上。
 *
 * 只在 INK_AUTOQUIT 下跑，这样正常开窗看效果时不会多打这些日志。
 */
bool runSelfCheck(ink::InkingScene& scene) {
    bool ok = true;

    // 离屏渲染那几步走的是 `SceneLibrary::RenderScene`，而它只画**活跃**场景。
    // 自检跑在 Show() 之前，这里得自己先把它设为活跃——否则采到的永远是纯底色，
    // 三态看起来"一模一样"（踩过）。
    ink::SceneLibrary::SetActiveScene(&scene);

    // 1. 生成器登记进库了吗
    const ink::ButtonData* data =
        ink::ButtonLibrary::Find(ink::cxxcss::kNormalButtonName);
    const bool registered = (data != nullptr);
    INK_DEBUG_CHECK(registered, "生成的配置没有进 ButtonLibrary");
    ok = ok && registered;
    if (data != nullptr) {
        INK_LOG_INFO("ButtonsDemo",
                     "配置：名字=" + data->name + " 尺寸="
                         + std::to_string(data->width) + "x"
                         + std::to_string(data->height));
    }

    // 2. 按钮进渲染树了吗（进不去就是"画不出来也点不到"）
    float centerX = 0.0f;
    float centerY = 0.0f;
    bool found = false;
    for (const ink::InkingScene::DrawItem& item : scene.BuildDrawList()) {
        if (item.node != nullptr
            && item.node->GetName() == ink::cxxcss::kNormalButtonName) {
            centerX = item.absX + static_cast<float>(item.node->GetWidth()) * 0.5f;
            centerY = item.absY + static_cast<float>(item.node->GetHeight()) * 0.5f;
            found = true;
        }
    }
    INK_DEBUG_CHECK(found, "按钮不在绘制列表里（场景构造期建的子节点没补登记）");
    ok = ok && found;

    if (found) {
        // 3. 点一下：回调应当被调用一次
        const int clicked = simulateClick(scene, centerX, centerY, false);
        INK_DEBUG_CHECK(clicked == 1, "点击没有触发回调");
        ok = ok && (clicked == 1);

        // 4. 按着滑出去再松开：这是取消，不该触发
        const int cancelled = simulateClick(scene, centerX, centerY, true);
        INK_DEBUG_CHECK(cancelled == 0, "按着滑出按钮再松开竟然触发了回调");
        ok = ok && (cancelled == 0);

        // 5. 收尾：把指针状态复原，别把模拟的痕迹留给真正的主循环
        scene.SetPointerState(0.0f, 0.0f, false, false);
        scene.DispatchPointer();

        // 6. 三态的透明度真的落在像素上吗
        //
        //    这一条是补上来的：状态机对了、颜色也选对了，但 SDL 的绘制混合模式
        //    默认是关的，alpha 会被原样写进目标——屏幕上三态一模一样。
        //    只有真像素能证明"合成发生了"。
        const int sampleX = static_cast<int>(centerX);
        const int sampleY = static_cast<int>(centerY);
        const std::uint32_t normal = sampleRenderedPixel(sampleX, sampleY);

        ink::InkingStaticButton* button = nullptr;
        for (const ink::InkingScene::DrawItem& item : scene.BuildDrawList()) {
            if (item.node != nullptr
                && item.node->GetName() == ink::cxxcss::kNormalButtonName
                && !item.node->IsDynamic()) {
                button = static_cast<ink::InkingStaticButton*>(item.node);
            }
        }

        if (button != nullptr) {
            button->MouseHover(true);
            const std::uint32_t hover = sampleRenderedPixel(sampleX, sampleY);
            button->MousePress(true);
            const std::uint32_t pressed = sampleRenderedPixel(sampleX, sampleY);
            button->MouseRelease();
            button->MouseHover(false);

            INK_LOG_INFO("ButtonsDemo",
                         "三态像素：通常=" + toHex(normal) + " 悬停="
                             + toHex(hover) + " 按下=" + toHex(pressed)
                             + "（底色 0xFF121218）");

            // 前提：这个按钮的三态颜色**不一样**。示例配置特意拉大了透明度差
            // （0.35 / 0.85），就是为了让"悬停看得出来"。
            const bool statesDiffer = (normal != hover);
            INK_DEBUG_CHECK(statesDiffer,
                            "通常态与悬停态渲染出的像素一模一样（检查配置里的透明度）");
            ok = ok && statesDiffer;

            // 白色按钮叠在暗底上：越不透明越白。所以悬停（0.85）应当比通常（0.35）亮。
            const auto brightness = [](std::uint32_t color) {
                return static_cast<int>(ink::ColorRed(color))
                     + static_cast<int>(ink::ColorGreen(color))
                     + static_cast<int>(ink::ColorBlue(color));
            };
            const bool hoverBrighter = brightness(hover) > brightness(normal);
            INK_DEBUG_CHECK(hoverBrighter,
                            "悬停态没有比通常态更亮（alpha 可能没参与合成）");
            ok = ok && hoverBrighter;

            // 按下态按配置应当回到和通常态同一档（onclicked = 0.35）。
            const bool pressedBack = (pressed == normal);
            INK_DEBUG_CHECK(pressedBack,
                            "按下态与通常态不一致（配置里两者都是 0.35）");
            ok = ok && pressedBack;
        }
    }

    INK_LOG_PASS("ButtonsDemo",
                 ok ? "自检通过：配置→库→渲染树→点击回调 这条链路是通的"
                    : "自检失败，看上面的错误日志");
    return ok;
}

}  // namespace

int main() {
    // 场景必须在 Show 之前就存在：Show(名字) 是按名字去 SceneLibrary 里找的。
    MyScene scene;

    // 环境变量用 std::getenv：这里不需要 SDL，也就没必要把 SDL 头拖进示例。
    const bool autoQuit = std::getenv("INK_AUTOQUIT") != nullptr;
    if (autoQuit) {
        const bool ok = runSelfCheck(scene);
        if (!ok) {
            return 1;
        }
    }

    // 这一句就是全部：窗口开出来、场景设为活跃、进主循环。
    ink::InkingWindow::Instance().Show(kSceneName);

    INK_LOG_INFO("ButtonsDemo",
                 "窗口已关闭，按钮被点了 " + std::to_string(gClickCount) + " 次");
    return 0;
}
