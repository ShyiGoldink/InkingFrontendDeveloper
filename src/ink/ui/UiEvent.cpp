#include <ink/ui/UiEvent.h>

namespace ink {

const char* toString(WindowCommand command) noexcept {
    switch (command) {
        case WindowCommand::None:           return "无";
        case WindowCommand::Minimize:       return "最小化";
        case WindowCommand::Maximize:       return "最大化";
        case WindowCommand::ToggleMaximize: return "切换最大化";
        case WindowCommand::Restore:        return "还原";
        case WindowCommand::BeginDrag:      return "拖动窗口";
        case WindowCommand::Close:          return "关闭";
    }
    return "未知";
}

const char* toString(PointerAction action) noexcept {
    switch (action) {
        case PointerAction::Down:   return "按下";
        case PointerAction::Up:     return "抬起";
        case PointerAction::Enter:  return "进入";
        case PointerAction::Leave:  return "离开";
        case PointerAction::Cancel: return "取消";
    }
    return "未知";
}

}  // namespace ink
