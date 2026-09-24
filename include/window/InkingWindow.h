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

    void Show(const std::string& sceneName = "");

private:
    InkingWindow();
    ~InkingWindow();

    int _width = inking::kDesignWidth;
    int _height = inking::kDesignHeight;
};

}  // namespace ink
