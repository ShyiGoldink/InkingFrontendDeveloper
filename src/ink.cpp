#include <ink/ink.h>

// 几个组件的公开头在这里被 include 一次，理由见 docs/CodeStyleRule.md §2.4：
// 新公开头如果不被任何 .cpp 或自检 include，它就是"永远编译不过的死文件"，
// 而构建照样全绿（这个坑踩过——include/scene/InkingScene.h 曾经藏了很久）。
#include <button/InkingStaticButton.h>

#include <SDL3/SDL.h>

#include <type_traits>

namespace ink {

// ---------------------------------------------------------------------------
// 编译期把两条设计约束钉住，别让后来的改动悄悄把它们拆掉
//
// 1. **静态组件没有几何写入口**（InkingAnchor.h 末尾那段）。这不是"约定没写"，
//    是类型上的硬保证：连命中表里存的 InkingAnchor* 都改不了几何。
// 2. **按钮是静态组件**。三态只换颜色 → 不标脏 → 能进命中表；哪天有人给
//    Button 加了 Resize，下面那条断言会当场把构建打红，而不是等到
//    "画的是圆角、点到的是直角"才发现。
//
// 检测写法说明：不能直接写 `requires(Button& b) { b.Resize(1,1); }`。
// 那个表达式里没有模板参数，是**非依赖**的，成员不存在时不会走"约束不满足"
// 这条替换失败路径，而是当场报"no member named Resize"——编译照样红，
// 但报错信息指向断言本身而不是"有人加了写入口"。所以按标准 detection idiom
// 做成模板：不满足约束时重载被丢弃（替换失败不是错误），
// HasResize<Button> 直接是 false，断言才真的在测"没有"。
// ---------------------------------------------------------------------------

template <typename T>
concept HasResize = requires(T& node) { node.Resize(1, 1); };

template <typename T>
concept HasChangeSelfAnchor =
    requires(T& node) { node.ChangeSelfAnchor(Anchor{0.5f, 0.5f}); };

template <typename T>
concept HasChangeTraceAnchor =
    requires(T& node) { node.ChangeTraceAnchor(Anchor{0.5f, 0.5f}); };

template <typename T>
concept HasChangeOffset =
    requires(T& node) { node.ChangeOffset(1.0f, 1.0f); };

template <typename T>
concept HasSetVisible = requires(T& node) { node.SetVisible(true); };

template <typename T>
concept HasChangeZIndex = requires(T& node) { node.ChangeZIndex(1); };

static_assert(std::is_base_of_v<InkingStaticAnchor, InkingStaticButton>,
              "Button 是静态组件：三态只换颜色，几何构造即定型");
static_assert(!std::is_base_of_v<InkingDynamicAnchor, InkingStaticButton>,
              "Button 不该同时是动态组件：静态 / 动态是二选一，不能回头改");

static_assert(!HasResize<InkingStaticButton>, "静态组件不该有 Resize");
static_assert(!HasChangeSelfAnchor<InkingStaticButton>,
              "静态组件不该有 ChangeSelfAnchor");
static_assert(!HasChangeTraceAnchor<InkingStaticButton>,
              "静态组件不该有 ChangeTraceAnchor");
static_assert(!HasChangeOffset<InkingStaticButton>,
              "静态组件不该有 ChangeOffset");

// 反面：骨架变化两版共用，静态组件必须有，别在"去掉几何写入口"时连它们一起删了。
static_assert(HasSetVisible<InkingStaticButton>,
              "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingStaticButton>,
              "层级是骨架变化，两版都该有");

std::string sdl3_version() {
    // SDL3 把版本编码成一个整数返回（SDL2 是出参形式的 SDL_version）。
    const int version = SDL_GetVersion();
    return std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MINOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MICRO(version));
}

}  // namespace ink

