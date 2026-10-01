// 渲染树的确定性校验：把场景渲染到**离屏**渲染目标，再把像素读回来，
// 逐个采样点比对颜色。
//
// 为什么要离屏：开窗截图会被别的窗口遮挡，验证结果随桌面状态漂移，
// 没法在无人值守的地方跑。离屏目标走的是同一条提交路径
// （SceneLibrary::RenderScene → InkingScene::Render → 节点 onRender →
// SDL_RenderFillRect），但结果完全由自己掌控。
//
// 非 0 退出码表示有检查项失败。

#include <ink/ink.h>
#include <ink/basic/InkingAnchor.h>
#include <scene/InkingScene.h>
#include <scene/SceneLibrary.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int gFailed = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s %s\n", condition ? "[通过]" : "[失败]", what.c_str());
    if (!condition) {
        ++gFailed;
    }
}

constexpr int kWidth = 1920;
constexpr int kHeight = 1080;

// 颜色都是不透明的纯色，读写两边用同一套值，避开色彩空间换算的干扰。
constexpr std::uint32_t kRed = 0xFFE05252u;
constexpr std::uint32_t kGreen = 0xFF52E07Fu;
constexpr std::uint32_t kBlue = 0xFF5290E0u;
constexpr std::uint32_t kBackdrop = 0xFF1A1A22u;

class ProbeScene : public ink::InkingScene {
public:
    ProbeScene() : ink::InkingScene("Probe") {}
};

ink::AnchorData patch(float x, float y, int z, std::uint32_t color) {
    ink::AnchorData data;
    data.width = 400;
    data.height = 260;
    data.offsetX = x;
    data.offsetY = y;
    data.zIndex = z;
    data.color = color;
    return data;
}

/** 场景的绘制入口是 protected，框架侧统一从 SceneLibrary 走。 */
void renderActiveScene(SDL_Renderer* renderer) {
    ink::SceneLibrary::RenderScene(renderer);
}

/** 把离屏目标读成 0xAARRGGBB 的紧凑缓冲。 */
bool readPixelsAsArgb(SDL_Renderer* renderer, std::vector<std::uint32_t>& out) {
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (surface == nullptr) {
        return false;
    }

    bool ok = false;
    if (SDL_LockSurface(surface)) {
        const SDL_PixelFormat format = surface->format;
        const int bytesPerPixel = SDL_BYTESPERPIXEL(format);
        const SDL_PixelFormatDetails* details =
            SDL_GetPixelFormatDetails(format);
        if (details == nullptr || bytesPerPixel <= 0 || bytesPerPixel > 4) {
            SDL_UnlockSurface(surface);
            SDL_DestroySurface(surface);
            return false;
        }

        // 按 SDL 报的通道掩码取字节，不假设是哪一种 32 位排布。
        out.assign(static_cast<std::size_t>(kWidth) * kHeight, 0u);
        for (int y = 0; y < kHeight && y < surface->h; ++y) {
            const auto* row = static_cast<const std::uint8_t*>(surface->pixels)
                            + static_cast<std::size_t>(y) * surface->pitch;
            for (int x = 0; x < kWidth && x < surface->w; ++x) {
                const std::uint8_t* pixel = row
                    + static_cast<std::size_t>(x) * bytesPerPixel;

                std::uint32_t value = 0;
                for (int i = 0; i < bytesPerPixel; ++i) {
                    value |= static_cast<std::uint32_t>(pixel[i]) << (8 * i);
                }

                const auto channel = [&](std::uint32_t mask) -> std::uint8_t {
                    if (mask == 0u) {
                        return 0xFFu;  // 没有这个通道时按"满"处理
                    }
                    std::uint32_t shift = 0;
                    while (((mask >> shift) & 1u) == 0u) {
                        ++shift;
                    }
                    const std::uint32_t max = mask >> shift;
                    const std::uint32_t raw = (value & mask) >> shift;
                    return static_cast<std::uint8_t>((raw * 255u) / max);
                };

                out[static_cast<std::size_t>(y) * kWidth + x] =
                    (static_cast<std::uint32_t>(
                         channel(details->Amask)) << 24)
                    | (static_cast<std::uint32_t>(
                           channel(details->Rmask)) << 16)
                    | (static_cast<std::uint32_t>(
                           channel(details->Gmask)) << 8)
                    | static_cast<std::uint32_t>(channel(details->Bmask));
            }
        }
        ok = true;
        SDL_UnlockSurface(surface);
    }
    SDL_DestroySurface(surface);
    return ok;
}

/** 从紧凑缓冲里取一个像素（0xAARRGGBB）。 */
std::uint32_t pixelAt(const std::vector<std::uint32_t>& pixels, int x, int y) {
    return pixels[static_cast<std::size_t>(y) * kWidth + x];
}

std::string toHex(std::uint32_t color) {
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "0x%08X", color);
    return buffer;
}

}  // namespace

int main() {
    std::printf("渲染树离屏校验（SDL3 %s）\n", ink::sdl3_version().c_str());

    // 用 dummy 视频驱动：不需要真窗口，跑得动就行。
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("[失败] SDL_Init 失败：%s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("offscreen", 64, 64, 0);
    if (window == nullptr) {
        std::printf("[失败] 建窗口失败：%s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (renderer == nullptr) {
        std::printf("[失败] 建渲染器失败：%s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    std::printf("渲染驱动：%s\n", SDL_GetRendererName(renderer));

    SDL_Texture* target =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888,
                          SDL_TEXTUREACCESS_TARGET, kWidth, kHeight);
    if (target == nullptr) {
        std::printf("[失败] 建离屏纹理失败：%s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    check(SDL_SetRenderTarget(renderer, target), "渲染目标切到离屏纹理");

    // 先验一件事：这个离屏目标上的 SDL_RenderFillRect 到底读不读得回来。
    // 读不回来的话，后面所有像素断言都没有意义，得先在这里暴露。
    {
        std::vector<std::uint32_t> sanity;
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        SDL_RenderClear(renderer);
        const SDL_FRect box{100.0f, 100.0f, 50.0f, 50.0f};
        SDL_SetRenderDrawColor(renderer, 0, 255, 0, 255);
        SDL_RenderFillRect(renderer, &box);
        SDL_RenderPresent(renderer);
        const bool ok = readPixelsAsArgb(renderer, sanity);
        check(ok, "离屏目标可读回");
        if (ok) {
            check(pixelAt(sanity, 120, 120) == 0xFF00FF00u,
                  "离屏目标上的 SDL_RenderFillRect 能读回来（绿）");
            check(pixelAt(sanity, 900, 900) == 0xFFFFFFFFu,
                  "离屏目标上的清屏色能读回来（白）");
        }
    }

    // ---- 场景：背景板 + 三个互相重叠的块 ----
    ProbeScene scene;

    // 场景要先注册进库、并设为活跃，才能经框架侧渲染——
    // Render 是 protected，这正是"只有活跃场景会被渲染"这条规矩的入口。
    check(scene.IsRegistered(), "探针场景注册成功");
    check(!scene.IsRejected(), "探针场景没有被驳回");
    ink::SceneLibrary::SetActiveScene(&scene);
    check(ink::SceneLibrary::GetActiveScene() == &scene, "探针场景已激活");
    check(scene.IsActive(), "场景自己也知道自己活跃");

    ink::AnchorData backdrop;
    backdrop.width = kWidth;
    backdrop.height = kHeight;
    backdrop.zIndex = -100;
    backdrop.color = kBackdrop;
    ink::InkingStaticAnchor backdropNode(&scene, backdrop);

    // z 递增、逐步右移下移：重叠区应当显示 z 更大的那一块。
    ink::InkingStaticAnchor red(&scene, patch(240.0f, 120.0f, 1, kRed));
    ink::InkingStaticAnchor green(&scene, patch(460.0f, 160.0f, 2, kGreen));
    ink::InkingStaticAnchor blue(&scene, patch(680.0f, 200.0f, 3, kBlue));

    check(scene.GetDrawItemCount() == 4, "绘制列表有 4 项（背景 + 三块）");

    // 先把背景板藏起来跑一次：如果这时三块读得回来，就说明是背景板
    // （铺满整屏、又排在最后画）把三块盖掉了，而不是三块没画。
    {
        std::vector<std::uint32_t> shadow;
        backdropNode.SetVisible(false);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        renderActiveScene(renderer);
        SDL_RenderPresent(renderer);
        readPixelsAsArgb(renderer, shadow);
        check(pixelAt(shadow, 300, 150) == kRed,
              "藏掉背景板后，红块读得回来");
        check(pixelAt(shadow, 1100, 700) == 0xFF000000u,
              "藏掉背景板后，空白处是清屏的黑");
        backdropNode.SetVisible(true);
    }

    // 先清成纯黑，再看场景自己画成什么样；这样"没画到"和"画错了"能分开。
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    renderActiveScene(renderer);
    SDL_RenderPresent(renderer);

    // ---- 把像素读回来 ----
    std::vector<std::uint32_t> pixels;
    const bool readOk = readPixelsAsArgb(renderer, pixels);
    check(readOk, "离屏像素读回成功");
    if (!readOk) {
        SDL_DestroyTexture(target);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    struct Probe {
        const char* what;
        int x;
        int y;
        std::uint32_t expect;
    };
    const Probe probes[] = {
        {"背景板露出来的地方", 1500, 900, kBackdrop},
        // 每块挑一个**只有自己**覆盖的点：
        //   红 x240-640 y120-380；绿 x460-860 y160-420；蓝 x680-1080 y200-460
        {"红块独有区域", 300, 150, kRed},
        {"绿块独有区域（红够不着）", 700, 180, kGreen},
        {"蓝块独有区域（红绿都够不着）", 900, 380, kBlue},
        {"红绿重叠 → 绿压红", 480, 200, kGreen},
        {"绿蓝重叠 → 蓝压绿", 700, 300, kBlue},
        {"三块都够不着 → 背景板", 1100, 700, kBackdrop},
    };

    for (const Probe& probe : probes) {
        const std::uint32_t actual = pixelAt(pixels, probe.x, probe.y);
        check(actual == probe.expect,
              std::string(probe.what) + "：期望 " + toHex(probe.expect)
                  + "，实得 " + toHex(actual));
    }

    // 隐藏的块必须彻底不画：放一块 z 最高的紫色，藏起来，整屏都不该有它。
    {
        ink::AnchorData hidden = patch(0.0f, 0.0f, 99, 0xFFFF00FFu);
        hidden.width = kWidth;
        hidden.height = kHeight;
        ink::InkingStaticAnchor hiddenNode(&scene, hidden);
        check(hiddenNode.SetVisible(false), "隐藏一块覆盖全屏的高层块");

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        renderActiveScene(renderer);
        SDL_RenderPresent(renderer);
        readPixelsAsArgb(renderer, pixels);

        std::size_t magenta = 0;
        for (std::uint32_t color : pixels) {
            if (color == 0xFFFF00FFu) {
                ++magenta;
            }
        }
        check(magenta == 0, "隐藏的块一个像素都没有画出来");
    }

    // 恢复可见之后，它应当盖住全屏（验证可见性是可逆的）。
    {
        // 上一块已经析构，这里重放一次"显示→隐藏→显示"的循环。
        ink::AnchorData cover = patch(0.0f, 0.0f, 99, 0xFFFF00FFu);
        cover.width = kWidth;
        cover.height = kHeight;
        ink::InkingStaticAnchor coverNode(&scene, cover);
        check(coverNode.SetVisible(false) && coverNode.SetVisible(true),
              "隐藏再显示同一块");
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        renderActiveScene(renderer);
        readPixelsAsArgb(renderer, pixels);
        const std::uint32_t top = pixelAt(pixels, 300, 150);
        check(top == 0xFFFF00FFu, "重新显示的块盖在最上面");
    }

    // ---- 只有活跃场景会被渲染 ----
    {
        // 把活跃场景摘掉：这一帧应当什么都不画（保持清屏色）。
        ink::SceneLibrary::SetActiveScene(nullptr);
        check(!scene.IsActive(), "被摘掉活跃态后场景自己也知道");
        check(!ink::SceneLibrary::RenderScene(renderer),
              "没有活跃场景时 RenderScene 返回 false");

        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        renderActiveScene(renderer);
        readPixelsAsArgb(renderer, pixels);
        check(pixelAt(pixels, 300, 150) == 0xFF000000u,
              "没有活跃场景时，场景的内容一个像素都没画");

        // 放回来继续
        ink::SceneLibrary::SetActiveScene(&scene);
    }

    SDL_DestroyTexture(target);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
