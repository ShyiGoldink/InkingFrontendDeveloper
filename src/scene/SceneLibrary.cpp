#include <scene/SceneLibrary.h>

#include <unordered_map>
#include <utility>

#include <SDL3/SDL.h>

#include <scene/InkingScene.h>

namespace ink {

namespace {

/** name → 该名字占用的场景。一对一，这是"同名同期只能有一个"的落点。 */
using SceneMap = std::unordered_map<std::string, InkingScene*>;

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

/** 当前活跃（正在渲染）的场景；谁都不活跃时为 nullptr。 */
InkingScene*& activeScene() {
    static InkingScene* scene = nullptr;
    return scene;
}

}  // namespace

// ---------------------------------------------------------------------------
// 登记 / 注销
// ---------------------------------------------------------------------------

SceneLibrary::RegisterResult SceneLibrary::RegisterScene(
    const std::string& sceneName, InkingScene* scene) {
    if (scene == nullptr || sceneName.empty()) {
        return RegisterResult::Invalid;
    }

    SceneMap& table = scenes();
    if (table.find(sceneName) != table.end()) {
        // 名字被占了就驳回：不覆盖、不排队。
        // 后来者照样构造出来了，但它没进库，所以永远不会被播放。
        return RegisterResult::NameTaken;
    }

    table.emplace(sceneName, scene);
    return RegisterResult::Ok;
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

    // 只摘自己的那一份：名字下已经换成别的场景时什么都不做，
    // 否则会把别人的登记误删掉（换名字重注册那条路上就会碰到）。
    if (it->second != scene) {
        return;
    }

    table.erase(it);

    // 顺手把活跃指针也摘掉，避免它指向一个已经退出登记的场景。
    if (activeScene() == scene) {
        activeScene() = nullptr;
    }
}

InkingScene* SceneLibrary::FindScene(const std::string& sceneName) {
    const SceneMap& table = scenes();
    const auto it = table.find(sceneName);
    return it != table.end() ? it->second : nullptr;
}

bool SceneLibrary::CloseScene(const std::string& sceneName) {
    InkingScene* scene = FindScene(sceneName);
    if (scene == nullptr) {
        return false;
    }

    // 先通知场景自己"被关掉了"，再从库里摘掉：
    // 反过来的话，场景那边还得反查一次自己的名字。
    scene->MarkClosed();
    UnregisterScene(sceneName, scene);
    return true;
}

// ---------------------------------------------------------------------------
// 活跃态
// ---------------------------------------------------------------------------

void SceneLibrary::SetActiveScene(InkingScene* scene) {
    InkingScene*& current = activeScene();
    if (current == scene) {
        return;
    }

    // 旧的自动进入静默：不渲染、跟着渲染的那组逻辑也不动。
    // 数据还在、能读，所以"先把数据拿完再切场景"这条路是通的。
    if (current != nullptr) {
        current->SetActiveFromLibrary(false);
    }

    current = scene;

    if (current != nullptr) {
        current->SetActiveFromLibrary(true);
    }
}

InkingScene* SceneLibrary::GetActiveScene() {
    return activeScene();
}

bool SceneLibrary::RenderScene(SDL_Renderer* renderer) {
    InkingScene* scene = activeScene();

    // 只渲染活跃场景：停放态（注册被驳回）、静默态（被顶掉的）、
    // 已经关闭的场景都不会走到这里。每个场景一帧只渲染一次，
    // 也是靠"活跃只有一个"这条保证的。
    if (scene == nullptr || renderer == nullptr) {
        return false;
    }

    scene->Render(renderer);
    return true;
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

std::vector<std::string> SceneLibrary::GetAllSceneNames() {
    std::vector<std::string> names;
    names.reserve(scenes().size());
    for (const auto& pair : scenes()) {
        names.push_back(pair.first);
    }
    return names;
}

std::size_t SceneLibrary::SceneCount() {
    return scenes().size();
}

bool SceneLibrary::IsNameTaken(const std::string& sceneName) {
    return scenes().find(sceneName) != scenes().end();
}

}  // namespace ink
