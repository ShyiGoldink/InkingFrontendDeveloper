#pragma once

// InkingFrontendDeveloper 的公共入口头文件。
// 引入这一个头文件即可拿到版本信息、日志、消息队列、任务队列，
// 以及场景 / 按钮那一套 UI 骨架（窗口入口在 <window/InkingWindow.h>）。

#include <string>

#include <ink/basic/InkLog.h>
#include <ink/thread/TaskQueue.h>
#include <ink/ui/Button.h>
#include <ink/ui/Canvas.h>
#include <ink/ui/HitTable.h>
#include <ink/ui/InkColor.h>
#include <ink/ui/InkRect.h>
#include <ink/ui/InputRouter.h>
#include <ink/ui/MessageQueue.h>
#include <ink/ui/Scene.h>
#include <ink/ui/SceneRegistry.h>
#include <ink/ui/UiEvent.h>

namespace ink {

inline constexpr const char* kProjectName = "InkingFrontendDeveloper";
inline constexpr const char* kVersion     = "0.1.0";

/// 返回当前构建链接到的 SDL3 版本字符串，例如 "3.4.16"。
std::string sdl3_version();

}  // namespace ink
