// 唯一一个**由 CXXCSS 配置驱动**的示例：按钮长什么样全部来自
// CXXCSS/Button/*.json，代码里只引用生成出来的类。
//
// 这里摆了两块按钮，正好是同一个组件的两档：
//
//   normalButton（静态）——  几何构造即定型，三态只换颜色，进命中表；
//                           指针由场景层派发**推**给它（DispatchPointer）。
//   dynamicTestButton（动态）——几何可以运行期改，不进命中表；指针由它自己在
//                           Tick 里**拉**（JudgePointer），所以它要 TickLogic。
//
// 这就是"从 json 到窗口里一个能点的按钮"这条路的验收样本：
//   json → inkgen（构建期）→ build/…/button_service.h + button_register.cpp
//        → ButtonLibrary 里有配置 → 生成类能被构造
//        → 两个档次各自的输入路径都能把点击投给回调
//
// 跑法：
//   build/<预设>/bin/buttons_demo.exe              # 开窗，用鼠标点那两块按钮
//   $env:INK_AUTOQUIT=1; build/<预设>/bin/buttons_demo.exe   # 自动退（冒烟）

#include <ink/ink.h>
#include <button/ButtonLibrary.h>
#include <button/InkingDynamicButton.h>
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

/**
 * 一个逻辑步的长度，和主循环里那条时间线一致（Timeline 默认 50Hz）。
 *
 * 自检要手动推逻辑步：动态按钮的输入发生在它自己的 Tick 里，
 * 光调 `DispatchPointer()` 是**推不动**它的（那正是"输入不同"的证明）。
 */
constexpr double kLogicStep = 1.0 / 50.0;

/** 静态按钮被点击了几次（自检要看这个数，所以放在能被 main 读到的地方）。 */
int gClickCount = 0;

/** 动态按钮被点击了几次。两个计数器分开：混在一起就分不清是谁响的。 */
int gDynamicClickCount = 0;

/**
 * 示例场景：一块静态按钮 + 一块动态按钮。
 *
 * 两块都是**生成出来的类**：颜色、尺寸、形状、锚点全部烘在类里，构造时
 * **不查任何表**——"配置取不到"这件事在类型上不可能发生。唯一区别是基类，
 * 而基类由 json 里的 `"dynamic"` 决定。
 *
 * 为什么还要传 `*this` 当父级：组件必须知道自己属于哪个场景才能登记进渲染树
 * （不登记就既不渲染也点不到），而这件事只有构造它的人知道。
 * 等到生成器连**场景类**一起生成时，这个参数可以由生成代码填上——那是 T-7 的事。
 *
 * 注意两块按钮都是**在场景构造函数体里**建出来的：场景的基类构造（含盖章成根）
 * 先跑完，所以成员构造时已经认得出所属场景了；InkingScene 另外还会在构造末尾
 * 回头补登记一遍（`rebindEarlyChildren`），那一条是给"在场景自己构造函数体里
 * 建的子节点"用的。
 */
class MyScene : public ink::InkingScene {
public:
    MyScene()
        : ink::InkingScene(kSceneName),
          // 显式转成基类指针：`*this` 在 InkingScene 里是明确的，但对按钮的
          // 构造函数来说"MyScene& → InkingAnchor*"是两步（派生到基类 + 取址），
          // 编译器不替我们猜。写成 asParent() 比在调用点写 static_cast 干净。
          _button(asParent()),
          _dynamicButton(asParent()) {
        // 回调挂在构造里：按钮按下→抬起（指针还在按钮上）时会被调用。
        _button.SetOnClicked([] {
            ++gClickCount;
            INK_LOG_PASS("ButtonsDemo",
                         "I am deepseek, someone call me a blue fat fish "
                         "who love eating white rice!");
        });

        // 动态按钮的回调挂在**同一个地方**：点击怎么触发是两档各自的实现，
        // 但"回调怎么挂"对使用者来说没有区别。
        _dynamicButton.SetOnClicked([] {
            ++gDynamicClickCount;
            INK_LOG_PASS("ButtonsDemo", "动态按钮被点了一下！");
        });

        INK_LOG_INFO("ButtonsDemo",
                     "场景建好了：静态 " + std::string(ink::cxxcss::NormalButton::kName)
                         + "（" + std::to_string(_button.GetWidth()) + "x"
                         + std::to_string(_button.GetHeight()) + "） + 动态 "
                         + std::string(ink::cxxcss::DynamicTestButton::kName) + "（"
                         + std::to_string(_dynamicButton.GetWidth()) + "x"
                         + std::to_string(_dynamicButton.GetHeight()) + "）");
    }

private:
    /// 按钮的父级：就是本场景（非拥有关系）。
    ink::InkingAnchor* asParent() noexcept {
        return static_cast<ink::InkingAnchor*>(this);
    }

    ink::cxxcss::NormalButton _button;
    ink::cxxcss::DynamicTestButton _dynamicButton;
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
 * 模拟动态按钮的一次"移入 → 按下 → 抬起"。
 *
 * **和上面那个的区别就是 T-1 的正文**：动态按钮不吃 `DispatchPointer()`
 * 那一套（那一条路明确跳过动态节点），它的输入发生在自己的 Tick 里，
 * 所以模拟必须推逻辑步（`TickLogic`），场景那边只负责更新指针状态。
 * 换句话说：静态按钮是"被喂"的，动态按钮是"自己拉"的。
 *
 * @return 回调被触发的次数
 */
int simulateDynamicClick(ink::InkingScene& scene, float x, float y,
                        bool cancelByMovingAway) {
    const int before = gDynamicClickCount;

    // 只更新指针状态，**不派发**：派发那条路对动态按钮是空转（下面专门验一次）。
    scene.SetPointerState(x, y, /*down=*/false, /*inside=*/true);
    scene.TickLogic(kLogicStep);  // 移入：按钮进入 Hover

    scene.SetPointerState(x, y, /*down=*/true, /*inside=*/true);
    scene.TickLogic(kLogicStep);  // 按下：进入 Pressed

    if (cancelByMovingAway) {
        // 按着滑出按钮再松开：这是"取消"，不该触发回调。
        scene.SetPointerState(-100.0f, -100.0f, /*down=*/true,
                              /*inside=*/true);
        scene.TickLogic(kLogicStep);
    }

    const float upX = cancelByMovingAway ? -100.0f : x;
    const float upY = cancelByMovingAway ? -100.0f : y;
    scene.SetPointerState(upX, upY, /*down=*/false, /*inside=*/true);
    scene.TickLogic(kLogicStep);  // 抬起

    return gDynamicClickCount - before;
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
 * 像素亮度（三通道之和）。
 *
 * 断言只验**关系**（"悬停比通常亮"），不复述配置里的数值：把 0.35 / 0.85
 * 抄进自检，等于给配置又留一份副本——改 json 就红，而那种红没有信息量
 * （配置本来就该能随便调）。
 */
int brightness(std::uint32_t color) {
    return static_cast<int>(ink::ColorRed(color))
         + static_cast<int>(ink::ColorGreen(color))
         + static_cast<int>(ink::ColorBlue(color));
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

    // 1. 生成器把同一份配置也登记进库了吗（给"按名字查"的用法）。
    //
    //    注意：**按钮能用不依赖这一步**——生成出来的类自己带着数据。
    //    这里查得到，只是说明启动期那份登记也跑过了。
    const ink::ButtonData* data =
        ink::ButtonLibrary::Find(ink::cxxcss::NormalButton::kName);
    const bool registered = (data != nullptr);
    INK_DEBUG_CHECK(registered, "生成的配置没有进 ButtonLibrary");
    ok = ok && registered;
    if (data != nullptr) {
        INK_LOG_INFO("ButtonsDemo",
                     "配置：名字=" + data->name + " 尺寸="
                         + std::to_string(data->width) + "x"
                         + std::to_string(data->height));
    }

    // 1b. 更关键的一条：生成类的 Data() 与库里那份**是同一份内容**，
    //     而且它不依赖库是否登记过（类自己带着）。
    {
        const ink::ButtonData& baked = ink::cxxcss::NormalButton::Data();
        const bool same = registered && baked.name == data->name
                       && baked.width == data->width
                       && baked.height == data->height
                       && baked.normal.color == data->normal.color;
        INK_DEBUG_CHECK(same, "生成类烘的配置与库里登记的那份不一致");
        ok = ok && same;
    }

    // 2. 按钮进渲染树了吗（进不去就是"画不出来也点不到"）
    float centerX = 0.0f;
    float centerY = 0.0f;
    bool found = false;
    for (const ink::InkingScene::DrawItem& item : scene.BuildDrawList()) {
        if (item.node != nullptr
            && item.node->GetName() == ink::cxxcss::NormalButton::kName) {
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
                && item.node->GetName() == ink::cxxcss::NormalButton::kName
                && !item.node->IsDynamic()) {
                button = static_cast<ink::InkingStaticButton*>(item.node);
            }
        }

        if (button != nullptr) {
            // ---- 过渡（T-2）：配置里 hover 带了 transition，所以颜色**不再瞬时到位** ----
            //
            // 这里只留一行实测记录、不做断言（T-2 明说了不必为动画写测试）：
            // 起手是通常色，推半个时长应当是中间色，推完到悬停色。
            button->MouseHover(true);
            const std::uint32_t transitionStart = button->GetDisplayColor();
            scene.TickFrame(0.075);  // hover 的 duration 是 0.15，推到一半
            const std::uint32_t transitionHalf = button->GetDisplayColor();
            scene.TickFrame(0.2);    // 推过头，过渡应当收尾
            const std::uint32_t transitionEnd = button->GetDisplayColor();
            const std::uint32_t hoverTarget
                = button->GetAppearance(ink::ButtonState::Hover).color;
            INK_LOG_INFO("ButtonsDemo",
                         "悬停过渡（源色）：起=" + toHex(transitionStart)
                             + " 中途=" + toHex(transitionHalf) + " 到位="
                             + toHex(transitionEnd) + " 目标="
                             + toHex(hoverTarget));

            // 上面已经把过渡推完了，所以这里采到的就是悬停态的终态颜色。
            const std::uint32_t hover = sampleRenderedPixel(sampleX, sampleY);
            // 按下没配 transition（onclicked 是瞬变），不需要推进。
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

    // 7. 动态按钮：**输入与静态按钮不同**
    //
    //    静态按钮的三态是场景派发推过去的；动态按钮不在派发那条路上
    //    （动态节点不进表、也不被遍历），它每帧在自己的 Tick 里从场景拉。
    //    所以这里先证明"派发推不动它、推逻辑步才动得动"，再证明它自己的
    //    三态、点击、取消和静态版是同一套手感。
    {
        ink::InkingDynamicButton* dynamicButton = nullptr;
        float dynamicCenterX = 0.0f;
        float dynamicCenterY = 0.0f;
        for (const ink::InkingScene::DrawItem& item : scene.BuildDrawList()) {
            if (item.node != nullptr
                && item.node->GetName()
                       == ink::cxxcss::DynamicTestButton::kName) {
                dynamicButton
                    = static_cast<ink::InkingDynamicButton*>(item.node);
                // 动态节点每帧现算坐标，所以问它自己，而不是列表里的快照
                // （快照是建表那一刻的，动态组件随时会动）。
                dynamicCenterX =
                    dynamicButton->GetAbsX()
                    + static_cast<float>(dynamicButton->GetWidth()) * 0.5f;
                dynamicCenterY =
                    dynamicButton->GetAbsY()
                    + static_cast<float>(dynamicButton->GetHeight()) * 0.5f;
            }
        }

        const bool dynamicInTree =
            dynamicButton != nullptr && dynamicButton->IsDynamic();
        INK_DEBUG_CHECK(dynamicInTree,
                        "动态按钮不在绘制列表里，或者被当成了静态档");
        ok = ok && dynamicInTree;

        if (dynamicInTree) {
            // 7a. 派发推不动它：指针挪到它身上 + `DispatchPointer()`，
            //     三态必须**纹丝不动**。这一条就是"输入不同"的硬证据。
            scene.SetPointerState(dynamicCenterX, dynamicCenterY,
                                  /*down=*/false, /*inside=*/true);
            scene.DispatchPointer();
            const bool unaffectedByDispatch =
                dynamicButton->GetState() == ink::ButtonState::Normal;
            INK_DEBUG_CHECK(unaffectedByDispatch,
                            "动态按钮被场景派发推动了——它的输入该由自己拉");
            ok = ok && unaffectedByDispatch;

            // 7b. 推一个逻辑步，它在自己的 Tick 里拉到了指针 → Hover。
            scene.TickLogic(kLogicStep);
            const bool hoverByTick =
                dynamicButton->GetState() == ink::ButtonState::Hover
                && dynamicButton->IsPointerOver();
            INK_DEBUG_CHECK(hoverByTick,
                            "推了逻辑步，动态按钮没有自己判出 Hover");
            ok = ok && hoverByTick;

            // 7c. 点一下：回调恰好一次
            const int dynamicClicked = simulateDynamicClick(
                scene, dynamicCenterX, dynamicCenterY, false);
            INK_DEBUG_CHECK(dynamicClicked == 1, "动态按钮的点击没有触发回调");
            ok = ok && (dynamicClicked == 1);

            // 7d. 按着滑出去再松开：取消，不该触发
            const int dynamicCancelled = simulateDynamicClick(
                scene, dynamicCenterX, dynamicCenterY, true);
            INK_DEBUG_CHECK(dynamicCancelled == 0,
                            "动态按钮按着滑出去再松开竟然触发了回调");
            ok = ok && (dynamicCancelled == 0);

            // 7e. 抓取语义要和静态版一致：按下去之后滑出去仍然算"按着"
            //     （不是"指针一离开就弹回 Hover"）。
            scene.SetPointerState(dynamicCenterX, dynamicCenterY,
                                  /*down=*/true, /*inside=*/true);
            scene.TickLogic(kLogicStep);
            const bool pressedInside =
                dynamicButton->GetState() == ink::ButtonState::Pressed;
            scene.SetPointerState(-100.0f, -100.0f, /*down=*/true,
                                  /*inside=*/true);
            scene.TickLogic(kLogicStep);
            const bool stillPressedOutside =
                dynamicButton->GetState() == ink::ButtonState::Pressed
                && dynamicButton->IsPressCaptured();
            INK_DEBUG_CHECK(pressedInside && stillPressedOutside,
                            "按着不放滑出按钮之后动态按钮不该自己松开");
            ok = ok && pressedInside && stillPressedOutside;

            // 7f. 在外面松开：回到 Normal，而且不算一次点击
            const int beforeRelease = gDynamicClickCount;
            scene.SetPointerState(-100.0f, -100.0f, /*down=*/false,
                                  /*inside=*/true);
            scene.TickLogic(kLogicStep);
            const bool backToNormal =
                dynamicButton->GetState() == ink::ButtonState::Normal
                && gDynamicClickCount == beforeRelease;
            INK_DEBUG_CHECK(backToNormal,
                            "在外面松开之后动态按钮没有回到 Normal，或者误触发了一次点击");
            ok = ok && backToNormal;

            // 7g. 三态的差别真的落在像素上吗（和静态按钮同一条依据：
            //     SDL 的绘制混合默认是关的，只有真像素能证明 alpha 参与了合成）。
            //
            //     这个按钮的 hover 也配了 transition（0.2s），所以每次切完状态
            //     还要推一帧渲染帧把颜色与变换一起走完——**采样点取的是中心**，
            //     而旋转/缩放都是绕中心做的，中心是不动点，所以位置不受影响。
            const int sampleX = static_cast<int>(dynamicCenterX);
            const int sampleY = static_cast<int>(dynamicCenterY);

            scene.SetPointerState(-100.0f, -100.0f, /*down=*/false,
                                  /*inside=*/true);
            scene.TickLogic(kLogicStep);
            scene.TickFrame(0.3);
            const std::uint32_t dynamicNormal =
                sampleRenderedPixel(sampleX, sampleY);

            scene.SetPointerState(dynamicCenterX, dynamicCenterY,
                                  /*down=*/false, /*inside=*/true);
            scene.TickLogic(kLogicStep);
            scene.TickFrame(0.3);
            const std::uint32_t dynamicHover =
                sampleRenderedPixel(sampleX, sampleY);

            scene.SetPointerState(dynamicCenterX, dynamicCenterY,
                                  /*down=*/true, /*inside=*/true);
            scene.TickLogic(kLogicStep);
            scene.TickFrame(0.3);
            const std::uint32_t dynamicPressed =
                sampleRenderedPixel(sampleX, sampleY);

            INK_LOG_INFO("ButtonsDemo",
                         "动态按钮三态像素：通常=" + toHex(dynamicNormal)
                             + " 悬停=" + toHex(dynamicHover) + " 按下="
                             + toHex(dynamicPressed) + "（底色 0xFF121218）");

            const bool dynamicStatesDiffer = dynamicNormal != dynamicHover;
            INK_DEBUG_CHECK(dynamicStatesDiffer,
                            "动态按钮的通常态与悬停态像素一模一样");
            ok = ok && dynamicStatesDiffer;

            const bool dynamicHoverBrighter =
                brightness(dynamicHover) > brightness(dynamicNormal);
            INK_DEBUG_CHECK(dynamicHoverBrighter,
                            "动态按钮的悬停态没有比通常态更亮（alpha 可能没参与合成）");
            ok = ok && dynamicHoverBrighter;

            const bool dynamicPressedBack = dynamicPressed == dynamicNormal;
            INK_DEBUG_CHECK(dynamicPressedBack,
                            "动态按钮的按下态与通常态不一致（配置里两者都是 0.35）");
            ok = ok && dynamicPressedBack;

            // 7h. 收尾：指针状态与动态按钮的三态都复原，
            //     别把自检的痕迹留给真正的主循环。
            scene.SetPointerState(0.0f, 0.0f, false, false);
            scene.DispatchPointer();
            scene.TickLogic(kLogicStep);
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
