#include <window/InkingWindow.h>

#include <ink/basic/InkLog.h>
#include <ink/dataStruct/MessageStruct.h>
#include <ink/ui/InputRouter.h>
#include <ink/ui/MessageQueue.h>
#include <ink/ui/Scene.h>
#include <ink/ui/SceneRegistry.h>
#include <window/SdlCanvas.h>

#include <SDL3/SDL.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <string>
#include <thread>

namespace ink {

namespace {

constexpr const char* kModuleName = "InkingWindow";

/// 帧节流：vsync 还没接上，先用固定节拍。
constexpr std::chrono::microseconds kFrameInterval{16000};

/// 无头冒烟：设了环境变量 INK_AUTOQUIT 就跑这么久然后自己退出。
constexpr std::chrono::milliseconds kAutoQuitAfter{2000};

/// 帧转储：设了环境变量 INK_DUMPFRAME=<路径> 就在第 N 帧存一张 BMP。
/// 给没有显示器的环境（CI、远程无头会话）确认布局用。
constexpr int kDumpFrameIndex = 30;

/// 窗口底色。
constexpr Color kBackground{18, 18, 24, 255};

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
    bool          borderless = false; /** 是否无边框 */
    std::string   sceneName;          /** 当前要展示的场景名 */

    // 手工拖动：无边框窗口没有系统标题栏，抓着标题栏搬窗口得自己做。
    bool  dragging = false;
    float dragOffsetX = 0.0f; /** 鼠标抓住窗口时，鼠标在窗口内的横向位置 */
    float dragOffsetY = 0.0f;
};

/** 开始拖窗口：记下鼠标此时在窗口里的位置，之后照着这个偏移搬。 */
void beginWindowDrag(WindowState& s) {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    int windowX = 0;
    int windowY = 0;
    // 注意用全局鼠标坐标：窗口一开始跟着动，窗口内的相对坐标就失真了。
    SDL_GetGlobalMouseState(&mouseX, &mouseY);
    if (!SDL_GetWindowPosition(s.window, &windowX, &windowY)) {
        return;
    }
    s.dragging = true;
    s.dragOffsetX = mouseX - static_cast<float>(windowX);
    s.dragOffsetY = mouseY - static_cast<float>(windowY);
}

/** 拖动中：让窗口跟着鼠标走（偏移保持不变）。 */
void updateWindowDrag(WindowState& s) {
    float mouseX = 0.0f;
    float mouseY = 0.0f;
    SDL_GetGlobalMouseState(&mouseX, &mouseY);
    SDL_SetWindowPosition(s.window,
                          static_cast<int>(std::lround(mouseX - s.dragOffsetX)),
                          static_cast<int>(std::lround(mouseY - s.dragOffsetY)));
}

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
 * letterbox 之下这是两套坐标：鼠标事件给的是窗口像素，场景认的是
 * 设计空间（1920×1080）。换算是渲染器的事，所以只在这里做一次，
 * 交给场景层的永远是设计坐标。
 */
void toDesign(SDL_Renderer* renderer, float windowX, float windowY,
              float& designX, float& designY) {
    designX = windowX;
    designY = windowY;
    if (renderer != nullptr) {
        SDL_RenderCoordinatesFromWindow(renderer, windowX, windowY, &designX, &designY);
    }
}

/** SDL 的鼠标键号 → 框架的指针按键。 */
PointerButton toButton(std::uint8_t sdlButton) {
    switch (sdlButton) {
        case SDL_BUTTON_LEFT:   return PointerButton::Left;
        case SDL_BUTTON_MIDDLE: return PointerButton::Middle;
        case SDL_BUTTON_RIGHT:  return PointerButton::Right;
        default:                return PointerButton::Unknown;
    }
}

/**
 * 执行场景提上来的窗口命令（场景层不认识窗口，只能提要求）。
 * 返回 false 表示"该退出主循环了"。
 */
bool applyWindowCommand(SDL_Window* window, WindowCommand command) {
    switch (command) {
        case WindowCommand::Minimize:
            SDL_MinimizeWindow(window);
            return true;
        case WindowCommand::Maximize:
            SDL_MaximizeWindow(window);
            return true;
        case WindowCommand::Restore:
            SDL_RestoreWindow(window);
            return true;
        case WindowCommand::ToggleMaximize:
            if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0) {
                SDL_RestoreWindow(window);
            } else {
                SDL_MaximizeWindow(window);
            }
            return true;
        case WindowCommand::BeginDrag:
            return true;  // 交给 beginWindowDrag/updateWindowDrag 处理
        case WindowCommand::Close:
            return false;
        case WindowCommand::None:
            return true;
    }
    return true;
}

/** 把 UI 消息队列里攒的消息倒进日志——队列就是给 UI 线程这么用的。 */
void flushMessages() {
    for (const Message& message : MessageQueue::drainMessages()) {
        switch (message.type) {
            case MessageType::Pass:   INK_LOG_PASS(kModuleName, message.message); break;
            case MessageType::Warn:   INK_LOG_WARN(kModuleName, message.message); break;
            case MessageType::Error:  INK_LOG_ERROR(kModuleName, message.message); break;
            case MessageType::Normal: INK_LOG_INFO(kModuleName, message.message); break;
        }
    }
}

/** 把当前这帧的像素存成 BMP（开发用，平时不调用）。 */
void dumpFrame(SDL_Renderer* renderer, const char* path) {
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr) {
        INK_LOG_WARN(kModuleName, std::string("帧转储失败：") + SDL_GetError());
        return;
    }

    if (SDL_SaveBMP(surface, path)) {
        INK_LOG_INFO(kModuleName, std::string("帧已转储：") + path);
    } else {
        INK_LOG_WARN(kModuleName, std::string("帧转储写盘失败：") + SDL_GetError());
    }
    SDL_DestroySurface(surface);
}

/**
 * 主循环：事件泵 → 窗口命令 → 每帧回调 → 绘制。
 *
 * 骨架期只有"输入 → 映射表 → 事件"和"平铺表 → 绘制"两件事，
 * 渲染层（DrawCall 合批 + 静态烘焙）落地后替换的是绘制那一段。
 */
void runLoop(Scene& rootScene, WindowState& s) {
    SdlCanvas canvas(s.renderer);

    InputRouter router;
    router.attach(rootScene);

    // 静态场景先烘一遍：之后每帧只查表，不再遍历树。
    if (!rootScene.IsDynamic()) {
        rootScene.bake();
    }

    const bool autoQuit = SDL_getenv("INK_AUTOQUIT") != nullptr;
    const auto start = std::chrono::steady_clock::now();
    auto previous = start;
    bool running = true;
    bool maximized = (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MAXIMIZED) != 0;
    const char* dumpPath = SDL_getenv("INK_DUMPFRAME");
    int frameIndex = 0;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            switch (event.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (event.key.key == SDLK_ESCAPE) {
                        running = false;
                    }
                    break;
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    if (!maximized) {
                        maximized = true;
                        rootScene.onWindowStateChanged(true);
                    }
                    break;
                case SDL_EVENT_WINDOW_MINIMIZED:
                case SDL_EVENT_WINDOW_RESTORED:
                    if (maximized) {
                        maximized = false;
                        rootScene.onWindowStateChanged(false);
                    }
                    break;
                case SDL_EVENT_MOUSE_MOTION: {
                    if (s.dragging) {
                        // 拖动中：窗口跟着鼠标走，顺便跳过悬停计算——
                        // 窗口自己动的时候，"鼠标在窗口内哪儿"毫无意义。
                        updateWindowDrag(s);
                        break;
                    }
                    float x = 0.0f;
                    float y = 0.0f;
                    toDesign(s.renderer, event.motion.x, event.motion.y, x, y);
                    router.pointerMove(x, y);
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    float x = 0.0f;
                    float y = 0.0f;
                    toDesign(s.renderer, event.button.x, event.button.y, x, y);
                    router.pointerDown(x, y, toButton(event.button.button));
                    break;
                }
                case SDL_EVENT_MOUSE_BUTTON_UP: {
                    s.dragging = false;
                    float x = 0.0f;
                    float y = 0.0f;
                    toDesign(s.renderer, event.button.x, event.button.y, x, y);
                    router.pointerUp(x, y, toButton(event.button.button));
                    break;
                }
                default:
                    break;
            }
        }

        // 场景想让窗口做的事，攒到这里统一办。
        for (WindowCommand command = rootScene.takeWindowCommand();
             command != WindowCommand::None;
             command = rootScene.takeWindowCommand()) {
            INK_LOG_DEBUG(kModuleName, std::string("执行窗口命令：") + toString(command));
            if (command == WindowCommand::BeginDrag) {
                beginWindowDrag(s);
                continue;
            }
            if (!applyWindowCommand(s.window, command)) {
                running = false;
                break;
            }
            if (command == WindowCommand::Maximize
                || command == WindowCommand::ToggleMaximize) {
                const bool nowMaximized =
                    (SDL_GetWindowFlags(s.window) & SDL_WINDOW_MAXIMIZED) != 0;
                if (nowMaximized != maximized) {
                    maximized = nowMaximized;
                    rootScene.onWindowStateChanged(maximized);
                }
            }
        }

        // 静态场景被改过就重烘焙：一帧最多付一次。
        if (!rootScene.IsDynamic() && rootScene.needsBake()) {
            rootScene.bake();
        }

        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - previous).count();
        previous = now;
        rootScene.tick(deltaSeconds);

        canvas.clear(kBackground);
        rootScene.draw(canvas);
        if (dumpPath != nullptr && frameIndex == kDumpFrameIndex) {
            dumpFrame(s.renderer, dumpPath);
        }
        canvas.present();
        ++frameIndex;

        flushMessages();

        if (autoQuit
            && std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
                   > kAutoQuitAfter) {
            running = false;
        }

        std::this_thread::sleep_for(kFrameInterval);
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

bool InkingWindow::GetBorderless() const noexcept {
    return _borderless;
}

void InkingWindow::setBorderless(bool borderless) {
    if (_borderless == borderless) {
        return;
    }
    _borderless = borderless;
    state().borderless = borderless;
    INK_LOG_DEBUG(kModuleName, _borderless ? "无边框窗口" : "带边框窗口");
}

void InkingWindow::Show(const std::string& sceneName) {
    WindowState& s = state();

    if (s.open) {
        // Show() 会一直阻塞到窗口关闭，所以"已经打开"只能是重复调用。
        INK_LOG_WARN(kModuleName, "窗口已经打开，忽略这次 Show：" + sceneName);
        return;
    }

    // SDL3 的 SDL_Init 返回 bool：true 才是成功（SDL2 是"0 表示成功"）
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        INK_LOG_ERROR(kModuleName, std::string("SDL_Init 失败：") + SDL_GetError());
        return;
    }

    std::uint64_t flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (_borderless) {
        flags |= SDL_WINDOW_BORDERLESS;
    }

    s.window = SDL_CreateWindow("InkingWindow", _width, _height, flags);
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
    s.borderless = _borderless;
    s.sceneName = sceneName;

    // 没给名字就用最后登记的那个场景——单页程序不必自己记场景名。
    Scene* rootScene =
        sceneName.empty() ? SceneRegistry::latest() : SceneRegistry::find(sceneName);
    if (rootScene == nullptr) {
        INK_LOG_ERROR(kModuleName, "场景库里找不到场景：" + sceneName);
        releaseWindow(s);
        return;
    }

    INK_LOG_PASS(kModuleName,
                 "窗口已打开 " + std::to_string(_width) + "x" + std::to_string(_height)
                     + "，设计尺寸 " + std::to_string(inking::kDesignWidth) + "x"
                     + std::to_string(inking::kDesignHeight)
                     + "，根场景：" + rootScene->GetName()
                     + (rootScene->IsDynamic() ? "（动态）" : "（静态）"));

    runLoop(*rootScene, s);

    INK_LOG_INFO(kModuleName, "主循环退出，释放窗口：" + rootScene->GetName());
    releaseWindow(s);
}

}  // namespace ink
