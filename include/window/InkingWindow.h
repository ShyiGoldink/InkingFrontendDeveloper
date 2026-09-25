#pragma once

#include <string>
#include <config/window_config.h>

namespace ink {

class InkingWindow {
public:
    static InkingWindow& Instance();

    InkingWindow(const InkingWindow&) = delete;
    InkingWindow& operator=(const InkingWindow&) = delete;

    int GetWidth() const noexcept;
    int GetHeight() const noexcept;
    bool setWidth(int width);
    bool setHeight(int height);

    /// 无边框窗口：头部菜单自己当标题栏时用，要在 Show() 之前设置。
    bool GetBorderless() const noexcept;
    void setBorderless(bool borderless);

    /// 打开窗口并进入主循环，直到窗口关闭（或按 ESC）。
    /// sceneName 为空时用场景库里最后登记的那个场景。
    void Show(const std::string& sceneName = "");

private:
    InkingWindow();
    ~InkingWindow();

    int _width = inking::kDesignWidth;
    int _height = inking::kDesignHeight;
    bool _borderless = false;
};

}  // namespace ink
