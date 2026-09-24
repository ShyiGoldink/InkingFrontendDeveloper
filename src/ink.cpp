#include <ink/ink.h>

#include <SDL3/SDL.h>

namespace ink {

std::string sdl3_version() {
    // SDL3 把版本编码成一个整数返回（SDL2 是出参形式的 SDL_version）。
    const int version = SDL_GetVersion();
    return std::to_string(SDL_VERSIONNUM_MAJOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MINOR(version)) + "."
         + std::to_string(SDL_VERSIONNUM_MICRO(version));
}

}  // namespace ink

