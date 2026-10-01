#include <window/InkingWindow.h>

#include <ink/basic/InkLog.h>
#include <ink/basic/Timeline.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <string>
#include <thread>

namespace ink {

namespace {

constexpr const char* kModuleName = "InkingWindow";

/**
 * 帧节流的兜底：vsync 接不上时用的固定节拍（16ms ≈ 60Hz）。
 *
 * 注意它**不是**精确的 60fps：`sleep_for` 不扣本帧耗时，实际一帧是
 * 16ms + 工作时间。正常情况下走 vsync（`SDL_SetRenderVSync`），
 * 这条只在无头 / 无显示环境接不上 vsync 时兜底。
 */
constexpr std::chrono::microseconds kFrameInterval{16000};

/// 无头冒烟：设了 INK_AUTOQUIT 就跑这么久然后自己退出。
///
/// 计时用 steady_clock：主循环的节流和冒烟计时走同一套单调时钟，
/// 不掺 SDL 的计时器。
constexpr std::chrono::milliseconds kAutoQuitAfter{2000};

/// 占位绘制的底色，和 basic 示例保持一致；样式层接手后换掉。
constexpr Uint8 kBackgroundRed   = 18;
constexpr Uint8 kBackgroundGreen = 18;
constexpr Uint8 kBackgroundBlue  = 24;

/// 根锚点的尺寸 = 设计画布，不是窗口像素：子组件的锚点是相对设计空间算的。
AnchorData rootAnchorData() {
    AnchorData data;
    data.width  = inking::kDesignWidth;
    data.height = inking::kDesignHeight;
    return data;
}

/**
 * 设计单位 → 设备像素的倍率。
 *
 * letterbox 是整幅等比缩放，所以取两轴里小的那个，多出来的空间就是黑边。
 * 它只影响绘制换算，不标脏（docs/API.md「标脏」）；窗口层是唯一来源，
 * 组件不自己倒推。
 */
float letterboxMagnification(SDL_Window* window) {
    int pixelWidth = 0;
    int pixelHeight = 0;
    if (window == nullptr
        || !SDL_GetWindowSizeInPixels(window, &pixelWidth, &pixelHeight)
        || pixelWidth <= 0 || pixelHeight <= 0) {
        return 1.0f;
    }

    const float scaleX = static_cast<float>(pixelWidth)
                       / static_cast<float>(inking::kDesignWidth);
    const float scaleY = static_cast<float>(pixelHeight)
                       / static_cast<float>(inking::kDesignHeight);
    return scaleX < scaleY ? scaleX : scaleY;
}

/**
 * 窗口的运行期状态。
 *
 * 放在实现文件的匿名命名空间里，是为了让公开头文件保持"零 SDL 依赖"：
 * 谁包含 <window/InkingWindow.h> 都不会被迫跟着包含 SDL 的头文件。
 * InkingWindow 本身是单例，这份状态也天然只有一份。
 */
struct WindowState {
    SDL_Window*   window = nullptr;     /** SDL 窗口句柄 */
    SDL_Renderer* renderer = nullptr;   /** SDL 渲染器句柄 */
    bool          rendererReady = false;/** 渲染器是否真的建起来了 */
    bool          vsyncActive = false;  /** vsync 是否接上了（决定还要不要自己 sleep） */
    bool          open = false;         /** 是否已经成功打开 */
    std::string   sceneName;            /** 当前要展示的场景名 */
};

/** 返回唯一的窗口状态。 */
WindowState& state() {
    static WindowState instance;
    return instance;
}

/** 释放 SDL 资源并复位状态。 */
void releaseWindow(WindowState& s) {
    if (s.renderer) {
        SDL_DestroyRenderer(s.renderer);
        s.renderer = nullptr;
    }
    s.rendererReady = false;

    if (s.window) {
        SDL_DestroyWindow(s.window);
        s.window = nullptr;
    }

    if (s.open) {
        SDL_Quit();
        s.open = false;
    }

    s.sceneName.clear();
}

/**
 * 窗口坐标 → 设计坐标。
 *
 * letterbox 之下这是两套坐标：SDL 给的是窗口像素，场景认的是设计空间
 * （kDesignWidth × kDesignHeight）。换算是渲染器的职责，所以只在这里做一次，
 * 交给场景层的永远是设计坐标。
 *
 * 必须走渲染器换算，不能拿展示倍率乘除：letterbox 下设计画面只占窗口中间一块，
 * 上下（或左右）还有黑边，换算里有偏移。实测 1000×1200 的窗口配 1920×1080 的
 * 设计——等比倍率 0.5208、上下黑边各 318.8：
 *   正中 (500,600)：SDL 给 (960,540)，除以倍率会算成 (960,1152)；
 *   (500,0) 在黑边里：SDL 给负的 y（正确地判为空白），除以倍率会算成 y=0，在顶边误命中。
 *
 * 这条同时说明：**整体缩放不需要重烘命中表**——表建在设计坐标里，缩放只写
 * Magnification（不标脏），鼠标坐标每帧换算回设计空间再查表。
 */
void toDesign(SDL_Renderer* renderer, float windowX, float windowY,
              float& designX, float& designY) {
    designX = windowX;
    designY = windowY;
    if (renderer != nullptr) {
        SDL_RenderCoordinatesFromWindow(renderer, windowX, windowY,
                                        &designX, &designY);
    }
}

/**
 * 事件泵：把这一帧攒下的 SDL 事件吃干净，变成 MouseInput 里的状态。
 * 返回 false 表示该退出主循环了。
 *
 * 这里只“更新状态”，不派发事件：命中与投递留给下一帧的 Scene 层，
 * 这样才和设计稿里的 isDirty + query 流程对得上。
 */
bool pumpEvents(WindowState& s, MouseInput& mouse) {
    bool running = true;
    SDL_Event event{};

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
            case SDL_EVENT_QUIT:
                INK_LOG_INFO(kModuleName, "收到退出事件");
                running = false;
                break;

            case SDL_EVENT_KEY_DOWN:
                if (event.key.key == SDLK_ESCAPE) {
                    INK_LOG_INFO(kModuleName, "按下 ESC，退出主循环");
                    running = false;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_WHEEL:
                mouse.UpdateFromSDL(event);
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
                mouse.UpdateFromSDL(event);
                // 只记按下与抬起，移动不记——否则 Log.html 会被鼠标刷屏。
                INK_LOG_DEBUG(kModuleName,
                              std::string("鼠标") + (event.button.down ? "按下 " : "抬起 ")
                                  + std::to_string(static_cast<int>(event.button.button))
                                  + " @窗口 ("
                                  + std::to_string(static_cast<int>(event.button.x)) + ", "
                                  + std::to_string(static_cast<int>(event.button.y)) + ")");
                break;

            default:
                break;
        }
    }

    // 设计坐标一帧只换算一次：一帧里收到多少个移动事件都只算一次，
    // 和“每帧只判断一次”同拍。
    const MousePosition& windowPoint = mouse.GetState().position;
    float designX = 0.0f;
    float designY = 0.0f;
    toDesign(s.renderer, static_cast<float>(windowPoint.x),
             static_cast<float>(windowPoint.y), designX, designY);
    mouse.SetDesignPosition(designX, designY);

    return running;
}

/**
 * 每帧把指针状态喂给活跃场景，并派发一次。
 *
 * 位置：事件泵之后（设计坐标刚算完）、时间线推进之前——和
 * `SceneLibrary::RenderScene` 同一个落点（"只有活跃场景"这条规矩的唯一出口）。
 *
 * 为什么由窗口层喂：设计坐标的换算要走渲染器（letterbox 有黑边偏移），
 * 那是渲染器的职责，场景层不该认识渲染器（docs/AGENTS.md §6 第 8 条）。
 * 场景只接受"设计坐标 + 按没按下"这四个值。
 *
 * 为什么不在事件循环里直接派发：一帧里可能有多个移动事件，逐个派发等于
 * 同一帧判断多次；docs/InputDesign.md §1 定的是"每帧只判断一次"。
 */
void dispatchPointer(WindowState& s, const MouseInput& mouse) {
    InkingScene* scene = SceneLibrary::GetActiveScene();
    if (scene == nullptr) {
        return;
    }

    const MouseState& state = mouse.GetState();
    // 指针在不在窗口里：移出去时不该继续悬停，也不该在窗口外"按下"。
    const bool inside =
        s.window != nullptr
        && (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MOUSE_FOCUS) != 0;

    scene->SetPointerState(state.design.x, state.design.y, state.leftButtonDown,
                           inside);
    scene->DispatchPointer();
}

/**
 * 每帧把展示倍率刷成当前值。
 *
 * 不靠事件驱动：全屏切换、移动到不同 DPI 的显示器、系统缩放变化……SDL 发的事件
 * 并不统一（实测进全屏会发 PIXEL_SIZE_CHANGED **和** DISPLAY_SCALE_CHANGED，
 * 后者我们没接）。反正只是两次除法，每帧从当前像素尺寸重算最省心，也永远不会
 * 落后一帧。值没变就什么都不做。
 *
 * 设计坐标里的几何一点没动——这正是"场景不做布局变化"和"整体缩放不用重烘表"
 * 能成立的原因。
 */
void syncMagnification(WindowState& s, InkingWindow& window) {
    const float magnification = letterboxMagnification(s.window);
    const float previous = window.GetMagnification();
    if (magnification > previous - 1e-6f && magnification < previous + 1e-6f) {
        return;
    }
    window.SetMagnification(magnification);
    INK_LOG_DEBUG(kModuleName, "展示倍率=" + std::to_string(magnification));
}

/**
 * 上一帧真的渲染了哪个场景（没有场景时为 nullptr）。
 *
 * 纯自检用：主循环跑完、窗口释放时会清掉场景名，所以"窗口确实按名字解析到
 * 场景并把它渲染出来了"这件事，只能靠这里的记录来验。
 * 函数内静态量，避开静态初始化顺序问题（docs/AGENTS.md §6 第 12 条）。
 */
InkingScene*& lastRenderedScene() {
    static InkingScene* scene = nullptr;
    return scene;
}

/**
 * 绘制这一帧：底色 → 当前场景的绘制列表 → 指针标记。
 *
 * 场景那一笔是真正的渲染路径：`InkingScene::Render` 在内部把顺序重建到最新
 * （惰性、每帧最多一次），再按 z 序把每个节点提交给渲染器。
 *
 * 指针小方块是临时脚手架，作用只有一个——肉眼确认「窗口坐标 → 设计坐标」
 * 换算对不对：拉成别的窗口比例，方块也应该准确贴在指针上。
 * SDF 形状层 / 样式层接手后，这一段连同 SDL_RenderFillRect 一起删掉。
 */
void drawFrame(WindowState& s, const MouseInput& mouse) {
    SDL_Renderer* renderer = s.renderer;

    // 自检钩子：记录这一帧真的渲染了哪个场景。
    // 窗口释放时会清掉场景名，所以"窗口确实解析到场景并渲染了"这件事
    // 只能靠这里的记录来验。
    lastRenderedScene() = SceneLibrary::GetActiveScene();

    if (renderer == nullptr) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, kBackgroundRed, kBackgroundGreen,
                           kBackgroundBlue, 255);
    SDL_RenderClear(renderer);

    // 当前场景的整棵渲染树。经 SceneLibrary 走一道，而不是直接调场景的
    // Render（那是 protected）：这样"只有活跃场景会被渲染"这条规矩只有
    // 一个落点，将来上合批时提交层也落在这里。没有活跃场景时返回 false。
    SceneLibrary::RenderScene(renderer);

    if (s.rendererReady) {
        const MousePoint& point = mouse.GetState().design;
        constexpr float kCursorSize = 16.0f;
        const SDL_FRect cursor{point.x - kCursorSize * 0.5f,
                               point.y - kCursorSize * 0.5f,
                               kCursorSize, kCursorSize};
        SDL_SetRenderDrawColor(renderer, 240, 200, 80, 255);
        SDL_RenderFillRect(renderer, &cursor);
    }

    SDL_RenderPresent(renderer);
}

/**
 * 主循环：事件泵 → 展示倍率 → 时间线 → 绘制 → 帧末收尾 → 节流。
 *
 * 时间线是这里的节拍器：逻辑步固定 50Hz，每帧通道和渲染同拍。
 * 接上 vsync 之后节流交给 SDL，不再自己 sleep。
 */
void runLoop(WindowState& s, MouseInput& mouse, InkingWindow& window) {
    const bool autoQuit = SDL_getenv("INK_AUTOQUIT") != nullptr;
    const auto start = std::chrono::steady_clock::now();

    // 整个 UI 的唯一时间源。逻辑步长取默认的 1/50s。
    Timeline timeline;

    timeline.SetLogicCallback([](double fixedStep) {
        // 固定逻辑步：每次派发都喂同一个步长，状态机要的就是确定的步。
        if (InkingScene* scene = SceneLibrary::GetActiveScene()) {
            scene->TickLogic(fixedStep);
        }
    });

    // 每帧通道跟着渲染走：真实 delta，和这一帧画出来的东西对齐。
    timeline.SetFrameCallback([](double delta) {
        if (InkingScene* scene = SceneLibrary::GetActiveScene()) {
            scene->TickFrame(delta);
        }
    });

    auto previousFrame = std::chrono::steady_clock::now();
    bool running = true;

    while (running) {
        // 1. 事件泵：退出、键盘、鼠标都在这里变成状态。
        running = pumpEvents(s, mouse);

        // 2. 展示倍率跟着窗口 / DPI 走。
        syncMagnification(s, window);

        // 2b. 指针：把设计坐标与按键状态喂给活跃场景，并派发一次
        //     （悬停进出 / 按下 / 抬起触发点击）。放在推进时间线之前，
        //     这样本帧画出来的就是本帧的交互结果，不会差一帧。
        dispatchPointer(s, mouse);

        // 3. 推进时间线。delta 用**真实流逝时间**，所以某帧掉到 30ms 也不会
        //    让逻辑时间相对墙上时钟漂移——累加器会把欠的步补上。
        const auto now = std::chrono::steady_clock::now();
        const double deltaSeconds =
            std::chrono::duration<double>(now - previousFrame).count();
        previousFrame = now;

        timeline.Advance(deltaSeconds);

        // 4. 绘制：底色 + 当前场景的整棵渲染树 + 指针标记。
        drawFrame(s, mouse);

        // 5. 帧末收尾：瞬时状态一帧只清一次。放在这里而不是事件循环里，
        //    否则一帧里的多个移动事件会被后一个清掉。
        mouse.ResetFrameFlags();

        // 6. 节流。接了 vsync 就交给 SDL（它会阻塞到下一次垂直回扫），
        //    没接上时才退回固定 sleep——sleep 不扣本帧耗时，
        //    实际是 16ms + 工作时间，这是没 vsync 时的将就之法。
        if (!s.vsyncActive) {
            std::this_thread::sleep_for(kFrameInterval);
        }

        if (autoQuit
            && std::chrono::steady_clock::now() - start > kAutoQuitAfter) {
            running = false;
        }
    }
}

}  // namespace

InkingWindow::InkingWindow() : InkingStaticAnchor(nullptr, rootAnchorData()) {}

InkingScene* GetLastRenderedScene() noexcept {
    return lastRenderedScene();
}

InkingWindow::~InkingWindow() {
    WindowState& s = state();
    if (s.open) {
        INK_LOG_INFO(kModuleName, "窗口销毁");
    }
    releaseWindow(s);
}

InkingWindow& InkingWindow::Instance() {
    static InkingWindow instance;
    return instance;
}

InkingScene* InkingWindow::GetCurrentScene() const {
    // 当前活跃（正在渲染）的场景。活跃态由 SceneLibrary 维护：
    // 切场景时旧的自动进入静默，所以这里拿到的永远是"该渲染的那一个"。
    return SceneLibrary::GetActiveScene();
}

int InkingWindow::GetWindowWidth() const noexcept {
    return _windowWidth;
}

int InkingWindow::GetWindowHeight() const noexcept {
    return _windowHeight;
}

bool InkingWindow::setWindowWidth(int width) {
    if (width <= 0) {
        INK_LOG_ERROR(kModuleName, "宽度必须为正数：" + std::to_string(width));
        return false;
    }

    _windowWidth = width;

    // 窗口已经开出来时立刻跟随；还没开就只是记下来，等 Show() 时用
    WindowState& s = state();
    if (s.window) {
        SDL_SetWindowSize(s.window, _windowWidth, _windowHeight);
        // 展示倍率不用在这里算：主循环每帧都会刷一次（见 syncMagnification）。
    }

    INK_LOG_DEBUG(kModuleName, "窗口宽度=" + std::to_string(_windowWidth));
    return true;
}

bool InkingWindow::setWindowHeight(int height) {
    if (height <= 0) {
        INK_LOG_ERROR(kModuleName, "高度必须为正数：" + std::to_string(height));
        return false;
    }

    _windowHeight = height;

    WindowState& s = state();
    if (s.window) {
        SDL_SetWindowSize(s.window, _windowWidth, _windowHeight);
        // 同上：倍率由主循环每帧刷新。
    }

    INK_LOG_DEBUG(kModuleName, "窗口高度=" + std::to_string(_windowHeight));
    return true;
}

/**
 * 按名字把某个场景设为活跃；找不到 / 名字为空时返回 false。
 *
 * 活跃态统一交给 SceneLibrary 管：切换时旧的自动进入静默（不渲染、
 * 跟着渲染的逻辑也停），新的开始活跃。窗口自己不保存场景指针，
 * 免得在场景关闭 / 析构之后留下野指针。
 */
bool showSceneByName(const std::string& sceneName) {
    if (sceneName.empty()) {
        SceneLibrary::SetActiveScene(nullptr);
        return false;
    }

    InkingScene* scene = SceneLibrary::FindScene(sceneName);
    if (scene == nullptr) {
        INK_LOG_WARN(kModuleName, "找不到场景：" + sceneName);
        return false;
    }

    SceneLibrary::SetActiveScene(scene);
    return true;
}

void InkingWindow::Show(const std::string& sceneName) {
    WindowState& s = state();

    if (s.open) {
        // 主循环跑起来之后，只有场景回调里再调 Show() 会走到这条分支——
        // 那正是运行期换场景的用法，所以这里是“切场景”而不是“忽略重复调用”。
        // 旧场景会在这里自动进入静默。
        s.sceneName = sceneName;
        showSceneByName(sceneName);
        INK_LOG_INFO(kModuleName, "窗口已经打开，切换场景：" + sceneName);
        return;
    }

    // SDL3 的 SDL_Init 返回 bool：true 才是成功（SDL2 是"0 表示成功"）
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        INK_LOG_ERROR(kModuleName, std::string("SDL_Init 失败：") + SDL_GetError());
        return;
    }

    s.window = SDL_CreateWindow(
        "InkingWindow",
        _windowWidth,
        _windowHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!s.window) {
        INK_LOG_ERROR(kModuleName,
                      std::string("SDL_CreateWindow 失败：") + SDL_GetError());
        SDL_Quit();
        return;
    }

    // 渲染器建不起来（无 GPU / 无显示 / 驱动缺失）时只降级、不退出：
    // 窗口和主循环照旧跑，只是这一帧画不出东西——渲染树本身仍然每帧更新，
    // 自检和坐标换算都还能验。以前这里是"渲染器失败就整个放弃"，
    // 在无头环境（CI、冒烟测试）下直接把窗口也毙了。
    s.renderer = SDL_CreateRenderer(s.window, nullptr);
    s.rendererReady = (s.renderer != nullptr);
    if (!s.rendererReady) {
        INK_LOG_WARN(kModuleName,
                     std::string("SDL_CreateRenderer 失败，降级为无渲染：")
                         + SDL_GetError());
    } else {
        // 设计尺寸只在这里用一次：它定义的是逻辑坐标系，
        // letterbox 保证任何窗口比例下 UI 都不变形，多余空间留黑边。
        SDL_SetRenderLogicalPresentation(
            s.renderer,
            inking::kDesignWidth,
            inking::kDesignHeight,
            SDL_LOGICAL_PRESENTATION_LETTERBOX);

        // -------------------------------------------------------------------
        // 打开绘制的 alpha 混合。
        //
        // SDL 的**绘制**混合模式默认是 `SDL_BLENDMODE_NONE`（和纹理的
        // `SDL_SetTextureBlendMode` 是两套东西）：那时 `SDL_SetRenderDrawColor`
        // 的 alpha 会被**原样写进目标**，不参与合成。后果是"半透明"根本不存在——
        // 按钮 hover 把 0.75 改成 0.85，画出来只是把底色整块换成另一个不透明的
        // 黑，眼睛看不出任何变化（实测：BLENDMODE_NONE 下按钮里读出 0xBF000000、
        // 打开之后读出 0xFF040406，后者才是"0.75 的黑叠在底色上"）。
        //
        // 为什么在这里设一次而不是每个组件各设一次：混合是**渲染器级的开关**，
        // 每个组件都调一遍纯属重复；而且漏一个就会出现"界面上有的半透明生效、
        // 有的不生效"——那种不一致比整块不透明难查得多。
        //
        // 副作用（预期内）：所有画出来的颜色从此都按 alpha 合成，
        // `AnchorData::color` 里的 alpha 开始真的有意义。
        // -------------------------------------------------------------------
        SDL_SetRenderDrawBlendMode(s.renderer, SDL_BLENDMODE_BLEND);

        // vsync：接上之后 SDL_RenderPresent 会阻塞到下一次垂直回扫，
        // 主循环就不用自己 sleep 了——固定 sleep 不扣本帧耗时，
        // 实际跑出来是 16ms + 工作时间，帧率会低于 60 且抖动。
        // 无头 / 无显示环境下可能接不上，那时退回 sleep（见 runLoop）。
        s.vsyncActive = SDL_SetRenderVSync(s.renderer, 1);
        if (s.vsyncActive) {
            INK_LOG_DEBUG(kModuleName, "vsync 已接上，节流交给 SDL");
        } else {
            INK_LOG_WARN(kModuleName,
                         std::string("vsync 没接上，退回固定节流：")
                             + SDL_GetError());
        }
    }

    // 展示倍率：设计单位 → 设备像素。窗口层写一次，之后每帧跟着窗口尺寸刷。
    SetMagnification(letterboxMagnification(s.window));

    s.open = true;
    s.sceneName = sceneName;

    // 场景的活跃态在这里落地：把指定场景设为"正在渲染"的那一个。
    // 名字找不到（还没构造 / 已被关闭）时只记名字，画面上就没有场景。
    showSceneByName(sceneName);

    INK_LOG_PASS(kModuleName,
                 "窗口已打开 " + std::to_string(_windowWidth) + "x"
                     + std::to_string(_windowHeight)
                     + "，设计尺寸 " + std::to_string(inking::kDesignWidth) + "x"
                     + std::to_string(inking::kDesignHeight)
                     + "，展示倍率 " + std::to_string(GetMagnification())
                     + "，场景：" + sceneName);

    // Show() 是启动点：窗口开出来之后就在这里进主循环，一直阻塞到窗口关闭
    // （或按 ESC），返回前把窗口释放掉。谁想让主循环跑在别的线程上，
    // 以后再加一个不阻塞的入口，别把 Show() 拆成两种语义。
    runLoop(s, _mouseInput, *this);

    INK_LOG_INFO(kModuleName, "主循环退出，释放窗口");
    releaseWindow(s);
}

}  // namespace ink

