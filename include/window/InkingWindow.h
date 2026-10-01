#pragma once

#include <config/window_config.h>
#include <ink/basic/InkingAnchor.h>
#include <input/MouseInput.h>
#include <string>

namespace ink {

// 只做前向声明：公开头里不拖场景实现进来。
class InkingScene;

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
 *
 * 注意窗口**不是**渲染树的根——渲染树以场景为根（docs/API.md）。窗口只负责
 * 开出来、把事件泵进 MouseInput、每帧把展示倍率刷一遍，然后把当前场景的
 * 绘制列表交给渲染器。
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

    /**
     * 当前正在展示（渲染）的场景；没有时为 nullptr。
     *
     * 活跃态由 `SceneLibrary` 统一维护，窗口**不缓存**场景指针——
     * 缓存了就得在场景关闭 / 析构时回头清掉，而现查一次更省心，
     * 也不会留下指向已销毁场景的野指针。
     */
    InkingScene* GetCurrentScene() const;

    void Show(const std::string& sceneName = "");

private:
    InkingWindow();
    ~InkingWindow();

    int _windowWidth  = inking::kDesignWidth;
    int _windowHeight = inking::kDesignHeight;

    MouseInput _mouseInput;
};

/**
 * 上一帧真的渲染了哪个场景；没有场景时为 nullptr。
 *
 * 纯自检用的观察点：`Show()` 返回时窗口已经释放、场景名也被清掉，
 * 所以"窗口确实按名字解析到了场景并把它渲染出来了"只能靠这个记录来验。
 * 窗口从没跑过主循环时也是 nullptr。
 */
InkingScene* GetLastRenderedScene() noexcept;

}  // namespace ink
