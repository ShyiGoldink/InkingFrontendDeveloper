#pragma once

// Offline development stub for the SDL3 API surface used by this project.
// It is NOT a functional SDL3 replacement. Enable with
// INK_SDL3_USE_STUB=ON only when real SDL3 cannot be fetched or installed.
//
// 桩只保证"编译链接通过"，不保证运行期行为：事件泵永远收不到事件，
// 渲染调用全部丢弃。新增代码用到真 SDL3 的 API 时，这里必须同步补上，
// 否则会出现"桩构建绿、真实构建红"（或反过来）。

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

using Uint8  = std::uint8_t;
using Uint16 = std::uint16_t;
using Uint32 = std::uint32_t;
using Uint64 = std::uint64_t;
using Sint32 = std::int32_t;
using SDL_WindowFlags = Uint64;

constexpr Uint32 SDL_INIT_VIDEO = 0x00000020u;

// 窗口标志：低位与真 SDL3 对齐，够用就行。
constexpr SDL_WindowFlags SDL_WINDOW_BORDERLESS           = 0x0000000000000010u;
constexpr SDL_WindowFlags SDL_WINDOW_RESIZABLE            = 0x0000000000000020u;
constexpr SDL_WindowFlags SDL_WINDOW_MINIMIZED            = 0x0000000000000040u;
constexpr SDL_WindowFlags SDL_WINDOW_MAXIMIZED            = 0x0000000000000080u;
constexpr SDL_WindowFlags SDL_WINDOW_HIGH_PIXEL_DENSITY   = 0x0000000000002000u;

constexpr Uint8 SDL_BUTTON_LEFT   = 1;
constexpr Uint8 SDL_BUTTON_MIDDLE = 2;
constexpr Uint8 SDL_BUTTON_RIGHT  = 3;

enum SDL_LogicalPresentation {
    SDL_LOGICAL_PRESENTATION_DISABLED,
    SDL_LOGICAL_PRESENTATION_STRETCH,
    SDL_LOGICAL_PRESENTATION_LETTERBOX,
    SDL_LOGICAL_PRESENTATION_OVERSCAN,
    SDL_LOGICAL_PRESENTATION_INTEGER_SCALE
};

enum SDL_EventType {
    SDL_EVENT_QUIT = 0x100,
    SDL_EVENT_KEY_DOWN,
    SDL_EVENT_KEY_UP,
    SDL_EVENT_WINDOW_MINIMIZED = 0x210,
    SDL_EVENT_WINDOW_MAXIMIZED,
    SDL_EVENT_WINDOW_RESTORED,
    SDL_EVENT_MOUSE_MOTION = 0x400,
    SDL_EVENT_MOUSE_BUTTON_DOWN,
    SDL_EVENT_MOUSE_BUTTON_UP
};

// SDL3 encodes the version in a single integer (see real SDL_version.h).
#define SDL_VERSIONNUM_MAJOR(version) ((version) / 1000000)
#define SDL_VERSIONNUM_MINOR(version) (((version) / 1000) % 1000)
#define SDL_VERSIONNUM_MICRO(version) ((version) % 1000)

struct SDL_Window {
    char            title[128] = {};
    int             w = 0;
    int             h = 0;
    SDL_WindowFlags flags = 0;
};

struct SDL_Renderer {};

struct SDL_Surface {};

struct SDL_FPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct SDL_FRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct SDL_KeyboardEvent {
    Uint32 type = 0;
    Uint32 key = 0;
};

struct SDL_MouseMotionEvent {
    Uint32 type = 0;
    float  x = 0.0f;
    float  y = 0.0f;
};

struct SDL_MouseButtonEvent {
    Uint32 type = 0;
    Uint8  button = 0;
    float  x = 0.0f;
    float  y = 0.0f;
};

struct SDL_Event {
    Uint32 type = 0;
    SDL_KeyboardEvent   key;
    SDL_MouseMotionEvent motion;
    SDL_MouseButtonEvent button;
};

constexpr Uint32 SDLK_ESCAPE = 27;

extern "C" {

inline void SDL_Log(const char* fmt, ...) {
    std::va_list args;
    va_start(args, fmt);
    std::vfprintf(stderr, fmt, args);
    std::fputc('\n', stderr);
    va_end(args);
}

inline bool SDL_Init(Uint32) { return true; }
inline void SDL_Quit() {}

inline const char* SDL_GetError() { return "stub error"; }

inline int SDL_GetVersion() { return 0; }

inline Uint64 SDL_GetTicks() { return 0; }
inline char* SDL_getenv(const char* name) { return std::getenv(name); }

inline SDL_Window* SDL_CreateWindow(const char* title, int w, int h, SDL_WindowFlags flags) {
    auto* window = new SDL_Window;
    if (title) {
        std::snprintf(window->title, sizeof(window->title), "%s", title);
    }
    window->w = w;
    window->h = h;
    window->flags = flags;
    return window;
}

inline void SDL_DestroyWindow(SDL_Window* window) { delete window; }

inline bool SDL_SetWindowSize(SDL_Window* window, int w, int h) {
    if (!window) {
        return false;
    }
    window->w = w;
    window->h = h;
    return true;
}

inline bool SDL_SetWindowTitle(SDL_Window* window, const char* title) {
    if (!window) {
        return false;
    }
    if (title) {
        std::snprintf(window->title, sizeof(window->title), "%s", title);
    }
    return true;
}

inline SDL_WindowFlags SDL_GetWindowFlags(SDL_Window* window) {
    return window ? window->flags : 0;
}

inline bool SDL_MinimizeWindow(SDL_Window* window) {
    if (!window) {
        return false;
    }
    window->flags |= SDL_WINDOW_MINIMIZED;
    window->flags &= ~SDL_WINDOW_MAXIMIZED;
    return true;
}

inline bool SDL_MaximizeWindow(SDL_Window* window) {
    if (!window) {
        return false;
    }
    window->flags |= SDL_WINDOW_MAXIMIZED;
    window->flags &= ~SDL_WINDOW_MINIMIZED;
    return true;
}

inline bool SDL_RestoreWindow(SDL_Window* window) {
    if (!window) {
        return false;
    }
    window->flags &= ~(SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED);
    return true;
}

inline bool SDL_GetWindowPosition(SDL_Window* window, int* x, int* y) {
    if (!window) {
        return false;
    }
    if (x) {
        *x = 0;
    }
    if (y) {
        *y = 0;
    }
    return true;
}

inline bool SDL_SetWindowPosition(SDL_Window*, int, int) { return true; }

inline Uint32 SDL_GetGlobalMouseState(float* x, float* y) {
    if (x) {
        *x = 0.0f;
    }
    if (y) {
        *y = 0.0f;
    }
    return 0;
}

inline SDL_Renderer* SDL_CreateRenderer(SDL_Window*, const char*) {
    return new SDL_Renderer;
}

inline void SDL_DestroyRenderer(SDL_Renderer* renderer) { delete renderer; }

inline bool SDL_PollEvent(SDL_Event* event) {
    if (event) {
        std::memset(event, 0, sizeof(*event));
    }
    return false;
}

inline void SDL_SetRenderLogicalPresentation(
    SDL_Renderer*, int, int, SDL_LogicalPresentation) {}

inline bool SDL_RenderCoordinatesFromWindow(
    SDL_Renderer*, float windowX, float windowY, float* x, float* y) {
    if (x) {
        *x = windowX;
    }
    if (y) {
        *y = windowY;
    }
    return true;
}

inline bool SDL_SetRenderDrawColor(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8) {
    return true;
}

inline bool SDL_RenderClear(SDL_Renderer*) { return true; }
inline bool SDL_RenderFillRect(SDL_Renderer*, const SDL_FRect*) { return true; }
inline bool SDL_RenderRect(SDL_Renderer*, const SDL_FRect*) { return true; }
inline bool SDL_RenderLine(SDL_Renderer*, float, float, float, float) { return true; }

inline SDL_Surface* SDL_RenderReadPixels(SDL_Renderer*, const void*) {
    return nullptr;  // 桩没有像素可读
}

inline bool SDL_SaveBMP(SDL_Surface*, const char*) { return false; }

inline void SDL_DestroySurface(SDL_Surface* surface) { delete surface; }

inline bool SDL_RenderPresent(SDL_Renderer*) { return true; }

}  // extern "C"
