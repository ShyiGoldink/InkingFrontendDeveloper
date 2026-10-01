// SDL3_ttf 接入自检：确认它真的链接得上、能打开字体、能量出文字尺寸。
//
// 字体本身没法"编进源码"（几百 KB 到几 MB），所以这里用一个**候选路径列表**：
// 机器上哪个存在就用哪个。找不到就**跳过**而不是失败——但这会打印一行提示，
// 免得"没验"被当成"验过了"。
//
// 注意：本自检**不涉及渲染**（不需要窗口）。文字真正画出来对不对属于
// "要不要开窗看一眼"那一档，按 docs/AGENTS.md §4 第 4 条交给用户确认。

#include <ink/ink.h>

#include <SDL3/SDL.h>

#include <cstdio>
#include <string>
#include <vector>

#if !defined(INK_HAS_SDL3_TTF)
#error "本文件只在 INK_HAS_SDL3_TTF 打开时才该参与编译"
#endif

#include <SDL3_ttf/SDL_ttf.h>

namespace {

int gFailed = 0;

void check(bool condition, const std::string& what) {
    std::printf("%s %s\n", condition ? "[通过]" : "[失败]", what.c_str());
    if (!condition) {
        ++gFailed;
    }
}

/**
 * 找一份能用的字体。
 *
 * 优先 CJK 字体（msyh = 微软雅黑），因为本项目是中文项目，
 * 拿一份没有汉字的字体测不出真正关心的东西。
 */
std::string findFontPath() {
    const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",      // 微软雅黑（含 CJK）
        "C:/Windows/Fonts/msyhbd.ttc",
        "C:/Windows/Fonts/simhei.ttf",    // 黑体
        "C:/Windows/Fonts/arial.ttf",     // 兜底：只有拉丁字符
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/System/Library/Fonts/Helvetica.ttc",
    };

    for (const char* path : candidates) {
        if (SDL_GetPathInfo(path, nullptr)) {
            return path;
        }
    }
    return {};
}

}  // namespace

int main() {
    std::printf("SDL3_ttf 接入自检（SDL3 %s）\n", ink::sdl3_version().c_str());

    if (!SDL_Init(0)) {
        std::printf("[失败] SDL_Init 失败：%s\n", SDL_GetError());
        return 1;
    }

    // ---- 版本探针 ----
    {
        const int version = TTF_Version();
        check(SDL_VERSIONNUM_MAJOR(version) == 3, "SDL3_ttf 主版本是 3");
        std::printf("      SDL3_ttf 版本 %d.%d.%d\n",
                    SDL_VERSIONNUM_MAJOR(version),
                    SDL_VERSIONNUM_MINOR(version),
                    SDL_VERSIONNUM_MICRO(version));
    }

    check(TTF_Init(), "TTF_Init 成功");

    // ---- 字体探针 ----
    const std::string fontPath = findFontPath();
    if (fontPath.empty()) {
        std::printf("[跳过] 这台机器上没找到候选字体，字体相关检查全部跳过\n");
        std::printf("       （这不是失败，但也**没有验过**——装一份字体再跑）\n");
        TTF_Quit();
        SDL_Quit();
        std::printf("失败项：%d\n", gFailed);
        return gFailed == 0 ? 0 : 1;
    }
    std::printf("      使用字体：%s\n", fontPath.c_str());

    TTF_Font* font = TTF_OpenFont(fontPath.c_str(), 24.0f);
    check(font != nullptr, "TTF_OpenFont 打开字体");
    if (font == nullptr) {
        std::printf("[失败] TTF_OpenFont：%s\n", SDL_GetError());
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // 字体自带的行高应当是个正数
    check(TTF_GetFontHeight(font) > 0, "字体行高为正");

    // ---- 字度量探针：能算出尺寸，说明字形真的被解析了 ----
    {
        int width = 0;
        int height = 0;
        check(TTF_GetStringSize(font, "Hello", 5, &width, &height),
              "TTF_GetStringSize 量出 \"Hello\" 的尺寸");
        check(width > 0 && height > 0,
              "尺寸为正（说明真的走了解析，不是空壳）");
        std::printf("      \"Hello\" = %dx%d\n", width, height);

        int cjkWidth = 0;
        int cjkHeight = 0;
        const char* cjk = "按钮";
        check(TTF_GetStringSize(font, cjk, 6, &cjkWidth, &cjkHeight),
              "量出中文 \"按钮\" 的尺寸");
        std::printf("      \"按钮\" = %dx%d\n", cjkWidth, cjkHeight);

        // 空串应当是 0 宽，而不是报错或崩溃
        int emptyWidth = 0;
        int emptyHeight = 0;
        check(TTF_GetStringSize(font, "", 0, &emptyWidth, &emptyHeight),
              "空串也能安全地量尺寸");
        check(emptyWidth == 0, "空串宽度为 0");
    }

    // ---- 字形覆盖探针：字体里到底有没有那个字 ----
    {
        check(TTF_FontHasGlyph(font, 'A'), "字体里有 'A' 的字形");

        // 中文项目最关心的一条：这份字体到底支不支持汉字
        const bool hasCjk = TTF_FontHasGlyph(font, 0x4E2D);  // '中'
        std::printf("      字体包含汉字「中」：%s\n", hasCjk ? "是" : "否");
        if (fontPath.find("msyh") != std::string::npos
            || fontPath.find("simhei") != std::string::npos) {
            check(hasCjk, "CJK 字体确实带汉字字形");
        }

        // 一个几乎不可能被收录的码位，用来确认这个函数不是恒真
        check(!TTF_FontHasGlyph(font, 0x10FFFD),
              "生僻码位没有字形（说明该函数不是恒真）");
    }

    TTF_CloseFont(font);
    TTF_Quit();
    SDL_Quit();

    std::printf("失败项：%d\n", gFailed);
    return gFailed == 0 ? 0 : 1;
}
