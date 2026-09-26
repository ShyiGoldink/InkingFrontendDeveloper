cmake_minimum_required(VERSION 3.25)

project(InkingFrontendDeveloper
    VERSION 0.1.0
    DESCRIPTION "Compile-time-polymorphic UI framework on SDL3"
    LANGUAGES C CXX)

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

# ---------------------------------------------------------------------------
# 核心库
# ---------------------------------------------------------------------------
set(INK_CORE_SOURCES
    src/ink/basic/InkingAnchor.cpp
    src/ink.cpp
    src/ink/thread/ThreadPool.cpp
    src/ink/thread/TaskQueue.cpp
    src/input/MouseInput.cpp
    src/window/InkingWindow.cpp)

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
# 示例 / 测试
# ---------------------------------------------------------------------------
if(INK_BUILD_EXAMPLES)
    add_subdirectory(examples/basic)
endif()

if(INK_BUILD_TESTS)
    enable_testing()
    add_executable(ink_test tests/test_main.cpp)
    target_link_libraries(ink_test PRIVATE ink::core)
    target_compile_features(ink_test PRIVATE cxx_std_20)
    add_test(NAME ink_test COMMAND ink_test)
endif()
