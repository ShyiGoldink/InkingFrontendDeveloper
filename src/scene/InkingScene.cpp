#include <scene/InkingScene.h>

#include <config/window_config.h>

namespace ink {

namespace {

/**
 * 场景根矩形的初始数据。
 *
 * 场景不做布局变化，但第一次渲染总要有对齐的锚点：这里先按设计画布
 * 定型（自身左上角对上窗口左上角），之后没有运行期位置写入口。
 * 将来由 CXXCSS 配置（CXXCSS/Scene/<场景名>.json）驱动时，
 * 改的就是这个函数，构造函数不用动。
 */
AnchorData sceneRootAnchorData() {
    AnchorData data;
    data.width  = inking::kDesignWidth;
    data.height = inking::kDesignHeight;
    return data;
}

}  // namespace

// ---------------------------------------------------------------------------
// 构造 / 析构
// ---------------------------------------------------------------------------

InkingScene::InkingScene()
    : InkingStaticAnchor(nullptr, sceneRootAnchorData()) {
    registerToSceneLibrary();
}

InkingScene::InkingScene(const std::string& sceneName)
    : InkingStaticAnchor(nullptr, sceneRootAnchorData()), _sceneName(sceneName) {
    registerToSceneLibrary();
}

InkingScene::~InkingScene() = default;

// ---------------------------------------------------------------------------
// 登记
// ---------------------------------------------------------------------------

void InkingScene::registerToSceneLibrary() {
    // 构造即在令牌里完成登记，析构即在令牌里完成注销：
    // 调用方永远不会忘记写注销，这正是 RAII 的用意。
    _sceneRegisterToken.emplace(_sceneName, this);
}

// ---------------------------------------------------------------------------
// 查询
// ---------------------------------------------------------------------------

const std::string& InkingScene::GetSceneName() const noexcept {
    return _sceneName;
}

bool InkingScene::IsRegistered() const noexcept {
    // 有令牌**并且**令牌真的登记了才算数：空名字的场景也有令牌，
    // 但那个令牌是空转的（见 RegisterToken::_ready）。
    return _sceneRegisterToken.has_value() && _sceneRegisterToken->IsRegistered();
}

}  // namespace ink
