#include <window/InkingWindow.h>

#include <core/RedrawScheduler.h>
#include <ink/basic/InkLog.h>
#include <ink/ui/MessageQueue.h>
#include <scene/InkingScene.h>
#include <scene/InputRouter.h>
#include <scene/SceneRegistry.h>
#include "SdlCanvas.h"

#include <SDL3/SDL.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <thread>

namespace ink {

namespace {

constexpr const char* kModuleName = "InkingWindow";

/// 帧节流：vsync 还没接上，先用固定节拍（16ms ≈ 60Hz）。
constexpr std::chrono::microseconds kFrameInterval{16000};

/// 无头冒烟：设了环境变量 INK_AUTOQUIT 就跑这么久然后自己退出。
constexpr std::chrono::milliseconds kAutoQuitAfter{2000};

/// 占位底色，和 basic 示例保持一致；样式层接手后换掉。
constexpr Color kBackground{18, 18, 24, 255};

/**
 * 窗口的运行期状态。
 *
 * 放在实现文件的匿名命名空间里，是为了让公开头文件保持「零 SDL 依赖」：
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
void toDesign(SDL_Renderer* renderer, float windowX, float windowY, float& designX,
              float& designY) {
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

/**
 * 帧转储（开发用）：把当前这一帧的像素存成 BMP。
 *
 * 只在设了环境变量 `INK_DUMPFRAME=<路径>` 时触发一次，用来在没有显示器的
 * 环境（CI、远程会话）确认布局真的画对了；平时一次都不调用。
 * 形状层与样式层落地后，这段和画布一起换成真正的抓帧工具。
 */
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
 * 事件泵：把这一帧攒下的 SDL 事件吃干净，变成 MouseInput 里的状态，
 * 顺便把点击投给场景。返回 false 表示该退出主循环了。
 *
 * 移动只「记状态 + 标脏」，不在这里判命中——命中与投递留给每帧一次的
 * isDirty + query（docs/InputDesign.md §5）。
 */
bool pumpEvents(WindowState& s, MouseInput& mouse, InputRouter& router) {
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
                mouse.UpdateFromSDL(event);
                router.MarkMouseMoved();  // 鼠标移动 → makeDirty
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP: {
                mouse.UpdateFromSDL(event);
                float x = 0.0f;
                float y = 0.0f;
                toDesign(s.renderer, event.button.x, event.button.y, x, y);
                // 点击不走脏标记，单独查一次。
                if (event.button.down) {
                    router.PointerDown(x, y, toButton(event.button.button));
                } else {
                    router.PointerUp(x, y, toButton(event.button.button));
                }
                INK_LOG_DEBUG(kModuleName,
                              std::string("鼠标") + (event.button.down ? "按下 " : "抬起 ")
                                  + std::to_string(static_cast<int>(event.button.button))
                                  + " @设计 (" + std::to_string(static_cast<int>(x)) + ", "
                                  + std::to_string(static_cast<int>(y)) + ")");
                break;
            }

            default:
                break;
        }
    }

    // 设计坐标一帧只换算一次：一帧里收到多少个移动事件都只算一次，
    // 和「每帧只判断一次」同拍。
    const MousePosition& windowPoint = mouse.GetState().position;
    float designX = 0.0f;
    float designY = 0.0f;
    toDesign(s.renderer, static_cast<float>(windowPoint.x),
             static_cast<float>(windowPoint.y), designX, designY);
    mouse.SetDesignPosition(designX, designY);
    if (mouse.IsMoved()) {
        router.MarkMouseMoved();
    }

    return running;
}

/**
 * 主循环：事件泵 → 设计坐标换算 → 输入（isDirty → query）→ 每帧更新 →
 * 重绘（独立的一路）→ 帧末收尾 → 节流。
 *
 * 三件事各自独立，正好是这次原型要验的：
 *   · 静态层：表在 Bake() 里烘一次，之后每帧只查表、只走平铺绘制表；
 *   · 输入：isDirty 时每帧一次 query，干净时 0 次（帧计数尾巴收尾）；
 *   · 重绘：需要重绘的属性调 makeDirty()，静止时整帧不画也不 present。
 */
void runLoop(WindowState& s, MouseInput& mouse, InkingScene& rootScene) {
    SdlCanvas canvas(s.renderer);

    InputRouter router;
    router.Attach(rootScene);

    // 静态场景先烘一遍：之后每帧只查表，不再遍历树。
    if (rootScene.NeedsBake()) {
        rootScene.Bake();
    }

    const bool autoQuit = SDL_getenv("INK_AUTOQUIT") != nullptr;
    const char* dumpPath = SDL_getenv("INK_DUMPFRAME");
    bool dumped = false;
    const auto start = std::chrono::steady_clock::now();
    auto previous = start;
    bool running = true;

    while (running) {
        // 1. 事件泵：退出、键盘、鼠标都在这里变成状态（点击顺手投出去）。
        running = pumpEvents(s, mouse, router);

        // 2. 静态表被改过就重烘：一帧最多付一次。
        if (rootScene.NeedsBake()) {
            rootScene.Bake();
        }

        // 3. 输入：脏就 query 一次（hover），帧计数收尾。
        const MousePoint& point = mouse.GetState().design;
        router.BeginFrame(point.x, point.y);

        // 4. 每帧更新：静态子树收不到，只有动态子树被叫醒。
        const auto now = std::chrono::steady_clock::now();
        const float deltaSeconds = std::chrono::duration<float>(now - previous).count();
        previous = now;
        rootScene.Tick(deltaSeconds);

        // 5. 重绘：独立的一路，静止时整帧不画。
        if (RedrawScheduler::BeginFrame()) {
            canvas.clear(kBackground);
            rootScene.Draw(canvas);
            if (dumpPath != nullptr && !dumped) {
                // 转储要在 present 之前：present 之后后台缓冲的内容就不保证还在了。
                dumped = true;  // 只抓第一帧真实画出来的画面
                dumpFrame(s.renderer, dumpPath);
            }
            canvas.present();
        }
        RedrawScheduler::EndFrame();

        // 6. 帧末收尾：瞬时状态一帧只清一次。
        mouse.ResetFrameFlags();

        flushMessages();

        // 7. 节流。
        std::this_thread::sleep_for(kFrameInterval);

        if (autoQuit
            && std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
                   > kAutoQuitAfter) {
            running = false;
        }
    }

    INK_LOG_INFO(kModuleName,
                 "主循环退出。静态表重烘 " + std::to_string(rootScene.BakeCount())
                     + " 次；hover query " + std::to_string(router.HoverQueries())
                     + " 次、点击 query " + std::to_string(router.ClickQueries())
                     + " 次、干净帧 " + std::to_string(router.IdleFrames())
                     + " 次；重绘 " + std::to_string(RedrawScheduler::PaintedFrames())
                     + " 帧、跳过 " + std::to_string(RedrawScheduler::SkippedFrames()) + " 帧");
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
        // Show() 会一直阻塞到窗口关闭，所以「已经打开」只能是重复调用。
        INK_LOG_WARN(kModuleName, "窗口已经打开，忽略这次 Show：" + sceneName);
        return;
    }

    // SDL3 的 SDL_Init 返回 bool：true 才是成功（SDL2 是「0 表示成功」）
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        INK_LOG_ERROR(kModuleName, std::string("SDL_Init 失败：") + SDL_GetError());
        return;
    }

    s.window = SDL_CreateWindow("InkingWindow", _width, _height,
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
        s.renderer, inking::kDesignWidth, inking::kDesignHeight,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);

    s.open = true;
    s.sceneName = sceneName;

    // 没给名字就用最后登记的那个场景——单页程序不必自己记场景名。
    InkingScene* rootScene =
        sceneName.empty() ? SceneRegistry::Latest() : SceneRegistry::Find(sceneName);
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

    // Show() 是启动点：窗口开出来之后就在这里进主循环，一直阻塞到窗口关闭
    // （或按 ESC），返回前把窗口释放掉。
    runLoop(s, _mouseInput, *rootScene);

    INK_LOG_INFO(kModuleName, "主循环退出，释放窗口：" + rootScene->GetName());
    releaseWindow(s);
}

}  // namespace ink
