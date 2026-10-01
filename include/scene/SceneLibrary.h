#pragma once

// 场景库：登记与查找场景的静态类，对应后端 InkingBackendFramework 的
// basic/ShineStatusChecker（静态类 + moduleName → 指针列表）。
//
// 结构对应关系：
//   ShineStatusChecker   → ink::SceneLibrary          （静态注册表）
//   ShineBasicModule     → ink::InkingScene           （被登记的基类）
//   StatusRegisterToken  → ink::SceneRegisterToken    （RAII 令牌）
//
// 在本项目里，场景就是"被登记的模块"：Scene 构造时（经 RAII 令牌）把自己
// 按场景名登记进来，窗口层 / 生成器据此把名字换成 InkingScene*，全程不用
// 认识具体场景类型。详见 docs/API.md「InkingScene」章节。
//
// 与后端不同的两处刻意改动（都是为了绕开后端那两个真实的坑）：
//
//   1. **存储用函数内静态量，不用类的静态数据成员。**
//      类的静态数据成员在 main 之前按翻译单元顺序初始化，而静态场景对象
//      可能先构造，于是会出现"场景活了、库还没起来"或者更糟的
//      "场景在库已经析构之后才注销"——静态初始化 / 析构顺序问题。
//      函数内静态量首次使用时才初始化，谁先登记就把库先拉起来，
//      库一定活得比登记者久。
//
//   2. **不加锁，只允许 UI 线程登记与注销。**
//      后端有锁是因为模块会被线程池里的多个管家线程读写；本项目的场景层
//      只在 UI 线程构造 / 析构（窗口主循环那一条线程），加锁既挡不住误用，
//      又白付一次原子操作。真出现跨线程场景，再加锁并把这条注释删掉。

#include <cstddef>
#include <string>
#include <vector>

namespace ink {

class InkingScene;

/**
 * @brief 场景库：登记与查找场景指针的静态类。
 *
 * 一个场景名可以对应多个场景实例（同期只有一个场景可渲染，见
 * docs/API.md「InkingScene」），所以内部是 name → 指针列表，
 * 和后端 ShineStatusChecker 的结构保持一致。
 */
class SceneLibrary {
public:
    /** 登记一个场景指针；指针为空会被忽略，同一指针重复登记只算一次。 */
    static void RegisterScene(const std::string& sceneName, InkingScene* scene);

    /** 注销一个场景指针；该名字下没有实例之后会把这个名字一并移除。 */
    static void UnregisterScene(const std::string& sceneName, InkingScene* scene);

    /** 按名字取该名字下的全部场景实例；没有就返回空 vector。 */
    static std::vector<InkingScene*> GetScenes(const std::string& sceneName);

    /** 取当前登记过的所有场景名，主要用于自检与枚举。 */
    static std::vector<std::string> GetAllSceneNames();

    /** 当前登记的场景实例总数，主要给自检用。 */
    static std::size_t SceneCount();

private:
    SceneLibrary() = delete;
    ~SceneLibrary() = delete;
};

}  // namespace ink
