#include <ink/ink.h>
#include <SDL3/SDL.h>

#include <chrono>
#include <cstdlib>

namespace {

constexpr int kDesignWidth  = 1280;
constexpr int kDesignHeight = 720;

}  // namespace

int main() {
    const std::string sdlVersion = ink::sdl3_version();
    SDL_Log("InkingFrontendDeveloper %s bootstrap (SDL3 %s)",
            ink::kVersion, sdlVersion.c_str());

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow(
        "InkingFrontendDeveloper — bootstrap",
        kDesignWidth, kDesignHeight,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!window) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) {
        SDL_Log("SDL_CreateRenderer failed: %s", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // The layout pipeline (later) renders into this logical design space;
    // letterboxing keeps the 1280x720 aspect intact on any window shape.
    SDL_SetRenderLogicalPresentation(
        renderer, kDesignWidth, kDesignHeight,
        SDL_LOGICAL_PRESENTATION_LETTERBOX);

    // 计时用 steady_clock，和 SDL 的计时器解耦——这个循环本来只是占位。
    const bool autoQuit = SDL_getenv("INK_AUTOQUIT") != nullptr;
    const auto start = std::chrono::steady_clock::now();
    bool running = true;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            } else if (event.type == SDL_EVENT_KEY_DOWN
                       && event.key.key == SDLK_ESCAPE) {
                running = false;
            }
        }

        if (autoQuit
            && std::chrono::steady_clock::now() - start
                   > std::chrono::milliseconds(2000)) {
            running = false;  // smoke-test mode: close after ~2 s
        }

        // Placeholder frame: the SDF shape / style pipeline replaces this.
        SDL_SetRenderDrawColor(renderer, 18, 18, 24, 255);
        SDL_RenderClear(renderer);

        SDL_SetRenderDrawColor(renderer, 34, 96, 168, 255);
        const SDL_FRect placeholder{140.0f, 60.0f, 1000.0f, 600.0f};
        SDL_RenderFillRect(renderer, &placeholder);

        SDL_RenderPresent(renderer);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
