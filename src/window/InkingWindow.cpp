#include <window/InkingWindow.h>

#include <ink/basic/InkLog.h>

#include <SDL3/SDL.h>

#include <string>

namespace ink {

namespace {

constexpr const char* kModuleName = "InkingWindow";

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
}

}  // namespace ink

