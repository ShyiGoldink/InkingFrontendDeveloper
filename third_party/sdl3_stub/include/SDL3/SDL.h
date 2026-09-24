#pragma once

// Offline development stub for the SDL3 API surface used by this project.
// It is NOT a functional SDL3 replacement. Enable with
// INK_SDL3_USE_STUB=ON only when real SDL3 cannot be fetched or installed.

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

constexpr Uint32 SDL_INIT_VIDEO = 0x00000020u;

constexpr Uint32 SDL_WINDOW_RESIZABLE         = 0x00000020u;
constexpr Uint32 SDL_WINDOW_HIGH_PIXEL_DENSITY = 0x00002000u;

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
    SDL_EVENT_KEY_UP
};

// SDL3 encodes the version in a single integer (see real SDL_version.h).
#define SDL_VERSIONNUM_MAJOR(version) ((version) / 1000000)
#define SDL_VERSIONNUM_MINOR(version) (((version) / 1000) % 1000)
#define SDL_VERSIONNUM_MICRO(version) ((version) % 1000)

struct SDL_Window {
    char title[128] = {};
    int  w = 0;
    int  h = 0;
};

struct SDL_Renderer {};

struct SDL_FRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct SDL_Event {
    Uint32 type = 0;
    struct KeyEvent {
        Uint32 key = 0;
    } key;
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

inline SDL_Window* SDL_CreateWindow(const char* title, int w, int h, Uint32) {
    auto* window = new SDL_Window;
    if (title) {
        std::snprintf(window->title, sizeof(window->title), "%s", title);
    }
    window->w = w;
    window->h = h;
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

inline bool SDL_SetRenderDrawColor(SDL_Renderer*, Uint8, Uint8, Uint8, Uint8) {
    return true;
}

inline bool SDL_RenderClear(SDL_Renderer*) { return true; }
inline bool SDL_RenderFillRect(SDL_Renderer*, const SDL_FRect*) { return true; }
inline bool SDL_RenderPresent(SDL_Renderer*) { return true; }

}  // extern "C"
