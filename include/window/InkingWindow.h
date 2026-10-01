#pragma once

#include <config/window_config.h>
#include <ink/basic/InkingAnchor.h>
#include <input/MouseInput.h>
#include <string>

namespace ink {

/**
 * 窗口：整个项目的根锚点，所以它继承**静态锚点**。
 *
 * 两套尺寸要分清：
 *   - 基类那对（GetWidth / GetHeight）是"根锚点的尺寸"，也就是设计画布
 *     大小（kDesignWidth × kDesignHeight），编译期定型、构造之后不动；
 *   - 自己那对（GetWindowWidth / GetWindowHeight）是**窗口像素尺寸**，
 *     运行期可调。
 *
 * 窗口怎么拉都不改设计坐标里的几何：设计空间固定，靠 letterbox 做整幅
 * 等比换算，换算倍率由窗口层通过基类的 SetMagnification 写进来（不标脏）。
 */
class InkingWindow : public InkingStaticAnchor {
public:
    static InkingWindow& Instance();

    InkingWindow(const InkingWindow&) = delete;
    InkingWindow& operator=(const InkingWindow&) = delete;

    /// 窗口像素尺寸（运行期属性）。注意与基类的 GetWidth / GetHeight 区分。
    int GetWindowWidth() const noexcept;
    int GetWindowHeight() const noexcept;
    bool setWindowWidth(int width);
    bool setWindowHeight(int height);

    void Show(const std::string& sceneName = "");

private:
    InkingWindow();
    ~InkingWindow();

    int _windowWidth  = inking::kDesignWidth;
    int _windowHeight = inking::kDesignHeight;

    MouseInput _mouseInput;
};

}  // namespace ink
