# SDL3_image acquisition policy for InkingFrontendDeveloper.
#
# 与 cmake/SDL3.cmake 同一套四选一策略（auto / system / fetch / local），
# 理由也一样：**来源必须显式可控**，别把某个来源写死进流程。
#
#   auto    默认。设了 INK_SDL3_IMAGE_LOCAL_DIR 就用它；没设就找系统包；
#           都没有才拉源码一起编译。
#   system  只用系统已安装的 SDL3_image；找不到就**关掉图片支持**（不报错）。
#   fetch   总是拉官方源码一起编译，忽略系统里装了什么。
#   local   只用 INK_SDL3_IMAGE_LOCAL_DIR 指的目录，不联网也不查系统。
#
# 配套变量：
#   INK_ENABLE_SDL3_IMAGE        总开关（默认 ON）。设成 OFF 就完全不碰这个依赖。
#   INK_SDL3_IMAGE_LOCAL_DIR     本地安装目录（里面要有 SDL3_imageConfig.cmake）
#   INK_SDL3_IMAGE_GIT_TAG       fetch 时用的官方 tag
#
# 对外暴露：
#   INK_SDL3_IMAGE_TARGET         成功时为 SDL3_image::SDL3_image，失败为空
#   INK_HAS_SDL3_IMAGE            成功为 TRUE
#
# ---------------------------------------------------------------------------
# 两个坑，都是实测踩出来的，改这个文件前先读：
#
# 1. **find_package 会把结果写进 cache，下次配置直接复用。**
#    不清理的话，一次成功的探测会被后面任何一次探测"捡走"：
#    把 IMAGE_SOURCE 改成 local 指向一个没有 image 的目录，它照样报
#    "using local"——其实是复用了上一次系统包找到的 SDL3_image_DIR。
#    所以每条策略在探测前都先清 cache。
#
# 2. **光靠 CMAKE_PREFIX_PATH 无法把来源限制成"只用本地"。**
#    MSYS2 的工具链前缀（C:/msys64/ucrt64）本来就在 CMake 的隐式搜索路径里，
#    prepend 一个目录只能提高优先级、**排除不掉**系统那份。实测：
#    -DINK_SDL3_IMAGE_SOURCE=local -DINK_SDL3_IMAGE_LOCAL_DIR=D:/nonexistent-dir
#    照样"找到"了系统包，local 策略形同虚设。
#    所以 local 分支**直接算出配置目录、指定给 SDL3_image_DIR**，不走搜索。
#
# 3. SDL3_image 的配置要求能找到 SDL3（它要 SDL3::Headers）。SDL3.cmake 里
#    的 find_package 是在函数里跑的，函数作用域内的 list(PREPEND ...) 不会
#    外泄，所以这里用 SDL3.cmake 留下的 INK_SDL3_CONFIG_DIR 把路径补回来。
# ---------------------------------------------------------------------------

option(INK_ENABLE_SDL3_IMAGE
    "启用 SDL3_image（图片加载）；关掉后不链接、也不定义 INK_HAS_SDL3_IMAGE" ON)

set(INK_SDL3_IMAGE_SOURCE "auto" CACHE STRING
    "SDL3_image 来源：auto = 本地目录优先，其次系统，最后拉源码；system = 只用系统已装的（找不到就关掉图片支持）；fetch = 总是拉源码；local = 只用 INK_SDL3_IMAGE_LOCAL_DIR")
set_property(CACHE INK_SDL3_IMAGE_SOURCE PROPERTY STRINGS auto system fetch local)

set(INK_SDL3_IMAGE_GIT_TAG "release-3.4.6" CACHE STRING
    "SDL3_image git tag/branch used by FetchContent")

set(INK_SDL3_IMAGE_LOCAL_DIR "" CACHE PATH
    "Path to a local SDL3_image install (must contain SDL3_imageConfig.cmake)")

set(INK_SDL3_IMAGE_TARGET "")
set(INK_HAS_SDL3_IMAGE FALSE)

if(NOT INK_ENABLE_SDL3_IMAGE)
    message(STATUS "Inking: SDL3_image 已关闭（INK_ENABLE_SDL3_IMAGE=OFF）")
    return()
endif()

message(STATUS "Inking: SDL3_image 来源策略 = ${INK_SDL3_IMAGE_SOURCE}")

# SDL3 的配置目录：SDL3_imageConfig 要靠它解析 SDL3::Headers。
set(_ink_img_sdl3_hint "")
if(INK_SDL3_CONFIG_DIR)
    list(APPEND _ink_img_sdl3_hint "${INK_SDL3_CONFIG_DIR}")
endif()

# ---------------------------------------------------------------------------
# 探测前清掉上一次的结果（理由见文件头第 1 条）
# ---------------------------------------------------------------------------
function(_ink_clear_sdl3_image_cache)
    unset(SDL3_image_DIR CACHE)
    unset(SDL3_image_CONFIG CACHE)
    unset(SDL3_image_CONSIDERED_CONFIGS CACHE)
    unset(SDL3_image_CONSIDERED_VERSIONS CACHE)
    unset(SDL3_image_FOUND CACHE)
endfunction()

# ---------------------------------------------------------------------------
# 在某个目录里找 SDL3_imageConfig.cmake，找到就把结果写进父作用域。
# 平铺布局 (<dir>/lib/cmake/SDL3_image/...) 和三元组布局
# (<dir>/x86_64-w64-mingw32/lib/cmake/SDL3_image/...) 都认。
#
# 用 file(GLOB) 而不是靠 find_package 搜索：这样"哪个目录算数"完全由本函数
# 决定，不受工具链隐式搜索路径影响——local 策略要的就是这个确定性。
# ---------------------------------------------------------------------------
function(_ink_find_sdl3_image_config_in dir resultVar outDirVar)
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
        if(IS_DIRECTORY "${_entry}/lib/cmake/SDL3_image")
            if(_entry MATCHES "${_arch_hint}")
                list(APPEND _preferred "${_entry}/lib/cmake/SDL3_image")
            else()
                list(APPEND _fallback "${_entry}/lib/cmake/SDL3_image")
            endif()
        endif()
    endforeach()

    # 平铺布局放最后：三元组布局优先，因为它和本机位数对得上。
    foreach(_candidate IN LISTS _preferred _fallback)
        if(EXISTS "${_candidate}/SDL3_imageConfig.cmake")
            set(${resultVar} TRUE PARENT_SCOPE)
            set(${outDirVar} "${_candidate}" PARENT_SCOPE)
            return()
        endif()
    endforeach()

    if(EXISTS "${dir}/lib/cmake/SDL3_image/SDL3_imageConfig.cmake")
        set(${resultVar} TRUE PARENT_SCOPE)
        set(${outDirVar} "${dir}/lib/cmake/SDL3_image" PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 本地目录：**指定目录**，不搜索
# ---------------------------------------------------------------------------
function(_ink_try_local_sdl3_image resultVar)
    set(${resultVar} FALSE PARENT_SCOPE)
    if(NOT INK_SDL3_IMAGE_LOCAL_DIR)
        return()
    endif()

    _ink_find_sdl3_image_config_in("${INK_SDL3_IMAGE_LOCAL_DIR}"
                                   _found _configDir)
    if(NOT _found)
        return()
    endif()

    _ink_clear_sdl3_image_cache()
    set(SDL3_image_DIR "${_configDir}" CACHE PATH "" FORCE)
    # PATHS 里带上一份 SDL3 的配置目录：SDL3_imageConfig 要解析 SDL3::Headers，
    # 而 SDL3 可能是本地目录那份（它不在隐式搜索路径里）。
    find_package(SDL3_image CONFIG QUIET PATHS "${_configDir}"
                 ${_ink_img_sdl3_hint} NO_DEFAULT_PATH)

    if(SDL3_image_FOUND AND TARGET SDL3_image::SDL3_image)
        message(STATUS "Inking: using local SDL3_image at ${_configDir}")
        set(${resultVar} TRUE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 系统包：正常搜索（工具链前缀本来就在搜索路径里，这正是"系统"的含义）
# ---------------------------------------------------------------------------
function(_ink_try_system_sdl3_image resultVar)
    _ink_clear_sdl3_image_cache()
    # PATHS 里的 SDL3 配置目录只是为了解析 SDL3::Headers；
    # 搜索本身**不加 NO_DEFAULT_PATH**——工具链前缀本来就在隐式路径里，
    # 那正是"系统包"的含义。
    find_package(SDL3_image CONFIG QUIET PATHS ${_ink_img_sdl3_hint})
    if(SDL3_image_FOUND AND TARGET SDL3_image::SDL3_image)
        message(STATUS "Inking: using system SDL3_image (${SDL3_image_DIR})")
        set(${resultVar} TRUE PARENT_SCOPE)
    else()
        set(${resultVar} FALSE PARENT_SCOPE)
    endif()
endfunction()

# ---------------------------------------------------------------------------
# 源码拉取
# ---------------------------------------------------------------------------
function(_ink_fetch_sdl3_image)
    message(STATUS "Inking: fetching SDL3_image source tag "
                   "${INK_SDL3_IMAGE_GIT_TAG}...")
    include(FetchContent)

    # 只编库本身：测试、示例、安装都不需要。
    set(SDLIMAGE_TESTS OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_INSTALL OFF CACHE BOOL "" FORCE)
    set(SDLIMAGE_SAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(sdl3_image
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_image.git
        GIT_TAG ${INK_SDL3_IMAGE_GIT_TAG}
        GIT_SHALLOW TRUE
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(sdl3_image)

    message(STATUS "Inking: SDL3_image built from source "
                   "(${INK_SDL3_IMAGE_GIT_TAG})")
endfunction()

# ---------------------------------------------------------------------------
# 结果落地
#
# 用**普通 CACHE** 而不是 CACHE INTERNAL：INTERNAL 隐含 FORCE，会把值永久
# 钉死，下次改了 source、或者把包装上了，也照样沿用旧结果。普通 CACHE 只在
# 变量未定义时取值，每次配置都重新走一遍探测。
# ---------------------------------------------------------------------------
function(_ink_accept_sdl3_image)
    set(INK_SDL3_IMAGE_TARGET "SDL3_image::SDL3_image" PARENT_SCOPE)
    set(INK_HAS_SDL3_IMAGE TRUE PARENT_SCOPE)
    set(INK_SDL3_IMAGE_TARGET "SDL3_image::SDL3_image" CACHE STRING
        "SDL3_image 的 CMake target（探测结果）")
    set(INK_HAS_SDL3_IMAGE TRUE CACHE STRING "是否成功接上 SDL3_image（探测结果）")
endfunction()

function(_ink_reject_sdl3_image reason)
    # 把上一条成功的探测结果清掉，否则"先接上、后关掉"会留下陈旧的 TRUE，
    # 链接行还在找那个 target，配置直接崩。
    unset(INK_SDL3_IMAGE_TARGET CACHE)
    set(INK_HAS_SDL3_IMAGE FALSE CACHE STRING
        "是否成功接上 SDL3_image（探测结果）")
    set(INK_HAS_SDL3_IMAGE FALSE PARENT_SCOPE)

    message(WARNING
        "Inking: 没接上 SDL3_image（${reason}），**图片加载相关功能会被关掉**，"
        "其余部分照常构建。\n"
        "  想装上：MSYS2 UCRT64 执行 pacman -S mingw-w64-ucrt-x86_64-sdl3-image\n"
        "  或者指定本地目录：-DINK_SDL3_IMAGE_SOURCE=local "
        "-DINK_SDL3_IMAGE_LOCAL_DIR=<目录>\n"
        "  或者允许联网拉源码：-DINK_SDL3_IMAGE_SOURCE=fetch\n"
        "  不想要这个能力：-DINK_ENABLE_SDL3_IMAGE=OFF（这条不会再提示）")
endfunction()

# ---------------------------------------------------------------------------
# 按策略分派
# ---------------------------------------------------------------------------
if(INK_SDL3_IMAGE_SOURCE STREQUAL "local")
    _ink_try_local_sdl3_image(_ink_img_ok)
    if(_ink_img_ok)
        _ink_accept_sdl3_image()
    else()
        # local 是"只用这个目录"的硬要求，达不到就是配置错误，直接停下，
        # 绝不悄悄退回系统包或联网。
        message(FATAL_ERROR
            "INK_SDL3_IMAGE_SOURCE=local，但 "
            "INK_SDL3_IMAGE_LOCAL_DIR=${INK_SDL3_IMAGE_LOCAL_DIR} "
            "里没有可用的 SDL3_image（应存在 "
            "lib/cmake/SDL3_image/SDL3_imageConfig.cmake，"
            "或 <三元组>/lib/cmake/SDL3_image/SDL3_imageConfig.cmake）。")
    endif()
    return()
endif()

if(INK_SDL3_IMAGE_SOURCE STREQUAL "system")
    _ink_try_system_sdl3_image(_ink_img_ok)
    if(_ink_img_ok)
        _ink_accept_sdl3_image()
    else()
        # system 是"不许联网"，找不到就降级——这不是配置错误，
        # 因为图片支持本身是可选的。
        _ink_reject_sdl3_image("INK_SDL3_IMAGE_SOURCE=system，但系统里没找到")
    endif()
    return()
endif()

if(INK_SDL3_IMAGE_SOURCE STREQUAL "fetch")
    _ink_fetch_sdl3_image()
    _ink_accept_sdl3_image()
    return()
endif()

if(NOT INK_SDL3_IMAGE_SOURCE STREQUAL "auto")
    message(FATAL_ERROR
        "INK_SDL3_IMAGE_SOURCE 的取值不认识：${INK_SDL3_IMAGE_SOURCE}"
        "（只接受 auto / system / fetch / local）。")
endif()

# auto：本地目录 → 系统 → 拉源码
if(INK_SDL3_IMAGE_LOCAL_DIR)
    _ink_try_local_sdl3_image(_ink_img_ok)
    if(_ink_img_ok)
        _ink_accept_sdl3_image()
    else()
        # 显式给了目录却用不了属于配置错误：直接停下，不悄悄退回别的来源。
        message(FATAL_ERROR
            "指定了 INK_SDL3_IMAGE_LOCAL_DIR，但那里没有可用的 SDL3_image。")
    endif()
    return()
endif()

_ink_try_system_sdl3_image(_ink_img_ok)
if(_ink_img_ok)
    _ink_accept_sdl3_image()
    return()
endif()

# auto 的最后一档：拉源码。这是默认路径上唯一会联网的一步，
# 先说清楚为什么，别让人对着一个卡住的配置猜。
message(STATUS
    "Inking: 系统里没有 SDL3_image，按 auto 策略拉源码 "
    "(${INK_SDL3_IMAGE_GIT_TAG})；不想联网就用 "
    "-DINK_ENABLE_SDL3_IMAGE=OFF 或 -DINK_SDL3_IMAGE_SOURCE=system")
_ink_fetch_sdl3_image()
_ink_accept_sdl3_image()
