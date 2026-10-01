#pragma once

// RAII 场景登记令牌，对应后端 InkingBackendFramework 的
// basic/StatusRegisterToken.h。
//
// 后端的写法是"构造即登记、析构即注销"，让调用方不可能忘记写注销：
// 令牌是场景的成员，场景析构时令牌一定先析构（成员先于基类、也先于
// 本翻译单元的其它静态量被销毁），所以注销不会漏、也不会晚。
//
// 本项目的令牌被**模板化**了：后端只有 ShineBasicModule 一种被登记类型，
// 所以它的令牌写死了指针类型；这里的被登记类型将来不止场景一种
// （控件、面板……），把类型做成模板就能一个令牌管到底，不必每来一种
// 类型就抄一遍完全相同的 RAII 代码。此处以 SceneLibrary 为库参数。

#include <string>
#include <utility>

#include <scene/SceneLibrary.h>

namespace ink {

/**
 * @brief 基于 RAII 思想的登记令牌：构造时把对象登记进库，析构时自动注销。
 *
 * 用法（见 InkingScene.h）：把它放在被登记类里当成员，
 * 在构造函数末尾 emplace，之后什么都不用管。
 *
 * 可移动、不可拷贝：令牌必须和"被登记的那个对象"一一对应，允许拷贝就会
 * 出现两份令牌抢着注销同一个登记，或者一份令牌提前把登记带走。
 */
template <typename Registered, typename Library>
class RegisterToken {
public:
    /** 构造即登记：传入登记用的名字与被登记的对象的指针。 */
    RegisterToken(std::string name, Registered* object)
        : _name(std::move(name)), _object(object) {
        // 不需要登记的情况有两类，统一收在这里，省得每个使用者各判一次：
        // 对象为空（没东西可登记），或者名字为空（登记进去也查不到，
        // 只会让 GetSceneName 变成空 key 那种谁也找不着的条目）。
        if (_ready()) {
            Library::RegisterScene(_name, _object);
        }
    }

    ~RegisterToken() noexcept {
        if (_ready()) {
            // 注销要能容忍"库已经先退出"：库内部用的是函数内静态量，
            // 谁先登记就把库先拉起来，所以正常路径下库一定还在。
            Library::UnregisterScene(_name, _object);
        }
    }

    // 禁止拷贝：一份登记只能有一个负责人。
    RegisterToken(const RegisterToken&) = delete;
    RegisterToken& operator=(const RegisterToken&) = delete;

    /**
     * 移动构造：把登记的所有权搬走，源对象置空。
     *
     * 注意这里**不能**先注销再登记：搬移语义是"同一条登记换个负责人"，
     * 中间那次注销会让别的线程（或库里恰好触发的枚举）看到一瞬间的空档。
     * 所以只搬指针，登记本身不动。
     */
    RegisterToken(RegisterToken&& other) noexcept
        : _name(std::move(other._name)), _object(other._object) {
        other._object = nullptr;
    }

    RegisterToken& operator=(RegisterToken&& other) noexcept {
        if (this != &other) {
            // 先把自己手上那份注销掉，再接别人的——否则这条登记就没人管了。
            if (_ready()) {
                Library::UnregisterScene(_name, _object);
            }
            _name = std::move(other._name);
            _object = other._object;
            other._object = nullptr;
        }
        return *this;
    }

    /** 登记用的名字（场景名）。 */
    const std::string& GetName() const noexcept { return _name; }

    /** 被登记的对象的指针；已经搬走时为 nullptr。 */
    Registered* GetObject() const noexcept { return _object; }

    /** 这次登记是否真的生效（对象与名字都非空）。 */
    bool IsRegistered() const noexcept { return _ready(); }

private:
    /** 这次令牌是否代表一条真实存在的登记：对象和名字都得有。 */
    bool _ready() const noexcept { return _object != nullptr && !_name.empty(); }

    std::string _name;             /** 登记用的名字（场景名） */
    Registered* _object = nullptr; /** 非拥有关系；搬走之后为 nullptr */
};

}  // namespace ink
