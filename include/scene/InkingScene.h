//InkingScene是场景的基类
//类似于QWidget，用户创建场景需要通过继承的方式来具体实例化内部的场景组件
//由于InkingScene是静态的，所以InkingScene本身的大小不能自己改变，只能跟随Window的大小改变
//想使用能随意改变大小的组件请参考InkingDynamicWidget

#pragma once

#include <optional>
#include <string>

#include "ink/basic/InkingAnchor.h"
#include "scene/SceneLibrary.h"
#include "scene/SceneRegisterToken.h"

namespace ink {

/**
 * @brief 场景：window 之下最高的结点，继承**静态锚点**。
 *
 * 场景永远静态：没有位置与尺寸写入口，只负责"渲染 / 不渲染"，
 * 会动的东西压在 widget 层（见 docs/API.md「InkingScene」）。
 *
 * 构造即登记：构造函数末尾调 registerToSceneLibrary()，由 RAII 令牌把
 * this 登记进 SceneLibrary，析构时令牌自动注销——调用方不需要（也不应该）
 * 自己写注销，这正是后端 StatusRegisterToken 那套写法的用意。
 *
 * 所以 `_sceneName` 必须**先于**令牌构造好：成员按声明顺序构造、逆序析构，
 * 这里的声明顺序因此是 `_sceneName` → `_sceneRegisterToken`，析构时令牌
 * 先走、场景名后走，注销用的 key 在整个析构过程中都是有效的。
 *
 * 登记与注销都只在 UI 线程发生：窗口主循环那一条线程负责场景的构造与析构。
 * SceneLibrary 因此不加锁（见该头文件里的说明）。
 */
class InkingScene : public InkingStaticAnchor {
public:
    /**
     * 场景的 RAII 登记令牌：绑定"场景类 → SceneLibrary"。
     * 类型别名让派生类不用关心模板参数。
     */
    using SceneToken = ink::RegisterToken<InkingScene, SceneLibrary>;

    /** 运行时构造：不绑定场景名，不登记进场景库。 */
    InkingScene();

    /**
     * 配置驱动：传入 sceneName 作为场景名，并登记进场景库。
     *
     * 推荐用 k 常量命名，编译期就能排查问题（见 docs/API.md）。
     * 场景的尺寸与锚点在构造时定下，之后不挪动。
     */
    explicit InkingScene(const std::string& sceneName);
    /**
     * InkingScene的另一种构造方式
     * 如果不想要用CXXCSS进行初始化，可以选择直接传入场景必须要的参数进行初始化
     * 一般用在小场景的情况下
     * 但是考虑到一般使用场景，Scene一般都需要和window的大小对齐，否则可能会有奇怪的黑边
     * 但是依然要提供这样的一个方法
     */
    explicit InkingScene(const std::string &sceneName,const AnchorData& anchotData);

    virtual ~InkingScene();

    // 禁止拷贝构造和赋值操作：场景持有父指针、还要登记进场景库，
    // 复制出的第二份关系会让父子链和场景库同时指向同一个对象。
    InkingScene(const InkingScene&) = delete;
    InkingScene& operator=(const InkingScene&) = delete;
    InkingScene(InkingScene&&) = delete;
    InkingScene& operator=(InkingScene&&) = delete;

    /** 场景名；构造时定下，没有写入口。 */
    const std::string& GetSceneName() const noexcept;

    /** 这个场景当前是否登记在场景库里（令牌还在自己手上）。 */
    bool IsRegistered() const noexcept;

protected:
    /**
     * 把 this 登记进 SceneLibrary。
     *
     * 与后端 ShineBasicModule::registerToStatusChecker() 同一套写法：
     * 在**构造函数末尾**调用，表示自身已经构造完成、可以登记了。
     * 本项目里两个构造函数都已经调过，派生类构造函数末尾再调一次也没关系——
     * 令牌是 std::optional，重复 emplace 会先析构旧令牌（注销旧登记）
     * 再构造新令牌（登记新名字），不会留下两份登记。
     */
    void registerToSceneLibrary();

    /** 场景名，需要在构造时直接构造好（令牌要用它当登记用的 key）。 */
    std::string _sceneName;

    /** 延迟构造的登记令牌：登记发生在构造函数体内，不在初始化列表里。 */
    std::optional<SceneToken> _sceneRegisterToken;

    /**render，渲染开关，作为私有属性，不允许外部调用。
     * 在注册到SceneLibrary时同时作为回调函数注册到Library中
     * 只有InkingWindow可以通过SceneLibrary调用
    */
};

}  // namespace ink
