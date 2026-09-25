#include <window/InkingWindow.h>

#include <ink/basic/InkLog.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <string>
#include <thread>

namespace ink {

namespace {

constexpr const char* kModuleName = "InkingWindow";

/// 帧节流：vsync 还没接上，先用固定节拍（16ms ≈ 60Hz）。
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

/**
 * 窗口的运行期状态。
 *
 * 放在实现文件的匿名命名空间里，是为了让公开头文件保持"零 SDL 依赖"：
 * 谁包含 <window/InkingWindow.h> 都不会被迫跟着包含 SDL 的头文件。
 * InkingWindow 本身是单例，这份状态也天然只有一份。
 */
struct WindowState {
    SDL_Window*   window = nullptr;   /** SDL 窗口句柄 */
    SDL_Renderer* renderer = nullptr; /** SDL 渲染器句柄 */
    bool          open = false;       /** 是否已经成功打开 */
    std::string   sceneName;          /** 当前要展示的场景名 */
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
 * 占位绘制：底色 + 跟着指针走的小方块。
 *
 * 这是临时脚手架，作用只有一个——肉眼确认「窗口坐标 → 设计坐标」换算对不对：
 * 拉成别的窗口比例，方块也应该准确贴在指针上。
 * Canvas / 场景绘制落地后，这个函数整段删掉。
 */
void drawPlaceholder(SDL_Renderer* renderer, const MouseInput& mouse) {
    if (renderer == nullptr) {
        return;
    }

    SDL_SetRenderDrawColor(renderer, kBackgroundRed, kBackgroundGreen,
                           kBackgroundBlue, 255);
    SDL_RenderClear(renderer);

    const MousePoint& point = mouse.GetState().design;
    constexpr float kCursorSize = 16.0f;
    const SDL_FRect cursor{point.x - kCursorSize * 0.5f,
                           point.y - kCursorSize * 0.5f,
                           kCursorSize, kCursorSize};
    SDL_SetRenderDrawColor(renderer, 86, 156, 214, 255);
    SDL_RenderFillRect(renderer, &cursor);

    SDL_RenderPresent(renderer);
}

/**
 * 主循环：事件泵 → 鼠标换算 → 每帧更新 → 绘制 → 帧末收尾 → 节流。
 *
 * 骨架期还没有场景，所以“每帧更新”和“绘制”都是占位的；等 Scene / Canvas
 * 落地，替换的只是这两段，循环骨架不用动。
 */
void runLoop(WindowState& s, MouseInput& mouse) {
    const bool autoQuit = SDL_getenv("INK_AUTOQUIT") != nullptr;
    const auto start = std::chrono::steady_clock::now();
    bool running = true;

    while (running) {
        // 1. 事件泵：退出、键盘、鼠标都在这里变成状态。
        running = pumpEvents(s, mouse);

        // 2. 每帧更新（占位）：Scene 落地后这里做 isDirty → query。
        //    现在唯一的输入源就是鼠标移动，标脏语义等 Scene 接上再补。

        // 3. 绘制（占位）。
        drawPlaceholder(s.renderer, mouse);

        // 4. 帧末收尾：瞬时状态一帧只清一次。放在这里而不是事件循环里，
        //    否则一帧里的多个移动事件会被后一个清掉。
        mouse.ResetFrameFlags();

        // 5. 节流。
        std::this_thread::sleep_for(kFrameInterval);

        if (autoQuit
            && std::chrono::steady_clock::now() - start > kAutoQuitAfter) {
            running = false;
        }
    }
}

}  // namespace

InkingWindow::InkingWindow() = default;

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

int InkingWindow::GetWidth() const noexcept {
    return _width;
}

int InkingWindow::GetHeight() const noexcept {
    return _height;
}

bool InkingWindow::setWidth(int width) {
    if (width <= 0) {
        INK_LOG_ERROR(kModuleName, "宽度必须为正数：" + std::to_string(width));
        return false;
    }

    _width = width;

    // 窗口已经开出来时立刻跟随；还没开就只是记下来，等 Show() 时用
    WindowState& s = state();
    if (s.window) {
        SDL_SetWindowSize(s.window, _width, _height);
    }

    INK_LOG_DEBUG(kModuleName, "窗口宽度=" + std::to_string(_width));
    return true;
}

bool InkingWindow::setHeight(int height) {
    if (height <= 0) {
        INK_LOG_ERROR(kModuleName, "高度必须为正数：" + std::to_string(height));
        return false;
    }

    _height = height;

    WindowState& s = state();
    if (s.window) {
        SDL_SetWindowSize(s.window, _width, _height);
    }

    INK_LOG_DEBUG(kModuleName, "窗口高度=" + std::to_string(_height));
    return true;
}

void InkingWindow::Show(const std::string& sceneName) {
    WindowState& s = state();

    if (s.open) {
        // 主循环跑起来之后，只有场景回调里再调 Show() 会走到这条分支——
        // 那正是运行期换场景的用法，所以这里是“切场景”而不是“忽略重复调用”。
        s.sceneName = sceneName;
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
        _width,
        _height,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!s.window) {
        INK_LOG_ERROR(kModuleName,
                      std::string("SDL_CreateWindow 失败：") + SDL_GetError());
        SDL_Quit();
        return;
    }

    s.renderer = SDL_CreateRenderer(s.window, nullptr);
    if (!s.renderer) {
        INK_LOG_ERROR(kModuleName,
                      std::string("SDL_CreateRenderer 失败：") + SDL_GetError());
        SDL_DestroyWindow(s.window);
        s.window = nullptr;
        SDL_Quit();
        return;
    }

    // 设计尺寸只在这里用一次：它定义的是逻辑坐标系，
    // letterbox 保证任何窗口比例下 UI 都不变形，多余空间留黑边。
    SDL_SetRenderLogicalPresentation(
        s.renderer,
        inking::kDesignWidth,
        inking::kDesignHeight,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);

    s.open = true;
    s.sceneName = sceneName;

    INK_LOG_PASS(kModuleName,
                 "窗口已打开 " + std::to_string(_width) + "x" + std::to_string(_height)
                     + "，设计尺寸 " + std::to_string(inking::kDesignWidth) + "x"
                     + std::to_string(inking::kDesignHeight)
                     + "，场景：" + sceneName);

    // Show() 是启动点：窗口开出来之后就在这里进主循环，一直阻塞到窗口关闭
    // （或按 ESC），返回前把窗口释放掉。谁想让主循环跑在别的线程上，
    // 以后再加一个不阻塞的入口，别把 Show() 拆成两种语义。
    runLoop(s, _mouseInput);

    INK_LOG_INFO(kModuleName, "主循环退出，释放窗口");
    releaseWindow(s);
}

}  // namespace ink

