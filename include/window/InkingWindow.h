#pragma once

#include <config/window_config.h>
#include <input/MouseInput.h>
#include <string>

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

    void Show(const std::string& sceneName = "");

private:
    InkingWindow();
    ~InkingWindow();

    int _width = inking::kDesignWidth;
    int _height = inking::kDesignHeight;

    MouseInput _mouseInput;
};

}  // namespace ink
