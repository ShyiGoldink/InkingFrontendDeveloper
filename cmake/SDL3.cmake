# SDL3 acquisition policy for InkingFrontendDeveloper.
#
# 来源由 INK_SDL3_SOURCE 决定，取值为 auto / system / fetch / local：
#
#   auto    默认。设了 INK_SDL3_LOCAL_DIR 就用它；没设就找系统包；
#           都没有才拉源码一起编译。
#   system  只用系统已安装的 SDL3；找不到直接报错，不做任何联网动作。
#   fetch   总是拉官方源码一起编译，忽略系统里装了什么。
#   local   只用 INK_SDL3_LOCAL_DIR 指的那个目录，不联网也不查系统。
#
# 配套变量：
#   INK_SDL3_LOCAL_DIR  本地 SDL3 安装目录（平铺布局或三元组布局都认）
#   INK_SDL3_GIT_TAG    fetch 时用的官方 tag
#
# 本机路径不想每次敲 -D，就写进 cmake/CMakeUserPaths.cmake（该文件被
# gitignore，存在即自动加载），写法见 cmake/CMakeUserPaths.cmake.example。
#
# 无论走哪条路，对外都暴露 INK_SDL3_TARGET，取值都是 SDL3::SDL3。

set(INK_SDL3_SOURCE "auto" CACHE STRING
    "SDL3 来源：auto = 本地目录优先，其次系统，最后拉源码；system = 只用系统已装的；fetch = 总是拉源码；local = 只用 INK_SDL3_LOCAL_DIR")
set_property(CACHE INK_SDL3_SOURCE PROPERTY STRINGS auto system fetch local)

set(INK_SDL3_GIT_TAG "release-3.4.2" CACHE STRING
    "SDL3 git tag/branch used by FetchContent")

set(INK_SDL3_LOCAL_DIR "" CACHE PATH
    "Path to a local SDL3 install (must contain SDL3Config.cmake)")

message(STATUS "Inking: SDL3 来源策略 = ${INK_SDL3_SOURCE}")

# ---------------------------------------------------------------------------
# 本地目录查找：平铺布局 (<dir>/lib/cmake/SDL3/...) 和三元组布局
# (<dir>/x86_64-w64-mingw32/lib/cmake/SDL3/...) 都支持，优先挑与本机位数
# 匹配的那个。结果写回调用方的 <resultVar>：TRUE 表示找到并已设好
# INK_SDL3_TARGET。
# ---------------------------------------------------------------------------
function(_ink_try_local_sdl3 resultVar)
    if(NOT INK_SDL3_LOCAL_DIR)
        set(${resultVar} FALSE PARENT_SCOPE)
        return()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_ink_sdl3_arch_hint "x86_64")
    else()
        set(_ink_sdl3_arch_hint "i686")
    endif()

    set(_ink_sdl3_preferred "")
    set(_ink_sdl3_fallback "")
    file(GLOB _ink_sdl3_entries LIST_DIRECTORIES true
         "${INK_SDL3_LOCAL_DIR}/*")
    foreach(_ink_sdl3_entry IN LISTS _ink_sdl3_entries)
        if(IS_DIRECTORY "${_ink_sdl3_entry}/lib/cmake/SDL3")
            if(_ink_sdl3_entry MATCHES "${_ink_sdl3_arch_hint}")
                list(APPEND _ink_sdl3_preferred "${_ink_sdl3_entry}")
            else()
                list(APPEND _ink_sdl3_fallback "${_ink_sdl3_entry}")
            endif()
        endif()
    endforeach()

    list(PREPEND CMAKE_PREFIX_PATH
         ${_ink_sdl3_preferred} ${_ink_sdl3_fallback} "${INK_SDL3_LOCAL_DIR}")
    find_package(SDL3 CONFIG QUIET)

    if(SDL3_FOUND)
        message(STATUS "Inking: using local SDL3 (${SDL3_VERSION}) at "
                       "${INK_SDL3_LOCAL_DIR}")
        set(INK_SDL3_TARGET SDL3::SDL3 PARENT_SCOPE)
        set(${resultVar} TRUE PARENT_SCOPE)
    else()
        set(${resultVar} FALSE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 系统包查找
# ---------------------------------------------------------------------------
function(_ink_try_system_sdl3 resultVar)
    find_package(SDL3 CONFIG QUIET)
    if(SDL3_FOUND)
        message(STATUS "Inking: using system SDL3 (${SDL3_VERSION})")
        set(INK_SDL3_TARGET SDL3::SDL3 PARENT_SCOPE)
        set(${resultVar} TRUE PARENT_SCOPE)
    else()
        set(${resultVar} FALSE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 源码拉取
# ---------------------------------------------------------------------------
function(_ink_fetch_sdl3)
    message(STATUS "Inking: SDL3 not found, fetching source tag "
                   "${INK_SDL3_GIT_TAG}...")
    include(FetchContent)

    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_SHARED ON CACHE BOOL "" FORCE)
    set(SDL_STATIC OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(sdl3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG ${INK_SDL3_GIT_TAG}
        GIT_SHALLOW TRUE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(sdl3)

    set(INK_SDL3_TARGET SDL3::SDL3 PARENT_SCOPE)
    message(STATUS "Inking: SDL3 built from source (${INK_SDL3_GIT_TAG})")
endfunction()

# 出错时的统一指引：把"怎么办"写清楚，而不是只丢一句"找不到"。
function(_ink_sdl3_fatal reason)
    message(FATAL_ERROR
        "${reason}\n"
        "当前 INK_SDL3_SOURCE=${INK_SDL3_SOURCE}"
        "，INK_SDL3_LOCAL_DIR=${INK_SDL3_LOCAL_DIR}\n"
        "可选做法：\n"
        "  1. 装系统 SDL3（3.2 以上），再用 -DINK_SDL3_SOURCE=system\n"
        "     MSYS2 UCRT64: pacman -S mingw-w64-ucrt-x86_64-sdl3\n"
        "     Debian/Ubuntu: apt install libsdl3-dev\n"
        "     Fedora:        dnf install SDL3-devel\n"
        "     macOS:         brew install sdl3\n"
        "  2. 手头有本地 SDL3，指过来（不联网）：\n"
        "     -DINK_SDL3_SOURCE=local -DINK_SDL3_LOCAL_DIR=<目录>\n"
        "  3. 允许联网时让它拉源码一起编译：-DINK_SDL3_SOURCE=fetch\n"
        "不想每次敲 -D，就把路径写进 cmake/CMakeUserPaths.cmake；"
        "该文件被 gitignore，存在即自动加载，模板见 "
        "cmake/CMakeUserPaths.cmake.example。")
endfunction()

# ---------------------------------------------------------------------------
# 按策略分派
# ---------------------------------------------------------------------------
if(INK_SDL3_SOURCE STREQUAL "local")
    _ink_try_local_sdl3(_ink_local_ok)
    if(NOT _ink_local_ok)
        _ink_sdl3_fatal(
            "INK_SDL3_SOURCE=local，但没能从 INK_SDL3_LOCAL_DIR 找到可用的 SDL3。")
    endif()
    return()
endif()

if(INK_SDL3_SOURCE STREQUAL "system")
    _ink_try_system_sdl3(_ink_system_ok)
    if(NOT _ink_system_ok)
        _ink_sdl3_fatal("INK_SDL3_SOURCE=system，但系统里没有找到可用的 SDL3。")
    endif()
    return()
endif()

if(INK_SDL3_SOURCE STREQUAL "fetch")
    _ink_fetch_sdl3()
    return()
endif()

if(NOT INK_SDL3_SOURCE STREQUAL "auto")
    _ink_sdl3_fatal(
        "INK_SDL3_SOURCE 的取值不认识：${INK_SDL3_SOURCE}"
        "（只接受 auto / system / fetch / local）。")
endif()

# auto：本地目录 → 系统 → 拉源码
if(INK_SDL3_LOCAL_DIR)
    _ink_try_local_sdl3(_ink_local_ok)
    if(NOT _ink_local_ok)
        # 显式给了目录却用不了属于配置错误：直接停下，不悄悄退回别的来源。
        _ink_sdl3_fatal("指定了 INK_SDL3_LOCAL_DIR，但那里没有可用的 SDL3。")
    endif()
    return()
endif()

_ink_try_system_sdl3(_ink_system_ok)
if(_ink_system_ok)
    return()
endif()

_ink_fetch_sdl3()
