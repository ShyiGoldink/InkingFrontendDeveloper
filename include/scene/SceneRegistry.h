#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace ink {

class InkingScene;

/**
 * 场景库：名字 → 场景。
 *
 * 每个场景构造时自动登记、析构时自动注销，所以窗口层只要一句
 * InkingWindow::Show("菜单栏") 就能知道从哪个场景开始展示（docs/API.md）。
 * 重名时后来的覆盖先来的，注销时只清掉「确实是自己」的那条。
 */
class SceneRegistry {
public:
    /// 按名字找场景，找不到返回 nullptr。
    static InkingScene* Find(const std::string& name);
    /// 最后登记的场景；Show() 没给名字时就用它。
    static InkingScene* Latest();
    /// 当前登记的名单（按名字排序）。
    static std::vector<std::string> Names();
    static std::size_t Size();

private:
    static void Register(InkingScene& scene);
    static void Unregister(InkingScene& scene);
    friend class InkingScene;
};

}  // namespace ink
