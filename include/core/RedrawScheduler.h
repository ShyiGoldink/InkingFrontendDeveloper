#pragma once

#include <cstdint>

namespace ink {

/**
 * 重绘方案（docs 里没有这一篇）。
 *
 * 输入那一套是「isDirty + query」，重绘这一套用**同样的形状**，但**独立实现**：
 * 两边的脏标记各管各的，谁也不替谁决定。区别只在"标脏之后干什么"——
 *
 *   输入：鼠标移动 / 几何 / 层级 / 可见性 → 命中结果可能过期 → 重新 query
 *   重绘：外观 / 几何 / 层级 / 可见性   → 像素可能过期   → 重新画一帧
 *
 * 谁标脏：
 *   · 几何 / 可见性 / 层级由写入口内部顺带标一次（框架保证，漏不了）；
 *   · 颜色、文字、悬停、按下这些**只影响重绘、不影响命中**的属性，由写入口
 *     自己明确调 makeDirty()——不做自动分析，调了就重画。
 *
 * 生命周期和输入那套一样带帧计数：标脏后连续 kTailFrames 帧没人再标脏就停画。
 * 静止时整帧不画也不 present（窗口层照 BeginFrame() 的返回值决定）。
 *
 * 用法（窗口层每帧）：
 *   if (RedrawScheduler::BeginFrame()) { canvas.clear(...); root.Draw(canvas); present(); }
 *   RedrawScheduler::EndFrame();
 */
class RedrawScheduler {
public:
    /// 连续多少帧没人再标脏就停下。取 2~3（和输入那套同一个理由：尾巴几帧白算无所谓）。
    static constexpr int kTailFrames = 2;

    /// 需要重绘。外观属性的写入口明确调这个。
    static void MakeDirty() noexcept;

    /// 这帧要不要画。窗口层每帧开头问一次，顺带记一笔账。
    static bool BeginFrame() noexcept;

    /// 帧末收尾：帧计数递减，到点清脏。
    static void EndFrame() noexcept;

    static bool Dirty() noexcept;
    static int FramesLeft() noexcept;

    /// 清回初始状态：脏 + 满帧计数，用来让下一帧一定画一次。
    static void Reset() noexcept;

    /// 自检与日志用：标脏次数 / 画了的帧数 / 跳过的帧数。
    static std::uint64_t DirtyCount() noexcept;
    static std::uint64_t PaintedFrames() noexcept;
    static std::uint64_t SkippedFrames() noexcept;
};

}  // namespace ink
