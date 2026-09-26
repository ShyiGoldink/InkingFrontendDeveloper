#include <core/RedrawScheduler.h>

namespace ink {

namespace {

/// 重绘方案的全部状态。
///
/// 单窗口 + 单 UI 线程，一份全局状态就够（和 MessageQueue / TaskQueue 一个路子）。
/// 它和输入那份状态**没有共享字段**：输入脏了不该顺手把重绘标脏，反之亦然。
struct State {
    bool dirty = true;                       ///< 首帧一定要画一次
    int framesLeft = RedrawScheduler::kTailFrames;
    std::uint64_t dirtyCount = 0;
    std::uint64_t painted = 0;
    std::uint64_t skipped = 0;
};

State& state() {
    static State instance;
    return instance;
}

}  // namespace

void RedrawScheduler::MakeDirty() noexcept {
    State& s = state();
    s.dirty = true;
    s.framesLeft = kTailFrames;
    ++s.dirtyCount;
}

bool RedrawScheduler::BeginFrame() noexcept {
    State& s = state();
    if (!s.dirty) {
        ++s.skipped;
        return false;
    }
    ++s.painted;
    return true;
}

void RedrawScheduler::EndFrame() noexcept {
    State& s = state();
    if (!s.dirty) {
        return;
    }
    if (--s.framesLeft <= 0) {
        s.dirty = false;
        s.framesLeft = 0;
    }
}

bool RedrawScheduler::Dirty() noexcept {
    return state().dirty;
}

int RedrawScheduler::FramesLeft() noexcept {
    return state().framesLeft;
}

void RedrawScheduler::Reset() noexcept {
    state() = State{};
}

std::uint64_t RedrawScheduler::DirtyCount() noexcept {
    return state().dirtyCount;
}

std::uint64_t RedrawScheduler::PaintedFrames() noexcept {
    return state().painted;
}

std::uint64_t RedrawScheduler::SkippedFrames() noexcept {
    return state().skipped;
}

}  // namespace ink
