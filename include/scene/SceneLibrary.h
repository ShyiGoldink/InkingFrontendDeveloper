#pragma once

// 场景库：登记与查找场景的静态类，对应后端 InkingBackendFramework 的
// basic/ShineStatusChecker（静态类 + moduleName → 指针）。
//
// 结构对应关系：
//   ShineStatusChecker   → ink::SceneLibrary          （静态注册表）
//   ShineBasicModule     → ink::InkingScene           （被登记的基类）
//   StatusRegisterToken  → ink::SceneRegisterToken    （RAII 令牌）
//
// 与后端不同的地方，都是本项目自己的规矩：
//
//   1. **同名同期只能有一个**。名字被占了，后来者注册**当场驳回**——对象还是
//      构造出来了，但它永远不进库、永远不会被播放。想重新用这个名字，得先把
//      占用者关掉（CloseScene）。这条规矩让"按名字找场景"永远只有一个答案，
//      窗口层和生成器都不用再处理"同名多个怎么办"。
//
//   2. **场景有活跃态**。同一时刻最多一个场景是"正在渲染"的（SetActiveScene），
//      切场景时旧的自动进入静默。静默不等于销毁：数据还在、能读，
//      只是不渲染、跟着渲染的那组逻辑也不动。
//
//   3. **存储用函数内静态量，不用类的静态数据成员**。类的静态数据成员在 main
//      之前按翻译单元顺序初始化，而静态场景对象可能先构造，于是出现"场景在库
//      已经析构之后才注销"的静态初始化 / 析构顺序问题。函数内静态量首次使用时
//      才初始化，谁先登记就把库先拉起来，库一定活得比登记者久。
//
//   4. **不加锁，只允许 UI 线程登记与注销**。后端有锁是因为模块会被线程池里的
//      多个管家线程读写；本项目的场景层只在 UI 线程构造 / 析构（窗口主循环那
//      一条线程），加锁既挡不住误用，又白付一次原子操作。
//
// 详见 docs/API.md「SceneLibrary」章节。

#include <cstddef>
#include <string>
#include <vector>

// 只做前向声明：公开头里不拖 SDL 进来。
struct SDL_Renderer;

namespace ink {

class InkingScene;

/**
 * @brief 场景库：登记与查找场景指针的静态类。
 *
 * 名字 → 场景，一对一。注册是"抢占式"的：谁先注册谁占住这个名字。
 */
class SceneLibrary {
public:
    /** 注册的结果。失败原因分得清，才能给出有用的日志。 */
    enum class RegisterResult {
        Ok,         /**< 注册成功 */
        NameTaken,  /**< 同名场景已在库中——驳回，后来者永不播放 */
        Invalid,    /**< 名字为空或指针为空 */
    };

    /**
     * 登记一个场景。**同名已存在时直接驳回**，不覆盖、不排队。
     *
     * @return 注册结果；只有 Ok 表示真的进库了。
     */
    static RegisterResult RegisterScene(const std::string& sceneName,
                                        InkingScene* scene);

    /**
     * 注销一个场景。
     *
     * 只在该名字当前正指向这个场景时才真的摘掉（指针不匹配就什么都不做），
     * 所以"库里已经换人了"这种情况不会误删别人的登记。
     */
    static void UnregisterScene(const std::string& sceneName, InkingScene* scene);

    /** 按名字找场景；没注册过返回 nullptr。 */
    static InkingScene* FindScene(const std::string& sceneName);

    /**
     * 关掉一个场景：从库里摘掉，并把它标记为已关闭。
     *
     * 场景对象本身不归库所有，**不会被销毁**——调用方自己管生命周期。
     * 关掉之后这个名字空出来，可以再建一个新的同名场景。
     * 这也是"避免野指针"的正路：库先放手，调用方再决定什么时候析构。
     *
     * @return 真的关掉了一个才返回 true。
     */
    static bool CloseScene(const std::string& sceneName);

    /** 把某个场景设为当前活跃（正在渲染）的那个；传 nullptr 表示谁都不活跃。 */
    static void SetActiveScene(InkingScene* scene);

    /** 当前活跃（正在渲染）的场景；没有时返回 nullptr。 */
    static InkingScene* GetActiveScene();

    /**
     * 渲染当前活跃场景。**窗口层每帧调这个**，不要直接去碰场景的 `Render`。
     *
     * `InkingScene::Render` 是 protected，只有框架该调；从库这里走一道，
     * "只有活跃场景会被渲染、每个场景一帧只渲染一次"这条规矩就有了唯一落点。
     * 将来上合批（DrawCall 合批 + 静态烘焙）时，提交层也正好落在这里。
     *
     * @return 真的画了才返回 true（没有活跃场景时为 false）。
     */
    static bool RenderScene(SDL_Renderer* renderer);

    /** 取当前登记过的所有场景名，主要用于自检与枚举。 */
    static std::vector<std::string> GetAllSceneNames();

    /** 当前库里有多少个场景（一个名字算一个）。 */
    static std::size_t SceneCount();

    /** 某个名字是否已经被占。 */
    static bool IsNameTaken(const std::string& sceneName);

private:
    SceneLibrary() = delete;
    ~SceneLibrary() = delete;
};

}  // namespace ink
