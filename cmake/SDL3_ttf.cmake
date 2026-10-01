# SDL3_ttf acquisition policy for InkingFrontendDeveloper.
#
# 与 cmake/SDL3.cmake、cmake/SDL3_image.cmake **完全同一套**四选一策略
# （auto / system / fetch / local）。改这个文件前先读 SDL3_image.cmake 的文件头，
# 那里记着两个踩过的坑，这里同样适用：
#
#   1. find_package 的结果会进 cache，探测前必须清 <Pkg>_DIR；
#   2. 光靠 CMAKE_PREFIX_PATH 没法把来源限制成"只用本地"——工具链前缀本来就在
#      隐式搜索路径里，所以 local 分支要自己算配置目录 + NO_DEFAULT_PATH。
#
# 配套变量：
#   INK_ENABLE_SDL3_TTF        总开关（默认 ON）
#   INK_SDL3_TTF_LOCAL_DIR     本地安装目录（里面要有 SDL3_ttfConfig.cmake）
#   INK_SDL3_TTF_GIT_TAG       fetch 时用的官方 tag
#
# 对外暴露：
#   INK_SDL3_TTF_TARGET        成功时为 SDL3_ttf::SDL3_ttf，失败为空
#   INK_HAS_SDL3_TTF           成功为 TRUE
#
# 和 SDL3_image 一样是**可选**依赖：接不上就打 WARNING、关掉文字功能，
# 其余照常构建。没有文字 UI 用不了，但构建不该因此整个失败。
#
# 版本说明：MSYS2 的 SDL3_ttf 是 3.2.2，比 SDL3（3.4.x）小一截——
# 这是正常的，SDL3_ttfConfig 只要求 SDL3 >= 3.2.6。

option(INK_ENABLE_SDL3_TTF
    "启用 SDL3_ttf（文字渲染）；关掉后不链接、也不定义 INK_HAS_SDL3_TTF" ON)

set(INK_SDL3_TTF_SOURCE "auto" CACHE STRING
    "SDL3_ttf 来源：auto = 本地目录优先，其次系统，最后拉源码；system = 只用系统已装的（找不到就关掉文字支持）；fetch = 总是拉源码；local = 只用 INK_SDL3_TTF_LOCAL_DIR")
set_property(CACHE INK_SDL3_TTF_SOURCE PROPERTY STRINGS auto system fetch local)

set(INK_SDL3_TTF_GIT_TAG "release-3.2.2" CACHE STRING
    "SDL3_ttf git tag/branch used by FetchContent")

set(INK_SDL3_TTF_LOCAL_DIR "" CACHE PATH
    "Path to a local SDL3_ttf install (must contain SDL3_ttfConfig.cmake)")

set(INK_SDL3_TTF_TARGET "")
set(INK_HAS_SDL3_TTF FALSE)

if(NOT INK_ENABLE_SDL3_TTF)
    message(STATUS "Inking: SDL3_ttf 已关闭（INK_ENABLE_SDL3_TTF=OFF）")
    return()
endif()

message(STATUS "Inking: SDL3_ttf 来源策略 = ${INK_SDL3_TTF_SOURCE}")

# SDL3 的配置目录：SDL3_ttfConfig 要靠它解析 SDL3::Headers。
set(_ink_ttf_sdl3_hint "")
if(INK_SDL3_CONFIG_DIR)
    list(APPEND _ink_ttf_sdl3_hint "${INK_SDL3_CONFIG_DIR}")
endif()

function(_ink_clear_sdl3_ttf_cache)
    unset(SDL3_ttf_DIR CACHE)
    unset(SDL3_ttf_CONFIG CACHE)
    unset(SDL3_ttf_CONSIDERED_CONFIGS CACHE)
    unset(SDL3_ttf_CONSIDERED_VERSIONS CACHE)
    unset(SDL3_ttf_FOUND CACHE)
endfunction()

# ---------------------------------------------------------------------------
# 在某个目录里找 SDL3_ttfConfig.cmake（平铺 + 三元组布局）
# ---------------------------------------------------------------------------
function(_ink_find_sdl3_ttf_config_in dir resultVar outDirVar)
    set(${resultVar} FALSE PARENT_SCOPE)
    if(NOT IS_DIRECTORY "${dir}")
        return()
    endif()

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(_arch_hint "x86_64")
    else()
        set(_arch_hint "i686")
    endif()

    set(_preferred "")
    set(_fallback "")
    file(GLOB _entries LIST_DIRECTORIES true "${dir}/*")
    foreach(_entry IN LISTS _entries)
        if(IS_DIRECTORY "${_entry}/lib/cmake/SDL3_ttf")
            if(_entry MATCHES "${_arch_hint}")
                list(APPEND _preferred "${_entry}/lib/cmake/SDL3_ttf")
            else()
                list(APPEND _fallback "${_entry}/lib/cmake/SDL3_ttf")
            endif()
        endif()
    endforeach()

    foreach(_candidate IN LISTS _preferred _fallback)
        if(EXISTS "${_candidate}/SDL3_ttfConfig.cmake")
            set(${resultVar} TRUE PARENT_SCOPE)
            set(${outDirVar} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(EXISTS "${dir}/lib/cmake/SDL3_ttf/SDL3_ttfConfig.cmake")
        set(${resultVar} TRUE PARENT_SCOPE)
        set(${outDirVar} "${dir}/lib/cmake/SDL3_ttf" PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 本地目录：指定目录，不搜索
# ---------------------------------------------------------------------------
function(_ink_try_local_sdl3_ttf resultVar)
    set(${resultVar} FALSE PARENT_SCOPE)
    if(NOT INK_SDL3_TTF_LOCAL_DIR)
        return()
    endif()

    _ink_find_sdl3_ttf_config_in("${INK_SDL3_TTF_LOCAL_DIR}" _found _configDir)
    if(NOT _found)
        return()
    endif()

    _ink_clear_sdl3_ttf_cache()
    set(SDL3_ttf_DIR "${_configDir}" CACHE PATH "" FORCE)
    find_package(SDL3_ttf CONFIG QUIET PATHS "${_configDir}" ${_ink_ttf_sdl3_hint}
                 NO_DEFAULT_PATH)

    if(SDL3_ttf_FOUND AND TARGET SDL3_ttf::SDL3_ttf)
        message(STATUS "Inking: using local SDL3_ttf at ${_configDir}")
        set(${resultVar} TRUE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 系统包：正常搜索（工具链前缀正是"系统"的含义）
# ---------------------------------------------------------------------------
function(_ink_try_system_sdl3_ttf resultVar)
    _ink_clear_sdl3_ttf_cache()
    find_package(SDL3_ttf CONFIG QUIET PATHS ${_ink_ttf_sdl3_hint})
    if(SDL3_ttf_FOUND AND TARGET SDL3_ttf::SDL3_ttf)
        message(STATUS "Inking: using system SDL3_ttf (${SDL3_ttf_DIR})")
        set(${resultVar} TRUE PARENT_SCOPE)
    else()
        set(${resultVar} FALSE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 源码拉取
# ---------------------------------------------------------------------------
function(_ink_fetch_sdl3_ttf)
    message(STATUS "Inking: fetching SDL3_ttf source tag "
                   "${INK_SDL3_TTF_GIT_TAG}...")
    include(FetchContent)

    # 让 SDL3_ttf 用系统/已找到的 freetype + harfbuzz；找不到时它自己会内建。
    # 只想编库本身：测试和示例都不要。
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLTTF_VENDORED OFF CACHE BOOL "" FORCE)

    # SDL3_ttf 依赖 SDL3（同名变量 SDL3_DIR 已经在 SDL3.cmake 里填好了）。
    FetchContent_Declare(sdl3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG ${INK_SDL3_TTF_GIT_TAG}
        GIT_SHALLOW TRUE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(sdl3_ttf)

    message(STATUS "Inking: SDL3_ttf built from source "
                   "(${INK_SDL3_TTF_GIT_TAG})")
endfunction()

function(_ink_accept_sdl3_ttf)
    set(INK_SDL3_TTF_TARGET "SDL3_ttf::SDL3_ttf" PARENT_SCOPE)
    set(INK_HAS_SDL3_TTF TRUE PARENT_SCOPE)
    set(INK_SDL3_TTF_TARGET "SDL3_ttf::SDL3_ttf" CACHE STRING
        "SDL3_ttf 的 CMake target（探测结果）")
    set(INK_HAS_SDL3_TTF TRUE CACHE STRING "是否成功接上 SDL3_ttf（探测结果）")
endfunction()

function(_ink_reject_sdl3_ttf reason)
    # 清掉上一条成功的结果，避免陈旧的 TRUE 让链接行继续找那个 target。
    unset(INK_SDL3_TTF_TARGET CACHE)
    set(INK_HAS_SDL3_TTF FALSE CACHE STRING "是否成功接上 SDL3_ttf（探测结果）")
    set(INK_HAS_SDL3_TTF FALSE PARENT_SCOPE)

    message(WARNING
        "Inking: 没接上 SDL3_ttf（${reason}），**文字渲染相关功能会被关掉**，"
        "其余部分照常构建。\n"
        "  想装上：MSYS2 UCRT64 执行 pacman -S mingw-w64-ucrt-x86_64-sdl3-ttf\n"
        "  或者指定本地目录：-DINK_SDL3_TTF_SOURCE=local "
        "-DINK_SDL3_TTF_LOCAL_DIR=<目录>\n"
        "  或者允许联网拉源码：-DINK_SDL3_TTF_SOURCE=fetch\n"
        "  不想要这个能力：-DINK_ENABLE_SDL3_TTF=OFF（这条不会再提示）")
endfunction()

# ---------------------------------------------------------------------------
# 按策略分派
# ---------------------------------------------------------------------------
if(INK_SDL3_TTF_SOURCE STREQUAL "local")
    _ink_try_local_sdl3_ttf(_ink_ttf_ok)
    if(_ink_ttf_ok)
        _ink_accept_sdl3_ttf()
    else()
        message(FATAL_ERROR
            "INK_SDL3_TTF_SOURCE=local，但 "
            "INK_SDL3_TTF_LOCAL_DIR=${INK_SDL3_TTF_LOCAL_DIR} "
            "里没有可用的 SDL3_ttf（应存在 "
            "lib/cmake/SDL3_ttf/SDL3_ttfConfig.cmake，"
            "或 <三元组>/lib/cmake/SDL3_ttf/SDL3_ttfConfig.cmake）。")
    endif()
    return()
endif()

if(INK_SDL3_TTF_SOURCE STREQUAL "system")
    _ink_try_system_sdl3_ttf(_ink_ttf_ok)
    if(_ink_ttf_ok)
        _ink_accept_sdl3_ttf()
    else()
        # system 是"不许联网"，找不到就降级——不是配置错误。
        _ink_reject_sdl3_ttf("INK_SDL3_TTF_SOURCE=system，但系统里没找到")
    endif()
    return()
endif()

if(INK_SDL3_TTF_SOURCE STREQUAL "fetch")
    _ink_fetch_sdl3_ttf()
    _ink_accept_sdl3_ttf()
    return()
endif()

if(NOT INK_SDL3_TTF_SOURCE STREQUAL "auto")
    message(FATAL_ERROR
        "INK_SDL3_TTF_SOURCE 的取值不认识：${INK_SDL3_TTF_SOURCE}"
        "（只接受 auto / system / fetch / local）。")
endif()

# auto：本地目录 → 系统 → 拉源码
if(INK_SDL3_TTF_LOCAL_DIR)
    _ink_try_local_sdl3_ttf(_ink_ttf_ok)
    if(_ink_ttf_ok)
        _ink_accept_sdl3_ttf()
    else()
        message(FATAL_ERROR
            "指定了 INK_SDL3_TTF_LOCAL_DIR，但那里没有可用的 SDL3_ttf。")
    endif()
    return()
endif()

_ink_try_system_sdl3_ttf(_ink_ttf_ok)
if(_ink_ttf_ok)
    _ink_accept_sdl3_ttf()
    return()
endif()

message(STATUS
    "Inking: 系统里没有 SDL3_ttf，按 auto 策略拉源码 "
    "(${INK_SDL3_TTF_GIT_TAG})；不想联网就用 "
    "-DINK_ENABLE_SDL3_TTF=OFF 或 -DINK_SDL3_TTF_SOURCE=system")
_ink_fetch_sdl3_ttf()
_ink_accept_sdl3_ttf()
