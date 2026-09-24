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
# 编译期开关（关键词：ISDEBUG / ISLOG）
#
#   ISDEBUG -> INK_ISDEBUG  调试级日志与调试断言
#   ISLOG   -> INK_ISLOG    日志系统，写可执行文件同级的 Log.html
#
# 两者都是**编译期**开关：关掉时 INK_LOG_* 宏展开为 ((void)0)，
# 参数不求值、字符串不拼接、日志代码不进二进制。
#
# ISDEBUG 默认跟随构建类型（Debug 构建默认打开）；ISLOG 默认打开。
# ---------------------------------------------------------------------------
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(_ink_isdebug_default ON)
else()
    set(_ink_isdebug_default OFF)
endif()

option(ISDEBUG "调试输出与调试断言（定义 INK_ISDEBUG）" ${_ink_isdebug_default})
option(ISLOG "日志系统，写可执行文件同级的 Log.html（定义 INK_ISLOG）" ON)

option(INK_BUILD_EXAMPLES "构建示例程序" ${PROJECT_IS_TOP_LEVEL})
option(INK_BUILD_TESTS "构建测试程序" ${PROJECT_IS_TOP_LEVEL})

# SDL3：优先使用系统安装版本，找不到则拉取官方源码，另有离线桩兜底。
include(cmake/SDL3.cmake)

# ---------------------------------------------------------------------------
# 核心库
# ---------------------------------------------------------------------------
set(INK_CORE_SOURCES
    src/ink.cpp
    src/ink/thread/ThreadPool.cpp
    src/ink/thread/TaskQueue.cpp
    src/ink/ui/MessageQueue.cpp
    src/window/InkingWindow.cpp)

# 日志实现只在 ISLOG 打开时才参与编译；
# 关闭时 InkLog 在头文件里退化成空实现，所以不会出现链接错误。
if(ISLOG)
    list(APPEND INK_CORE_SOURCES src/ink/basic/InkLog.cpp)
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

