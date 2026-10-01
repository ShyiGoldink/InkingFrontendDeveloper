#include <scene/SceneLibrary.h>

#include <algorithm>
#include <unordered_map>
#include <utility>

#include <scene/InkingScene.h>

namespace ink {

namespace {

/** name → 该名字下的场景实例列表。与后端 _callbackPointers 同构。 */
using SceneMap = std::unordered_map<std::string, std::vector<InkingScene*>>;

/**
 * 唯一的登记表。
 *
 * 用函数内静态量而不是类的静态数据成员：它首次被使用时才初始化，
 * 所以一定晚于（也就是活得久于）第一个来登记的静态场景对象，
 * 不会踩静态初始化 / 析构顺序的坑（见头文件里的说明）。
 */
SceneMap& scenes() {
    static SceneMap instance;
    return instance;
}

}  // namespace

// ---------------------------------------------------------------------------
// 登记 / 注销
// ---------------------------------------------------------------------------

void SceneLibrary::RegisterScene(const std::string& sceneName,
                                 InkingScene* scene) {
    if (scene == nullptr) {
        return;  // 鲁棒设计：空指针不进库，省得后面每个使用者都要判空
    }

    std::vector<InkingScene*>& bucket = scenes()[sceneName];
    // 先查重：同一个指针登记两次只算一次，避免注销时留一份野指针。
    if (std::find(bucket.begin(), bucket.end(), scene) == bucket.end()) {
        bucket.push_back(scene);
    }
}

void SceneLibrary::UnregisterScene(const std::string& sceneName,
                                   InkingScene* scene) {
    if (scene == nullptr) {
        return;
    }

    SceneMap& table = scenes();
    const auto it = table.find(sceneName);
    if (it == table.end()) {
        return;
    }

    std::vector<InkingScene*>& bucket = it->second;
    bucket.erase(std::remove(bucket.begin(), bucket.end(), scene),
                 bucket.end());

    // 该名字下已经没有实例，就把名字本身也移掉，
    // 否则 GetAllSceneNames / SceneCount 会把空壳算进去。
    if (bucket.empty()) {
        table.erase(it);
    }
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

std::vector<InkingScene*> SceneLibrary::GetScenes(const std::string& sceneName) {
    const SceneMap& table = scenes();
    const auto it = table.find(sceneName);
    if (it != table.end()) {
        return it->second;  // 拷贝一份返回，别把内部存储暴露出去
    }
    return {};
}

std::vector<std::string> SceneLibrary::GetAllSceneNames() {
    std::vector<std::string> names;
    names.reserve(scenes().size());
    for (const auto& pair : scenes()) {
        names.push_back(pair.first);
    }
    return names;
}

std::size_t SceneLibrary::SceneCount() {
    std::size_t count = 0;
    for (const auto& pair : scenes()) {
        count += pair.second.size();
    }
    return count;
}

}  // namespace ink
