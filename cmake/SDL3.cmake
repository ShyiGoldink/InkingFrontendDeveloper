# SDL3 acquisition policy for InkingFrontendDeveloper.
#
# Resolution order (first match wins):
#   1. INK_SDL3_LOCAL_DIR    -> explicit local SDL3 install (no network needed)
#   2. system-installed SDL3 -> find_package(SDL3 CONFIG)
#   3. FetchContent from src -> official repo at INK_SDL3_GIT_TAG
#
# The explicit option wins over auto-detection: if the user names a
# directory, that is what gets used.
#
# Exposes INK_SDL3_TARGET and SDL3::SDL3 regardless of the chosen source.

set(INK_SDL3_GIT_TAG "release-3.4.2" CACHE STRING
    "SDL3 git tag/branch used by FetchContent")

set(INK_SDL3_LOCAL_DIR "" CACHE PATH
    "Path to a local SDL3 install (must contain SDL3Config.cmake)")

# 1) Explicit local SDL3 install (no network needed).
if(INK_SDL3_LOCAL_DIR)
    # Accept both the flat install layout (<dir>/lib/cmake/SDL3/...) and
    # toolchain-prefixed ones (<dir>/x86_64-w64-mingw32/lib/cmake/SDL3/...).
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
    find_package(SDL3 CONFIG REQUIRED)
    message(STATUS "Inking: using local SDL3 (${SDL3_VERSION}) at "
                   "${INK_SDL3_LOCAL_DIR}")
    set(INK_SDL3_TARGET SDL3::SDL3)
    return()
endif()

# 2) Prefer an installed SDL3.
find_package(SDL3 CONFIG QUIET)
if(SDL3_FOUND)
    message(STATUS "Inking: using system SDL3 (${SDL3_VERSION})")
    set(INK_SDL3_TARGET SDL3::SDL3)
    return()
endif()

# 3) Otherwise fetch and build the official source.
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

set(INK_SDL3_TARGET SDL3::SDL3)
message(STATUS "Inking: SDL3 built from source (${INK_SDL3_GIT_TAG})")
