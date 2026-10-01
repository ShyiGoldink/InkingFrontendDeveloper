// SDL3_image 接入自检：确认它真的链接得上、而且能解码出正确的像素。
//
// 不依赖任何外部图片文件——PNG 字节直接编进源码（一张确定的手写图），
// 这样这个自检在干净环境里也能跑。
//
// 非 0 退出码表示有检查项失败。

#include <ink/ink.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if !defined(INK_HAS_SDL3_IMAGE)
#error "本文件只在 INK_HAS_SDL3_IMAGE 打开时才该参与编译"
#endif

#include <SDL3_image/SDL_image.h>

namespace {

int gFailed = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s %s\n", condition ? "[通过]" : "[失败]", what.c_str());
    if (!condition) {
        ++gFailed;
    }
}

/**
 * 一张确定的手写 PNG：8x8，纯红不透明。
 *
 * 存成字节而不是外部文件，是为了让自检不依赖工作目录和磁盘上的素材——
 * 生成器接进来之前，CXXCSS 里也没有图片可指。
 */
constexpr std::uint8_t kRedPng[] = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D,
    0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08,
    0x08, 0x06, 0x00, 0x00, 0x00, 0xC4, 0x0F, 0xBE, 0x8B, 0x00, 0x00, 0x00,
    0x16, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9C, 0x63, 0xFC, 0xCF, 0xC0, 0xF0,
    0x9F, 0x01, 0x0F, 0x60, 0xC2, 0x27, 0x39, 0x7C, 0x14, 0x00, 0x00, 0x12,
    0x5B, 0x02, 0x0E, 0x1F, 0x4A, 0x49, 0x5D, 0x00, 0x00, 0x00, 0x00, 0x49,
    0x45, 0x4E, 0x44, 0xAE, 0x42, 0x60, 0x82,
};

}  // namespace

int main() {
    std::printf("SDL3_image 接入自检（SDL3 %s）\n", ink::sdl3_version().c_str());

    // SDL3_image 要读 SDL 的错误串，得先把 SDL 起来。
    if (!SDL_Init(0)) {
        std::printf("[失败] SDL_Init 失败：%s\n", SDL_GetError());
        return 1;
    }

    // ---- 版本探针：确认链接到的确实是 3.x 的 SDL3_image ----
    {
        const int version = IMG_Version();
        check(SDL_VERSIONNUM_MAJOR(version) == 3,
              "SDL3_image 主版本是 3（SDLK 风格的编码版本号）");
        std::printf("      SDL3_image 版本 %d.%d.%d\n",
                    SDL_VERSIONNUM_MAJOR(version),
                    SDL_VERSIONNUM_MINOR(version),
                    SDL_VERSIONNUM_MICRO(version));
    }

    // ---- 解码探针：从内存里的 PNG 解出 surface ----
    {
        SDL_IOStream* stream = SDL_IOFromConstMem(kRedPng, sizeof(kRedPng));
        check(stream != nullptr, "从内存字节建立 IO 流");
        if (stream == nullptr) {
            std::printf("[失败] SDL_IOFromConstMem：%s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        SDL_Surface* surface = IMG_Load_IO(stream, true);  // true = 关掉流
        check(surface != nullptr, "IMG_Load_IO 从 PNG 字节解出 surface");
        if (surface == nullptr) {
            std::printf("[失败] IMG_Load_IO：%s\n", SDL_GetError());
            SDL_Quit();
            return 1;
        }

        check(surface->w == 8 && surface->h == 8,
              "解出来的尺寸是 8x8（说明真的走了解码，不是空壳）");

        // 取左上角那个像素，确认颜色真的是红。
        SDL_Surface* rgba =
            SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
        check(rgba != nullptr, "转成 RGBA32 便于逐字节比对");
        if (rgba != nullptr) {
            std::uint8_t r = 0;
            std::uint8_t g = 0;
            std::uint8_t b = 0;
            std::uint8_t a = 0;
            const SDL_PixelFormatDetails* details =
                SDL_GetPixelFormatDetails(rgba->format);
            if (details != nullptr && SDL_LockSurface(rgba)) {
                const auto* row = static_cast<const std::uint8_t*>(rgba->pixels);
                std::uint32_t raw = 0;
                for (int i = 0; i < details->bytes_per_pixel; ++i) {
                    raw |= static_cast<std::uint32_t>(row[i]) << (8 * i);
                }
                SDL_GetRGBA(raw, details, nullptr, &r, &g, &b, &a);
                SDL_UnlockSurface(rgba);
            }
            check(r > 200 && g < 60 && b < 60,
                  "像素是红色（解出来的内容对）");
            check(a == 255, "像素不透明");
            std::printf("      左上角像素 rgba=(%u,%u,%u,%u)\n", r, g, b, a);
            SDL_DestroySurface(rgba);
        }

        SDL_DestroySurface(surface);
    }

    // ---- 失败路径：坏数据要返回 nullptr 而不是崩 ----
    {
        const std::uint8_t garbage[] = {0x01, 0x02, 0x03, 0x04, 0x05};
        SDL_IOStream* stream = SDL_IOFromConstMem(garbage, sizeof(garbage));
        SDL_Surface* surface =
            stream != nullptr ? IMG_Load_IO(stream, true) : nullptr;
        check(surface == nullptr, "坏数据返回 nullptr，不崩");
        if (surface != nullptr) {
            SDL_DestroySurface(surface);
        }
    }

    SDL_Quit();

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
