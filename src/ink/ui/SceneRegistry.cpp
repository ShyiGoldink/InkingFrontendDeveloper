#include <ink/ui/SceneRegistry.h>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include <ink/ui/Scene.h>

namespace ink {

namespace {

/// 场景库的真实存储：一张名字表 + 一条登记顺序栈。
struct Registry {
    std::unordered_map<std::string, Scene*> byName;
    std::vector<Scene*>                     order;  ///< 登记顺序，latest() 取最后一个
};

Registry& registry() {
    static Registry instance;
    return instance;
}

}  // namespace

void SceneRegistry::registerScene(Scene& scene) {
    Registry& r = registry();
    r.byName[scene.GetName()] = &scene;  // 重名时后来的覆盖先来的
    r.order.push_back(&scene);
}

void SceneRegistry::unregisterScene(Scene& scene) {
    Registry& r = registry();

    const auto found = r.byName.find(scene.GetName());
    if (found != r.byName.end() && found->second == &scene) {
        r.byName.erase(found);
    }

    r.order.erase(std::remove(r.order.begin(), r.order.end(), &scene), r.order.end());
}

Scene* SceneRegistry::find(const std::string& name) {
    const Registry& r = registry();
    const auto found = r.byName.find(name);
    return found == r.byName.end() ? nullptr : found->second;
}

Scene* SceneRegistry::latest() {
    const Registry& r = registry();
    return r.order.empty() ? nullptr : r.order.back();
}

std::vector<std::string> SceneRegistry::names() {
    const Registry& r = registry();
    std::vector<std::string> result;
    result.reserve(r.byName.size());
    for (const auto& item : r.byName) {
        result.push_back(item.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::size_t SceneRegistry::size() {
    return registry().byName.size();
}

}  // namespace ink
