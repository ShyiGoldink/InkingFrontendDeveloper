#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <ink/ui/InkRect.h>

namespace ink {

class Scene;  // 命中表只存指针，不把整棵场景树的头文件拖进来

/// 一条"坐标 → UI"的映射：谁、占哪块矩形、在第几层。
struct HitEntry {
    Scene* target = nullptr;
    Rect   rect;
    int    order = 0;  ///< 节点的全局声明序号：越大越靠上
};

/// 命中结果。
struct HitResult {
    bool   hit = false;
    Scene* target = nullptr;
    int    order = 0;

    explicit operator bool() const noexcept { return hit; }
};

/**
 * 静态场景的坐标 → UI 映射表。
 *
 * 做法是"网格索引 + CSR 候选数组"：
 *   1. 建表时取所有条目的并集当包围盒，按 cellSize 切成方格；
 *   2. 每条条目登记进它覆盖到的所有格子（一个矩形可以横跨多格）；
 *   3. 查表 = 一次除法定位格子，再顺着该格子的候选区间扫一遍，
 *      第一个"矩形真的包含这个点"的条目就是答案。
 *
 * 候选区间按 order 降序填，所以扫到的第一个命中天然是最上面那个；
 * 格子只是"这一格可能相关的条目"，是否真的命中仍由矩形自己说了算，
 * 因此不会出现邻格误命中。
 *
 * 代价全在建表那一次，查询过程零分配、零指针追逐、零哈希，
 * 只有两个连续的数组——这就是静态场景敢"强硬"的原因。
 */
class HitTable {
public:
    /// 默认格子边长（设计像素）。格子越小定位越快，建表时占的内存越多。
    static constexpr float kDefaultCellSize = 64.0f;
    /// 单轴格子数上限，防止极端尺寸下格子数爆炸。
    static constexpr int kMaxCellsPerAxis = 512;

    /// 建表。entries 可以乱序，内部会按 order 降序整理。
    void build(std::vector<HitEntry> entries, float cellSize = kDefaultCellSize);
    void clear();

    bool  empty() const noexcept;
    std::size_t size() const noexcept;
    const Rect& bounds() const noexcept;
    float cellSize() const noexcept;
    int   columns() const noexcept;
    int   rows() const noexcept;
    const std::vector<HitEntry>& entries() const noexcept;
    std::size_t candidateCount() const noexcept;  ///< CSR 里实际存了多少条候选登记

    /// 查表：这个设计坐标上是谁。没命中时返回 HitResult{hit = false}。
    HitResult hit(float x, float y) const noexcept;

private:
    std::vector<HitEntry>      _entries;   ///< 按 order 降序
    std::vector<std::uint32_t> _offsets;   ///< CSR 行偏移，长度 = 格子数 + 1
    std::vector<std::uint32_t> _candidates;///< CSR 列：格子 → 条目下标
    Rect  _bounds;
    float _cellSize = kDefaultCellSize;
    int   _cols = 0;
    int   _rows = 0;
};

}  // namespace ink
