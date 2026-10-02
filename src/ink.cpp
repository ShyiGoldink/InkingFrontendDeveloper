#include <ink/ink.h>

// 几个组件的公开头在这里被 include 一次，理由见 docs/CodeStyleRule.md §2.4：
// 新公开头如果不被任何 .cpp 或自检 include，它就是"永远编译不过的死文件"，
// 而构建照样全绿（这个坑踩过——include/scene/InkingScene.h 曾经藏了很久）。
#include <button/ButtonLook.h>
#include <button/InkingDynamicButton.h>
#include <button/InkingStaticButton.h>

#include <SDL3/SDL.h>

#include <type_traits>
#include <utility>

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

// ---------------------------------------------------------------------------
// 动态档：同一套断言镜像一份
//
// 动态按钮（InkingDynamicButton）与静态按钮是**同一个组件的两档**，
// 判据反过来：它必须有几何写入口（几何会自己变，那是它存在的理由），
// 而且两个档位二选一、不能同时是。
//
// 这几条不只是"描述现状"：哪天有人为了让静态按钮"按下时缩一下"而把
// InkingDynamicButton 的几何写入口搬进公共基类，上面的
// `!HasResize<InkingStaticButton>` 会当场把构建打红，而这里会说明
// 那条路本来该走哪个类型。
// ---------------------------------------------------------------------------

static_assert(std::is_base_of_v<InkingDynamicAnchor, InkingDynamicButton>,
              "动态按钮属于动态档：几何会自己变，所以不进命中表");
static_assert(!std::is_base_of_v<InkingStaticAnchor, InkingDynamicButton>,
              "静态 / 动态是二选一，不能既进表又不进表");

static_assert(HasResize<InkingDynamicButton>,
              "动态组件必须有 Resize（几何写入口是这一档存在的理由）");
static_assert(HasChangeSelfAnchor<InkingDynamicButton>,
              "动态组件必须有 ChangeSelfAnchor");
static_assert(HasChangeTraceAnchor<InkingDynamicButton>,
              "动态组件必须有 ChangeTraceAnchor");
static_assert(HasChangeOffset<InkingDynamicButton>,
              "动态组件必须有 ChangeOffset");
static_assert(HasSetVisible<InkingDynamicButton>,
              "可见性是骨架变化，两版都该有");
static_assert(HasChangeZIndex<InkingDynamicButton>,
              "层级是骨架变化，两版都该有");

// 两块按钮共用同一份三态外观与状态机（ButtonLook.h），不能各写一份：
// 两份实现各自演化就会出现"静态按钮按下去和动态按钮长得不一样"。
// 这里钉住"两边问出来的三态是同一个类型"——谁另起一套 ButtonState 都会红。
static_assert(std::is_same_v<decltype(std::declval<const InkingStaticButton&>()
                                          .GetState()),
                             ButtonState>,
              "静态按钮的三态来自共用的 ButtonState");
static_assert(std::is_same_v<decltype(std::declval<const InkingDynamicButton&>()
                                          .GetState()),
                             ButtonState>,
              "动态按钮的三态也来自共用的 ButtonState");

std::string sdl3_version() {
    // SDL3 把版本编码成一个整数返回（SDL2 是出参形式的 SDL_version）。
    const int version = SDL_GetVersion();
    return std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MINOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MICRO(version));
}

}  // namespace ink

