#pragma once

// CXXCSS 的**加载 + 校验**：一个 json → 一份 ink::ButtonData。
//
// 这一层直接产出**运行期那个结构体**（`include/button/ButtonData.h`），不另立
// 一套"生成器自己的配置类型"。理由：两套类型意味着每个字段有两份定义、
// 两处校验、两条会分叉的演化路径——而它们表达的本来就是同一件事。
//
// 校验的调子按 CXXCSS.md §3.6 定：**报错停下，不是夹一下就过**。
// 生成器阶段的问题最容易查，放进运行期就是"跑起来了但样子不对"。
// 唯一的例外是形状的 radius 上限（那是形状本身的数学要求，运行期自己夹）。

#include "Json.h"

#include <button/ButtonData.h>

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace inkgen {

/// 一条校验错误：指到文件、行号、说清哪里不对。
struct CxxcssError {
    std::string file;
    int line = 0;
    std::string message;

    /// "CXXCSS/Button/x.json:12: 字段 \"foo\" 不认识" —— 编辑器能直接跳转的格式。
    std::string Format() const;
};

/// 一份加载成功的按钮配置。
struct ButtonConfig {
    std::string filePath;          ///< 来源文件，报错和日志要用
    std::string name;              ///< 配置里的 name（= ButtonLibrary 的查找键）
    /**
     * 生成出来的**类名**（`normalButton` → `NormalButton`）。
     *
     * name 不是合法 C++ 标识符时为空——那时生成不出类，只能按字符串名字使用。
     * 类名同时是"这个按钮的类型"和"拼错名字的编译期保险"：
     * 数据烘在类里，没有运行期查表。
     */
    std::string identifier;
    /**
     * 生成成**动态**按钮吗（json 里的 `"dynamic": true`）。
     *
     * 这一点只决定"生成出来的类继承谁"：静态继承 `ink::InkingStaticButton`、
     * 动态继承 `ink::InkingDynamicButton`（几何可以在运行期改，代价是不进命中表，
     * 指针由它自己在 Tick 里拉——见 include/button/InkingDynamicButton.h）。
     *
     * 它**不进运行期那份数据结构**（`ink::ButtonData` 里没有这个字段）：
     * 类型本身已经说明了它属于哪一档，再存一遍就是第二份真相，
     * 而第二份真相迟早会和第一份不一致。
     */
    bool dynamic = false;
    ink::ButtonData data{};        ///< 运行期那份配置

    /// 非致命提醒（例如 `img` / `svg` 还没落地）。不算错误，但必须让用户看见。
    std::vector<std::string> warnings;
};

/**
 * @brief 一次加载的结果：成功的配置 + 全部错误。
 *
 * 错误**不早退**：一次把文件里所有问题都列出来，用户改一轮就能全改完，
 * 而不是"改一条、再跑一次、又冒一条"。
 */
struct LoadResult {
    std::vector<ButtonConfig> buttons;
    std::vector<CxxcssError> errors;

    /**
     * 本次加载里每个文件认领的名字（名字 + 来源文件）。
     *
     * 不过滤"有没有通过校验"：一个文件可能因为别的问题被拒绝、因而不进
     * `buttons`，但它认领了某个名字这件事仍然成立——重名检查要看的就是这个。
     */
    std::vector<std::pair<std::string, std::string>> claimedNames;

    bool Ok() const { return errors.empty(); }
};

/// 加载并校验一个 `CXXCSS/Button/<名字>.json`。
LoadResult LoadButtonConfig(const std::string& path);

/**
 * @brief 加载一个文件，结果**追加**到 `result` 上。
 *
 * 批量加载必须走这个（而不是"每个文件各加载一份再合并"）：重名检查要看的是
 * **到目前为止这一批**认领了哪些名字，各自为政就会让"第二个文件撞上第一个"
 * 检不出来——那个 bug 真的踩过。
 */
void LoadButtonConfigInto(const std::string& path, LoadResult& result);

/**
 * @brief 加载一批文件；`*.example.json` 会被**跳过**（约定见 CXXCSS.md §1）。
 *
 * 显式传进来的 `.example.json` 会被跳过并记一条提示——批量收文件时它是正常的，
 * 但用户单独指着它说"生成这个"时，跳过比静默忽略更安全。
 */
LoadResult LoadButtonConfigs(const std::vector<std::string>& paths);

/// 文件名（不含目录、不含 `.json`）——用来核对 `name` 字段（CXXCSS.md §1 第 2 条）。
std::string FileStem(const std::string& path);

/// `"normalButton"` → `"kNormalButtonName"`。不是合法标识符时返回空串。
std::string MakeNameConstant(const std::string& name);

/// 名字是不是合法的 C++ 标识符（不以数字开头、只含字母数字下划线）。
bool IsCxxIdentifier(const std::string& name);

}  // namespace inkgen
