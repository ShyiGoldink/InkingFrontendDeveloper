#pragma once

// CXXCSS → C++ 的发射器：把加载好的配置写成两个文件。
//
// 产物固定两个文件，名字固定：
//   <headerName>      常量与登记入口的声明（`inline constexpr` 常量放这里，
//                     头文件可以直接被业务代码 include）
//   button_register.cpp   登记实现 + 一个启动期就位的静态对象
//
// 为什么在头文件里放 `inline constexpr` 常量、而不是生成一个"名字字符串表"：
// 用户代码应该写 `ink::cxxcss::kNormalButtonName`，拼错名字在**编译期**就红；
// 字符串表要等到运行期 Find 返回 nullptr 才知道。

#include "Cxxcss.h"

#include <string>
#include <vector>

namespace inkgen {

struct EmitOptions {
    /// 产物目录（不存在会创建）。约定只指向 build 目录，不写源码树。
    std::string outDir;
    /// 生成代码的命名空间，默认 `ink::cxxcss`。
    std::string nameSpace = "ink::cxxcss";
    /// 生成的头文件名，默认 `button_service.h`。
    /// 用 `#include <inkgen/button_service.h>` 这种带前缀的写法，避免撞名。
    std::string headerName = "button_service.h";
};

struct EmitResult {
    bool ok = false;
    std::string error;
    std::string headerPath;
    std::string sourcePath;
};

/**
 * @brief 把配置写成头文件 + 源文件。
 *
 * 调用方保证 `buttons` 里的名字已经唯一且校验过（`LoadButtonConfigs` 负责）。
 */
EmitResult Emit(const std::vector<ButtonConfig>& buttons,
                const EmitOptions& options);

}  // namespace inkgen
