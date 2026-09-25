#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ink {

class Scene;

// 场景库：名字 → 场景。
//
// 每个 Scene 构造时自动登记、析构时自动注销，所以窗口层只要一句
// InkingWindow::Show("头部菜单") 就能知道从哪个场景开始展示。
// 重名时后来的覆盖先来的，注销时只清掉"确实是自己"的那条。
class SceneRegistry {
public:
    /// 按名字找场景，找不到返回 nullptr。
    static Scene* find(const std::string& name);
    /// 最后登记的场景；Show() 没给名字时就用它。
    static Scene* latest();
    /// 当前登记的名单（顺序不保证）。
    static std::vector<std::string> names();
    static std::size_t size();

private:
    static void registerScene(Scene& scene);
    static void unregisterScene(Scene& scene);
    friend class Scene;
};

}  // namespace ink
