// 场景 + 按钮的最小实例：一个自绘的头部菜单（最小化 / 最大化 / 关闭）。
//
// 场景树（括号里是场景类型，静态 = 会被烘成坐标映射表）：
//
//   示例根（Scene，静态）
//   ├─ 头部菜单（Scene 子类，静态）——画标题栏底板，空白处按住可以拖窗口
//   │   └─ 系统按钮组（Scene，静态）——纯分组，本身没有外观
//   │       ├─ 最小化（Button，Glyph::Minimize）
//   │       ├─ 最大化（Button，Glyph::Maximize / Restore，跟着窗口状态换图标）
//   │       └─ 关闭  （Button，Glyph::Close）
//   └─ 内容区（Scene，静态）
//       ├─ 工具条（Scene，静态）→ 三个示例按钮
//       ├─ 点击计数条（Scene 子类，静态）——外观每帧变，几何一动不动
//       └─ 漂浮方块（Scene 子类，动态）——位置每帧变，所以只能是动态场景
//
// 静态场景的点击是查表得到的（Scene::GetHitTable），点击坐标直接映射到
// 具体那个 UI 结点；动态场景没有表，只能每帧遍历。两种场景可以套在一起。

#include <ink/ink.h>
#include <window/InkingWindow.h>

namespace {

constexpr float kTitleBarHeight   = 72.0f;
constexpr float kSystemButtonSize = 44.0f;
constexpr float kSystemButtonGap  = 8.0f;
constexpr float kSystemButtonEdge = 16.0f;

// 一套手写的配色，等 CXXCSS + 代码生成器接手后这些都会变成配置项。
constexpr ink::Color kTitleBarFill{24, 26, 36, 255};
constexpr ink::Color kTitleBarEdge{58, 62, 78, 255};
constexpr ink::Color kAccent{86, 156, 214, 255};
constexpr ink::Color kPanelFill{30, 33, 44, 255};
constexpr ink::Color kCardFill{38, 42, 56, 255};
constexpr ink::Color kCardBorder{64, 70, 90, 255};
constexpr ink::Color kTextPlaceholder{126, 134, 158, 255};
constexpr ink::Color kTrackFill{44, 48, 62, 255};
constexpr ink::Color kProgressFill{86, 156, 214, 255};
constexpr ink::Color kFloaterRight{236, 158, 84, 255};
constexpr ink::Color kFloaterLeft{124, 196, 152, 255};

/// 字形系统还没实现，用几根色条把"这里本该有字"画出来。
void drawTextPlaceholder(ink::Canvas& canvas, const ink::Rect& box, int lines,
                         const ink::Color& color) {
    if (!box.valid() || lines <= 0) {
        return;
    }
    constexpr float lineHeight = 8.0f;
    constexpr float lineGap    = 8.0f;
    const float stride = lineHeight + lineGap;
    for (int index = 0; index < lines; ++index) {
        const float width = box.w * (index + 1 == lines ? 0.55f : 1.0f);
        canvas.fillRect(ink::Rect{box.x, box.y + static_cast<float>(index) * stride, width,
                                  lineHeight},
                        color);
    }
}

/// 头部菜单右起第 index 个系统按钮的位置（index 0 = 最右边那个）。
ink::Rect systemButtonRect(const ink::Rect& titleBar, int index) {
    const float groupWidth =
        3.0f * kSystemButtonSize + 2.0f * kSystemButtonGap;
    const ink::Rect group{titleBar.right() - kSystemButtonEdge - groupWidth, titleBar.y,
                          groupWidth, titleBar.h};
    return ink::rightSlot(group, index, kSystemButtonSize, kSystemButtonSize,
                          kSystemButtonGap, 0.0f);
}

/// 带占位标签的按钮：真字形到位之前，标签先用色条表示。
class LabelButton : public ink::Button {
public:
    using Button::Button;

    void onDraw(ink::Canvas& canvas) const override {
        Button::onDraw(canvas);
        const ink::Rect box = GetRect().inset(GetRect().w * 0.22f, GetRect().h * 0.38f);
        canvas.fillRect(box, kTextPlaceholder);
    }
};

/**
 * 头部菜单。
 *
 * 它自己是一个静态场景，里面套了一个"系统按钮组"场景，按钮组里才是三个
 * 按钮——头部菜单这种"一块底板 + 一组固定按钮"的东西，用场景嵌套表达最省事：
 * 底板管拖动和底色，按钮组管按钮怎么摆，各管各的。
 */
class TitleBarScene : public ink::Scene {
public:
    TitleBarScene()
        : Scene("头部菜单") {
        SetRect(ink::Rect{0.0f, 0.0f, static_cast<float>(inking::kDesignWidth),
                          kTitleBarHeight});

        const ink::Rect bar = GetRect();
        _group = &make<ink::Scene>("系统按钮组");
        _group->SetRect(ink::Rect{bar.right() - kSystemButtonEdge
                                      - (3.0f * kSystemButtonSize + 2.0f * kSystemButtonGap),
                                  bar.y, 3.0f * kSystemButtonSize + 2.0f * kSystemButtonGap,
                                  bar.h});

        // 右起第一、二、三个：关闭、最大化、最小化。
        _close = makeSystemButton(*_group, 0, "关闭按钮", ink::Glyph::Close);
        // 关闭按钮得真正把窗口关掉：场景层只提要求，窗口层去执行。
        _close->action = [](ink::Button& button) {
            INK_LOG_INFO("场景示例", "点击：" + button.GetLabel());
            button.requestWindowCommand(ink::WindowCommand::Close);
        };

        _maximize = makeSystemButton(*_group, 1, "最大化按钮", ink::Glyph::Maximize);
        _maximize->action = [](ink::Button& button) {
            INK_LOG_INFO("场景示例", "点击：" + button.GetLabel());
            button.requestWindowCommand(ink::WindowCommand::ToggleMaximize);
        };

        _minimize = makeSystemButton(*_group, 2, "最小化按钮", ink::Glyph::Minimize);
        _minimize->action = [](ink::Button& button) {
            INK_LOG_INFO("场景示例", "点击：" + button.GetLabel());
            button.requestWindowCommand(ink::WindowCommand::Minimize);
        };
    }

    void onDraw(ink::Canvas& canvas) const override {
        const ink::Rect bar = GetRect();
        canvas.fillRect(bar, kTitleBarFill);
        canvas.fillRect(ink::Rect{bar.x, bar.bottom() - 1.0f, bar.w, 1.0f}, kTitleBarEdge);

        // 名牌：一个强调色方块 + 两行占位文字。
        canvas.fillRect(ink::Rect{bar.x + 24.0f, bar.y + 22.0f, 28.0f, 28.0f}, kAccent);
        drawTextPlaceholder(canvas, ink::Rect{bar.x + 64.0f, bar.y + 26.0f, 180.0f, 20.0f}, 2,
                            kTextPlaceholder);
    }

    /// 空白处按住 = 拖窗口（无边框窗口没有系统标题栏，得自己来）。
    bool onPointerDown(ink::PointerEvent& event) override {
        event.handled = true;
        requestWindowCommand(ink::WindowCommand::BeginDrag);
        return true;
    }

    /// 窗口最大化状态变了：把"最大化"换成"还原"图标。
    void onWindowStateChanged(bool maximized) override {
        Scene::onWindowStateChanged(maximized);
        if (_maximize != nullptr) {
            _maximize->SetGlyph(maximized ? ink::Glyph::Restore : ink::Glyph::Maximize);
        }
    }

private:
    ink::Button* makeSystemButton(ink::Scene& group, int slot, const char* name,
                                  ink::Glyph glyph) {
        ink::Button* button = &group.make<ink::Button>(name, "", glyph);
        button->SetRect(systemButtonRect(GetRect(), slot));
        button->SetFill(ink::kTransparent);
        button->SetBorder(ink::kTransparent);
        return button;
    }

    ink::Scene*  _group = nullptr;
    ink::Button* _minimize = nullptr;
    ink::Button* _maximize = nullptr;
    ink::Button* _close = nullptr;
};

/// 点击计数条：几何一动不动，只有里面的填充长度跟着点击数走。
/// 所以它一直是静态场景，父场景的命中表一次都不需要重烘焙。
class CounterBarScene : public ink::Scene {
public:
    CounterBarScene() : Scene("点击计数条") {}

    void AddHit() {
        ++_count;
    }
    void Reset() {
        _count = 0;
    }

    void onDraw(ink::Canvas& canvas) const override {
        const ink::Rect track = GetRect();
        canvas.fillRect(track, kTrackFill);
        constexpr float maxCount = 12.0f;
        const float filled = _count > maxCount ? 1.0f : static_cast<float>(_count) / maxCount;
        if (filled > 0.0f) {
            canvas.fillRect(ink::Rect{track.x, track.y, track.w * filled, track.h},
                            kProgressFill);
        }
        canvas.strokeRect(track, kCardBorder, 1.0f);
    }

private:
    int _count = 0;
};

/**
 * 漂浮方块：位置每帧都在变，所以只能是动态场景。
 *
 * 静态场景的表一旦烘好就把位置冻住了，装不下会动的东西；
 * 动态场景不进表，父场景的映射表里给它留一个"洞"，每帧现问现答。
 */
class FloaterScene : public ink::Scene {
public:
    FloaterScene() : Scene("漂浮方块", /*dynamic=*/true) {}

    void onTick(float deltaSeconds) override {
        constexpr float speed = 0.22f;
        _phase += deltaSeconds * speed * static_cast<float>(_direction);
        if (_phase <= 0.0f) {
            _phase = 0.0f;
            _direction = 1;
        } else if (_phase >= 1.0f) {
            _phase = 1.0f;
            _direction = -1;
        }

        constexpr float trackX = 1240.0f;
        constexpr float trackY = 640.0f;
        constexpr float trackW = 560.0f;
        constexpr float size = 88.0f;
        SetRect(ink::Rect{trackX + _phase * (trackW - size), trackY, size, size});
    }

    void onDraw(ink::Canvas& canvas) const override {
        canvas.fillRect(GetRect(), _direction > 0 ? kFloaterRight : kFloaterLeft);
        canvas.strokeRect(GetRect(), kCardBorder, 2.0f);
    }

    bool onClick(ink::PointerEvent& event) override {
        event.handled = true;
        _direction = -_direction;
        INK_LOG_INFO("场景示例", "点击：漂浮方块（换个方向）");
        return true;
    }

private:
    float _phase = 0.0f;
    int   _direction = 1;
};

/// 内容区的底板：只画一层卡片，点击落在这里就到此为止。
class ContentScene : public ink::Scene {
public:
    ContentScene() : Scene("内容区") {}

    void onDraw(ink::Canvas& canvas) const override {
        const ink::Rect box = GetRect();
        canvas.fillRect(box, kPanelFill);
        canvas.fillRect(ink::Rect{box.x + 32.0f, box.y + 40.0f, box.w - 64.0f, 1.0f},
                        kCardBorder);
    }
};

}  // namespace

int main() {
    INK_LOG_INFO("场景示例", "启动：场景 + 按钮实例");

    // 根场景：整页就是它。名字随便起，Show() 按名字取。
    ink::Scene root("场景示例");
    root.SetRect(ink::Rect{0.0f, 0.0f, static_cast<float>(inking::kDesignWidth),
                           static_cast<float>(inking::kDesignHeight)});

    // 头部菜单：场景里套场景，底板与按钮组各管各的。
    root.make<TitleBarScene>();

    // 内容区
    ContentScene& content = root.make<ContentScene>();
    content.SetRect(ink::Rect{0.0f, kTitleBarHeight, static_cast<float>(inking::kDesignWidth),
                              static_cast<float>(inking::kDesignHeight) - kTitleBarHeight});

    ink::Scene& toolbar = content.make<ink::Scene>("工具条");
    toolbar.SetRect(ink::Rect{64.0f, 160.0f, 720.0f, 96.0f});

    CounterBarScene& counter = content.make<CounterBarScene>();
    counter.SetRect(ink::Rect{64.0f, 300.0f, 720.0f, 24.0f});

    content.make<FloaterScene>();

    // 工具条上的三个按钮：纯回调，不派生新类。
    const ink::Rect slot0 = ink::childRect(toolbar.GetRect(), 0.0f, 0.0f, 200.0f, 96.0f);
    const ink::Rect slot1 = ink::childRect(toolbar.GetRect(), 236.0f, 0.0f, 200.0f, 96.0f);
    const ink::Rect slot2 = ink::childRect(toolbar.GetRect(), 472.0f, 0.0f, 200.0f, 96.0f);

    LabelButton& logButton = toolbar.make<LabelButton>("日志按钮", "写日志");
    logButton.SetRect(slot0);
    logButton.SetFill(kCardFill);
    logButton.SetBorder(kCardBorder);
    logButton.action = [&counter](ink::Button&) {
        counter.AddHit();
        INK_LOG_INFO("场景示例", "点击：写日志");
    };

    LabelButton& messageButton = toolbar.make<LabelButton>("消息按钮", "发消息");
    messageButton.SetRect(slot1);
    messageButton.SetFill(kCardFill);
    messageButton.SetBorder(kCardBorder);
    messageButton.action = [&counter](ink::Button&) {
        counter.AddHit();
        // 消息进队列，由窗口层每帧倒进 Log.html。
        INK_MESSAGE_PASS("场景示例：按钮发出一条消息");
    };

    LabelButton& resetButton = toolbar.make<LabelButton>("重置按钮", "重置计数");
    resetButton.SetRect(slot2);
    resetButton.SetFill(kCardFill);
    resetButton.SetBorder(kCardBorder);
    resetButton.action = [&counter](ink::Button&) {
        counter.Reset();
        INK_LOG_INFO("场景示例", "点击：重置计数");
    };

    ink::InkingWindow& window = ink::InkingWindow::Instance();
    window.setBorderless(true);   // 标题栏我们自己画
    window.Show(root.GetName());  // 阻塞到窗口关闭

    INK_LOG_INFO("场景示例", "退出");
    return 0;
}
