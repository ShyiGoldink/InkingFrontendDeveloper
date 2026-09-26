#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <core/InkRect.h>
#include <scene/InkEvent.h>

namespace ink {

class InkingScene;  // 表里只存指针，不把整棵场景树的头文件拖进来

/// 静态表里的一条：谁、占哪块矩形、在第几层。
struct StaticEntry {
    InkingScene* node = nullptr;
    Rect rect;
    int z = 0;                 ///< 显式 zindex：越大越靠上（docs/InputDesign.md §1）
    int order = 0;             ///< 同 z 时晚注册的在上
    bool nestedIndex = false;  ///< 这个结点自带专用查找（等宽等距），要递归问它
};

/// 静态层里的「洞」：动态子树不进表，由它自己每帧判（docs/InputDesign.md §10.5）。
///
/// 注意 rect 只是烘焙那一刻的快照（给日志和将来的「洞预筛」用）：动态子树
/// 会自己挪位置，所以判命中时不会拿这个矩形当准，而是现问节点。
struct DynamicHole {
    InkingScene* node = nullptr;
    Rect rect;
    int z = 0;
    int order = 0;
};

/**
 * 通用兜底结构：网格索引 + CSR 候选数组（docs/InputDesign.md §10.2）。
 *
 *   1. 建表时取所有条目的并集当包围盒，按 cellSize 切方格；
 *   2. 每条条目登记进它覆盖到的所有格子（一个矩形可以横跨多格）；
 *   3. 查表 = 一次除法定位格子，再顺着该格子的候选区间扫一遍。
 *
 * 候选按 z 降序填，所以扫到的第一个「真正包含该点且阻塞」的条目就是答案；
 * 格子只负责缩小范围，是否真的命中仍由矩形自己判定，所以不会邻格误命中。
 * 代价全在建表那一次，查询过程零分配、零哈希。
 */
class HitTable {
public:
    /// 默认格子边长（设计像素）。格子越小定位越快，建表时占的内存越多。
    static constexpr float kDefaultCellSize = 64.0f;
    /// 单轴格子数上限，防止极端尺寸下格子数爆炸。
    static constexpr int kMaxCellsPerAxis = 512;

    /// 建表。entries 可以乱序，内部会按 z 降序整理。
    void Build(std::vector<StaticEntry> entries, float cellSize = kDefaultCellSize);
    void Clear();

    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const Rect& Bounds() const noexcept;
    float CellSize() const noexcept;
    std::size_t CandidateCount() const noexcept;
    const std::vector<StaticEntry>& Entries() const noexcept;

    /// 查表：这个设计坐标上是谁。没东西时返回 Miss。
    HitTarget Query(float x, float y) const noexcept;

private:
    std::vector<StaticEntry> _entries;      ///< 按 z 降序
    std::vector<std::uint32_t> _offsets;    ///< CSR 行偏移，长度 = 格子数 + 1
    std::vector<std::uint32_t> _candidates; ///< CSR 列：格子 → 条目下标
    Rect _bounds;
    float _cellSize = kDefaultCellSize;
    int _cols = 0;
    int _rows = 0;
};

/**
 * 等宽等距容器的专用查找：纯算术，不建表（docs/InputDesign.md §10.4）。
 *
 * 菜单栏 / 工具条 / 分页条这种「等宽等距」的行，表本身是多余的：
 *
 *     index = (x - 行首 x) / (槽宽 + 槽间距)
 *
 * 一次除法定位槽位，再让槽位矩形自己判包含。生成器接手后，这一步会按配置里的
 * 布局类型直接生成；本原型手写这一种，用来验证「形状决定索引」这条路。
 */
class SlotIndex {
public:
    /// 建索引。要求槽位等宽、等高、等间距，否则返回 false（调用方退回网格表）。
    bool Build(std::vector<StaticEntry> slots);
    void Clear();

    bool Valid() const noexcept;
    std::size_t Size() const noexcept;
    float Gap() const noexcept;
    const Rect& Bounds() const noexcept;
    const std::vector<StaticEntry>& Slots() const noexcept;

    /// 这个坐标落在第几个槽位（槽位按 x 从小到大存，所以返回值就是它在行里的序号）；
    /// 落在槽间距、行外、或索引算不出时返回 -1。
    int IndexAt(float x, float y) const noexcept;

    /// 查询：按 z 降序扫槽位（槽就这么几个，线性就够）。
    HitTarget Query(float x, float y) const noexcept;

private:
    std::vector<StaticEntry> _slots;         ///< 按 x 升序：算术定位要用它
    std::vector<std::uint32_t> _queryOrder;  ///< 槽位下标，按 z 降序：查询要用它
    float _gap = 0.0f;
    Rect _bounds{};
    bool _valid = false;
};

}  // namespace ink
