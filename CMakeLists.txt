cmake_minimum_required(VERSION 3.25)

project(InkingFrontendDeveloper
    VERSION 0.1.0
    DESCRIPTION "Compile-time-polymorphic UI framework on SDL3"
    LANGUAGES C CXX)

# enable_testing() 必须在**任何 add_subdirectory 之前**调：它只对"调用之后
# 处理到的目录"生效，晚一步就会让子目录里的 add_test 静默失效——测试照样
# 编出来、手动跑也过，就是 ctest 里看不见（踩过：示例里的两个集成自检
# 一直不在 ctest 列表里）。
enable_testing()

# ---------------------------------------------------------------------------
# 输出目录：所有产物统一落在构建目录下，源码树保持干净
# ---------------------------------------------------------------------------
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# 编译期开关（关键词：ISDEBUG / ISLOG / ISMESSAGE）
#
#   ISDEBUG   -> INK_ISDEBUG    调试级日志与调试断言
#   ISLOG     -> INK_ISLOG      日志系统，写可执行文件同级的 Log.html
#   ISMESSAGE -> INK_ISMESSAGE  消息队列，UI 线程之外的消息入口
#
# 三者都是**编译期**开关：关掉时对应的宏展开为 ((void)0)，参数不求值、
# 字符串不拼接、对应的实现文件也不参与编译，代码完全不进二进制。
#
# ISDEBUG 默认跟随构建类型（Debug 构建默认打开）；ISLOG / ISMESSAGE 默认打开。
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_ink_isdebug_default ON)
else()
    set(_ink_isdebug_default OFF)
endif()

option(ISDEBUG "调试输出与调试断言（定义 INK_ISDEBUG）" ${_ink_isdebug_default})
option(ISLOG "日志系统，写可执行文件同级的 Log.html（定义 INK_ISLOG）" ON)
option(ISMESSAGE "消息队列参与编译（定义 INK_ISMESSAGE）" ON)

option(INK_BUILD_EXAMPLES "构建示例程序" ${PROJECT_IS_TOP_LEVEL})
option(INK_BUILD_TESTS "构建测试程序" ${PROJECT_IS_TOP_LEVEL})

# ---------------------------------------------------------------------------
# 本机依赖路径覆盖
#
# 该文件被 gitignore，只属于当前这台机器：存在就自动加载，不存在就跳过。
# 这样"SDL3 装在哪""本机要不要固定用某个来源"这类事情写一次就够，
# 不必每次配置都在命令行上重敲 -D。模板见 cmake/CMakeUserPaths.cmake.example。
# ---------------------------------------------------------------------------
set(INK_USER_PATHS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CMakeUserPaths.cmake"
    CACHE FILEPATH "本机依赖路径覆盖文件（被 gitignore，不存在则忽略）")
if(EXISTS "${INK_USER_PATHS_FILE}")
    include("${INK_USER_PATHS_FILE}")
    message(STATUS "Inking: 已加载本机依赖路径覆盖 ${INK_USER_PATHS_FILE}")
endif()

# SDL3：来源由 INK_SDL3_SOURCE 决定，默认 auto（本地目录 → 系统包 → 拉源码）。
include(cmake/SDL3.cmake)

# SDL3_image：同一套四选一来源策略（INK_SDL3_IMAGE_SOURCE）。
# 它是**可选**依赖：接不上就关掉图片加载能力，其余照常构建。
# 所以必须在 SDL3 之后 include——它要找 SDL3::Headers。
include(cmake/SDL3_image.cmake)

# SDL3_ttf：同样四选一（INK_SDL3_TTF_SOURCE），同样是可选依赖。
# 必须在 SDL3 之后 include——它也要找 SDL3::Headers。
include(cmake/SDL3_ttf.cmake)

# ---------------------------------------------------------------------------
# 核心库
# ---------------------------------------------------------------------------
set(INK_CORE_SOURCES
    src/ink/basic/InkingAnchor.cpp
    src/ink/basic/InkingDraw.cpp
    src/ink/basic/Timeline.cpp
    src/ink.cpp
    src/ink/thread/ThreadPool.cpp
    src/ink/thread/TaskQueue.cpp
    src/ink/ui/HitTable.cpp
    src/input/MouseInput.cpp
    src/scene/InkingScene.cpp
    src/scene/SceneLibrary.cpp
    src/window/InkingWindow.cpp
    src/button/ButtonLibrary.cpp
    src/button/ButtonPaint.cpp
    src/button/InkingDynamicButton.cpp
    src/button/InkingStaticButton.cpp)

# 日志实现只在 ISLOG 打开时才参与编译；
# 关闭时 InkLog 在头文件里退化成空实现，所以不会出现链接错误。
if(ISLOG)
    list(APPEND INK_CORE_SOURCES src/ink/basic/InkLog.cpp)
endif()

# 消息队列同理：ISMESSAGE 关闭时 MessageQueue 在头文件里退化成空实现，
# 实现文件不参与编译，队列本体和它依赖的存储都不进二进制。
if(ISMESSAGE)
    list(APPEND INK_CORE_SOURCES src/ink/ui/MessageQueue.cpp)
endif()

add_library(ink_core STATIC ${INK_CORE_SOURCES})
add_library(ink::core ALIAS ink_core)

target_include_directories(ink_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(ink_core PUBLIC ${INK_SDL3_TARGET})
target_compile_features(ink_core PUBLIC cxx_std_20)
set_target_properties(ink_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON)

# SDL3_image 是可选依赖：接上了就链接、并把 INK_HAS_SDL3_IMAGE 传播出去，
# 让用到它的代码能用 #if defined(INK_HAS_SDL3_IMAGE) 做条件编译。
# 同样走 PUBLIC——库和调用方必须看到同一个宏，否则是 ODR 违规
# （和 ISDEBUG / ISLOG 那三个开关同一个道理，见下面那段注释）。
if(INK_HAS_SDL3_IMAGE AND INK_SDL3_IMAGE_TARGET)
    target_link_libraries(ink_core PUBLIC ${INK_SDL3_IMAGE_TARGET})
    target_compile_definitions(ink_core PUBLIC INK_HAS_SDL3_IMAGE=1)
    message(STATUS "Inking: SDL3_image 已接入（${INK_SDL3_IMAGE_TARGET}）")
endif()

# SDL3_ttf 同理：可选、PUBLIC 传播。
if(INK_HAS_SDL3_TTF AND INK_SDL3_TTF_TARGET)
    target_link_libraries(ink_core PUBLIC ${INK_SDL3_TTF_TARGET})
    target_compile_definitions(ink_core PUBLIC INK_HAS_SDL3_TTF=1)
    message(STATUS "Inking: SDL3_ttf 已接入（${INK_SDL3_TTF_TARGET}）")
endif()

find_package(Threads REQUIRED)
target_link_libraries(ink_core PUBLIC Threads::Threads)

# 开关以 PUBLIC 传播：库、示例、测试必须看到同一套宏定义。
# 如果两边看到的宏不一致，头文件里的 inline 代码会有一份"真实现"和一份
# "空实现"，属于 ODR 违规——编译器不会报错，行为会静默错乱。
if(ISDEBUG)
    target_compile_definitions(ink_core PUBLIC INK_ISDEBUG=1)
endif()
if(ISLOG)
    target_compile_definitions(ink_core PUBLIC INK_ISLOG=1)
endif()
if(ISMESSAGE)
    target_compile_definitions(ink_core PUBLIC INK_ISMESSAGE=1)
endif()

# ---------------------------------------------------------------------------
# 控制台窗口：ISDEBUG 关闭时把可执行文件链成 GUI 子系统，双击不再弹黑框。
# ISDEBUG 打开时保留控制台，方便看调试输出。
#
# 挂在 ink_core 的 INTERFACE 上：谁链接 ink_core，谁就继承这条链接选项，
# 直接 add_subdirectory 引入本库的应用不用自己再写一遍。
#
# 注意 —— MinGW 下 `-mwindows` 会换成 GUI 子系统入口，但 mingw-w64 的
# 运行时仍然会把 main() 叫起来，所以源码里照旧写 int main() 即可。
# MSVC 是另一套开关，且必须显式指定入口点，否则链接器会去找 WinMain。
# ---------------------------------------------------------------------------
if(NOT ISDEBUG)
    if(MSVC)
        target_link_options(ink_core INTERFACE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
    elseif(WIN32)
        target_link_options(ink_core INTERFACE -mwindows)
    endif()
endif()

# ---------------------------------------------------------------------------
# inkgen：CXXCSS → C++ 的构建期生成器 + 生成规则自检
# ---------------------------------------------------------------------------
include(cmake/inkgen.cmake)

# ---------------------------------------------------------------------------
# 把 CXXCSS 配置生成成 C++，并挂到一个目标上。
#
#   ink_add_generated(<目标>
#       OUT_DIR   <include 根，约定在 build/ 下>
#       SOURCES   <json 文件列表>
#       [NAMESPACE ink::cxxcss] [HEADER inkgen/button_service.h])
#
# HEADER 可以带一层子目录（默认 `inkgen/button_service.h`）：产物写成
# `#include <inkgen/button_service.h>`，和其他头文件放在一起也不会撞名。
# OUT_DIR 是**加进 include 路径的那个根**，产物落在 OUT_DIR/HEADER 的目录里。
#
# 做完三件事：
#   1. 建自定义命令，配置变了就重新生成（依赖挂在 inkgen 可执行上，
#      所以第一次构建会先把生成器编出来）；
#   2. 把生成出来的 .cpp 编进一个 OBJECT 库；
#   3. 把它的对象文件与 include 目录加到 <目标> 上——调用方只要
#      `#include <inkgen/button_service.h>` 就能用，不需要手写任何构建规则。
#
# 产物只落 build 目录，源码树保持干净（docs/AGENTS.md §3 第 6 条）。
# ---------------------------------------------------------------------------
function(ink_add_generated target)
    cmake_parse_arguments(GEN "" "OUT_DIR;NAMESPACE;HEADER" "SOURCES" ${ARGN})

    if(NOT GEN_OUT_DIR)
        message(FATAL_ERROR "ink_add_generated(${target})：缺少 OUT_DIR")
    endif()
    if(NOT GEN_SOURCES)
        message(FATAL_ERROR
            "ink_add_generated(${target})：SOURCES 为空。"
            "先 glob 出 CXXCSS/Button/*.json 再传进来。")
    endif()
    if(NOT GEN_NAMESPACE)
        set(GEN_NAMESPACE "ink::cxxcss")
    endif()
    if(NOT GEN_HEADER)
        set(GEN_HEADER "inkgen/button_service.h")
    endif()

    # 产物目录 = include 根 + HEADER 里的子目录部分；include 根进 include 路径。
    get_filename_component(_gen_subdir "${GEN_HEADER}" DIRECTORY)
    if(_gen_subdir)
        set(_gen_dir "${GEN_OUT_DIR}/${_gen_subdir}")
    else()
        set(_gen_dir "${GEN_OUT_DIR}")
    endif()

    set(_header "${GEN_OUT_DIR}/${GEN_HEADER}")
    set(_source "${_gen_dir}/button_register.cpp")

    add_custom_command(
        OUTPUT "${_header}" "${_source}"
        COMMAND $<TARGET_FILE:inkgen>
                --out-dir "${GEN_OUT_DIR}"
                --namespace "${GEN_NAMESPACE}"
                --header "${GEN_HEADER}"
                ${GEN_SOURCES}
        DEPENDS inkgen ${GEN_SOURCES}
        COMMENT "inkgen：生成 ${GEN_NAMESPACE} 的按钮配置（${target}）"
        VERBATIM)

    # 独立的生成目标：构建它（或被别的目标依赖）就会触发上面那条命令。
    add_custom_target(${target}_generate DEPENDS "${_header}" "${_source}")

    add_library(${target}_generated OBJECT "${_source}")
    add_dependencies(${target}_generated ${target}_generate)
    # 生成代码 include 的是公开头，所以要知道 include/ 在哪、宏开了哪些。
    target_link_libraries(${target}_generated PRIVATE ink::core)
    # 生成的头文件自己要能找到（`#include <inkgen/buttons_demo/button_service.h>`
    # 里的那一段，是相对 include 根的）。
    target_include_directories(${target}_generated PRIVATE "${GEN_OUT_DIR}")
    set_target_properties(${target}_generated PROPERTIES
        POSITION_INDEPENDENT_CODE ON)

    target_sources(${target} PRIVATE $<TARGET_OBJECTS:${target}_generated>)
    # 产物目录进 include 路径：调用方写 `#include <inkgen/button_service.h>`。
    target_include_directories(${target} PRIVATE "${GEN_OUT_DIR}")

    # 把生成出来的头文件路径往外传一层：
    #   1. 调用方不用把 HEADER 再抄一遍（抄错就是两个不同的产物）；
    #   2. 别的目标（例如生成代码的自检）可以把它当 include 目录用——
    #      生成源文件里写的是 `#include <inkgen/xxx/button_service.h>`，
    #      编译器得从 HEADER 所在目录往上找，所以那个目录也要进 include 路径。
    set(${target}_GENERATED_HEADER "${GEN_HEADER}" PARENT_SCOPE)
    set(${target}_GENERATED_INCLUDE_DIR "${GEN_OUT_DIR}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# 示例 / 测试
# ---------------------------------------------------------------------------
if(INK_BUILD_EXAMPLES)
    add_subdirectory(examples/basic)
    add_subdirectory(examples/overlap)
endif()

# CXXCSS 驱动的示例：它们是"配置 → 代码 → 窗口"这条链路的集成自检，
# 所以只开测试（INK_BUILD_EXAMPLES=OFF）时也要编——test-debug 预设就是这种组合，
# 不这么写的话集成自检在测试预设里根本不存在。
#
#   buttons_demo：手写场景 + **按钮**由生成器产出（外加一堆离屏像素自检）
#   scene_demo  ：同一件事的最小形态——场景 5 行，演示"一行构造整个界面"
if(INK_BUILD_EXAMPLES OR INK_BUILD_TESTS)
    add_subdirectory(examples/buttons_demo)
    add_subdirectory(examples/scene_demo)
endif()

if(INK_BUILD_TESTS)
    # T-6 的验收：按任务编号把 T-1…T-5 的核心契约串一遍（不建渲染器，很快）。
    add_executable(ink_acceptance_test tests/test_acceptance.cpp)
    target_link_libraries(ink_acceptance_test PRIVATE ink::core)
    target_compile_features(ink_acceptance_test PRIVATE cxx_std_20)
    add_test(NAME ink_acceptance_test COMMAND ink_acceptance_test)

    add_executable(ink_test tests/test_main.cpp)
    target_link_libraries(ink_test PRIVATE ink::core)
    target_compile_features(ink_test PRIVATE cxx_std_20)
    add_test(NAME ink_test COMMAND ink_test)

    # 渲染树的离屏像素校验：把场景画到离屏目标再读回像素比对，
    # 不依赖真实窗口，也不受桌面上别的窗口遮挡影响。
    add_executable(ink_render_probe tests/render_probe.cpp)
    target_link_libraries(ink_render_probe PRIVATE ink::core)
    target_compile_features(ink_render_probe PRIVATE cxx_std_20)
    add_test(NAME ink_render_probe COMMAND ink_render_probe)

    # Button 组件自检：形状 SDF / 命中 / 颜色换算 / 三态 / 点击回调，
    # 外加离屏像素校验（三态颜色真的画出差别了）。
    add_executable(ink_button_test tests/test_button.cpp)
    target_link_libraries(ink_button_test PRIVATE ink::core)
    target_compile_features(ink_button_test PRIVATE cxx_std_20)
    add_test(NAME ink_button_test COMMAND ink_button_test)

    # SDL3_image 接入自检：只在真的接上时才编（没接上它连头文件都找不到）。
    if(INK_HAS_SDL3_IMAGE)
        add_executable(ink_image_test tests/test_image.cpp)
        target_link_libraries(ink_image_test PRIVATE ink::core)
        target_compile_features(ink_image_test PRIVATE cxx_std_20)
        add_test(NAME ink_image_test COMMAND ink_image_test)
    endif()

    # SDL3_ttf 接入自检：同样只在接上时编。它不涉及渲染，靠系统字体。
    if(INK_HAS_SDL3_TTF)
        add_executable(ink_ttf_test tests/test_ttf.cpp)
        target_link_libraries(ink_ttf_test PRIVATE ink::core)
        target_compile_features(ink_ttf_test PRIVATE cxx_std_20)
        add_test(NAME ink_ttf_test COMMAND ink_ttf_test)
    endif()
endif()
