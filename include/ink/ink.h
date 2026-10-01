#pragma once

// InkingFrontendDeveloper 的公共入口头文件。
// 引入这一个头文件即可拿到版本信息、日志、消息队列与任务队列。
//
// 注意它**不含组件**（Button / Scene / Window …）：组件头各自带自己的依赖，
// 全都塞进来会让"只是想用日志"的翻译单元被迫拖上一整套锚点树。
// 组件的入口分别是：
//   <button/InkingStaticButton.h>  按钮
//   <scene/InkingScene.h>          场景
//   <window/InkingWindow.h>        窗口

#include <string>

#include <ink/basic/InkLog.h>
#include <ink/ui/MessageQueue.h>
#include <ink/thread/TaskQueue.h>

namespace ink {

inline constexpr const char* kProjectName = "InkingFrontendDeveloper";
inline constexpr const char* kVersion     = "0.1.0";

/// 返回当前构建链接到的 SDL3 版本字符串，例如 "3.4.16"。
std::string sdl3_version();

}  // namespace ink

