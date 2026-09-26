#include <scene/HitTable.h>

#include <algorithm>
#include <cmath>

#include <scene/InkingScene.h>

namespace ink {

namespace {

/// 落在第几格（起点方向）：坐标比原点还小时夹到第一格。
int firstCell(float value, float origin, float cellSize, int limit) noexcept {
    if (value <= origin) {
        return 0;
    }
    const int index = static_cast<int>(std::floor((value - origin) / cellSize));
    return std::clamp(index, 0, limit - 1);
}

/// 落在第几格（终点方向）。矩形是半开区间，正好压在格子边界上时要退到左边一格。
int lastCell(float value, float origin, float cellSize, int limit) noexcept {
    if (value <= origin) {
        return 0;
    }
    const int index = static_cast<int>(std::ceil((value - origin) / cellSize)) - 1;
    return std::clamp(index, 0, limit - 1);
}

/// (z, order) 谁在上面：先比 z，z 相同比注册顺序。
/// 语义和 InkingAnchor::IsAbove 一致，这里是它的「裸数字版」，给合并与排序用。
bool zAbove(int z, int order, int otherZ, int otherOrder) noexcept {
    if (z != otherZ) {
        return z > otherZ;
    }
    return order > otherOrder;
}

/**
 * 候选按 z 降序扫一遍，落成三态（docs/InputDesign.md §6）。
 *
 *   · 让过的条目记下来继续往下找；
 *   · 阻塞的条目直接胜出；
 *   · 自带专用查找的结点（等宽等距容器）递归问它自己的表——父表里只记一条，
 *     但答案从它内部来。
 *
 * 网格表和等宽等距索引共用这一份逻辑，所以「谁在上面」只有一处定义。
 */
template <typename EntryAt>
HitTarget walkCandidates(std::size_t count, EntryAt entryAt, float x, float y) {
    HitTarget passThrough;

    for (std::size_t i = 0; i < count; ++i) {
        const StaticEntry& entry = entryAt(i);
        if (entry.node == nullptr || !entry.rect.contains(x, y)) {
            continue;
        }

        if (entry.nestedIndex) {
            const HitTarget inner = entry.node->Query(x, y);
            if (inner.Blocked()) {
                return inner;
            }
            if (inner.Found() && !passThrough.Found()) {
                passThrough = inner;
            }
            // 内部没挡住，这个容器自己还能兜底，继续往下判。
        }

        if (entry.node->GetHitPolicy() == HitPolicy::PassThrough) {
            if (!passThrough.Found()) {
                passThrough = HitTarget{HitState::PassThrough, entry.node, entry.z};
            }
            continue;
        }

        return HitTarget{HitState::Block, entry.node, entry.z};
    }

    return passThrough;
}

}  // namespace

// ---------------------------------------------------------------------------
// HitTable
// ---------------------------------------------------------------------------

void HitTable::Clear() {
    _entries.clear();
    _offsets.clear();
    _candidates.clear();
    _bounds = Rect{};
    _cellSize = kDefaultCellSize;
    _cols = 0;
    _rows = 0;
}

void HitTable::Build(std::vector<StaticEntry> entries, float cellSize) {
    Clear();

    // 空指针、零面积的条目直接筛掉：它们永远不可能是答案。
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const StaticEntry& entry) {
                                     return entry.node == nullptr || !entry.rect.valid();
                                 }),
                  entries.end());
    if (entries.empty()) {
        return;
    }

    // 按 z 降序排一次：(z, order) 大的在前，后面填候选时天然就是「上面的在前」。
    std::stable_sort(entries.begin(), entries.end(),
                     [](const StaticEntry& lhs, const StaticEntry& rhs) {
                         return zAbove(lhs.z, lhs.order, rhs.z, rhs.order);
                     });

    Rect bounds = entries.front().rect;
    for (const StaticEntry& entry : entries) {
        bounds = bounds.united(entry.rect);
    }

    float size = cellSize > 1.0f ? cellSize : 1.0f;
    const float longestSide = std::max(bounds.w, bounds.h);
    if (size > longestSide) {
        size = longestSide;  // 一个格子装得下整张表，就别切了
    }
    while (bounds.w / size > static_cast<float>(kMaxCellsPerAxis)
           || bounds.h / size > static_cast<float>(kMaxCellsPerAxis)) {
        size *= 2.0f;  // 格子太细就翻倍，把格子总数压回上限内
    }

    const int cols = std::max(1, static_cast<int>(std::ceil(bounds.w / size)));
    const int rows = std::max(1, static_cast<int>(std::ceil(bounds.h / size)));
    const std::size_t cellCount =
        static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);

    _entries = std::move(entries);
    _bounds = bounds;
    _cellSize = size;
    _cols = cols;
    _rows = rows;

    // 第一遍：数每格有几条候选。
    std::vector<std::uint32_t> counts(cellCount, 0u);
    for (const StaticEntry& entry : _entries) {
        const int x0 = firstCell(entry.rect.left(), bounds.x, size, cols);
        const int x1 = lastCell(entry.rect.right(), bounds.x, size, cols);
        const int y0 = firstCell(entry.rect.top(), bounds.y, size, rows);
        const int y1 = lastCell(entry.rect.bottom(), bounds.y, size, rows);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                ++counts[static_cast<std::size_t>(y) * static_cast<std::size_t>(cols)
                         + static_cast<std::size_t>(x)];
            }
        }
    }

    // 第二遍：前缀和 → CSR 行偏移。
    _offsets.assign(cellCount + 1, 0u);
    for (std::size_t i = 0; i < cellCount; ++i) {
        _offsets[i + 1] = _offsets[i] + counts[i];
    }

    // 第三遍：按行偏移往里填，填的顺序就是 z 降序。
    _candidates.assign(_offsets.back(), 0u);
    std::vector<std::uint32_t> cursor(_offsets.begin(), _offsets.end() - 1);
    for (std::uint32_t index = 0; index < _entries.size(); ++index) {
        const StaticEntry& entry = _entries[index];
        const int x0 = firstCell(entry.rect.left(), bounds.x, size, cols);
        const int x1 = lastCell(entry.rect.right(), bounds.x, size, cols);
        const int y0 = firstCell(entry.rect.top(), bounds.y, size, rows);
        const int y1 = lastCell(entry.rect.bottom(), bounds.y, size, rows);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const std::size_t cell =
                    static_cast<std::size_t>(y) * static_cast<std::size_t>(cols)
                    + static_cast<std::size_t>(x);
                _candidates[cursor[cell]++] = index;
            }
        }
    }
}

bool HitTable::Empty() const noexcept {
    return _entries.empty();
}

std::size_t HitTable::Size() const noexcept {
    return _entries.size();
}

const Rect& HitTable::Bounds() const noexcept {
    return _bounds;
}

float HitTable::CellSize() const noexcept {
    return _cellSize;
}

std::size_t HitTable::CandidateCount() const noexcept {
    return _candidates.size();
}

const std::vector<StaticEntry>& HitTable::Entries() const noexcept {
    return _entries;
}

HitTarget HitTable::Query(float x, float y) const noexcept {
    if (_entries.empty() || !_bounds.contains(x, y)) {
        return HitTarget{};
    }

    const int cx = std::clamp(static_cast<int>((x - _bounds.x) / _cellSize), 0, _cols - 1);
    const int cy = std::clamp(static_cast<int>((y - _bounds.y) / _cellSize), 0, _rows - 1);
    const std::size_t cell =
        static_cast<std::size_t>(cy) * static_cast<std::size_t>(_cols)
        + static_cast<std::size_t>(cx);

    const std::uint32_t begin = _offsets[cell];
    const std::uint32_t end = _offsets[cell + 1];
    return walkCandidates(static_cast<std::size_t>(end - begin),
                          [this, begin](std::size_t i) -> const StaticEntry& {
                              return _entries[_candidates[begin + i]];
                          },
                          x, y);
}

// ---------------------------------------------------------------------------
// SlotIndex
// ---------------------------------------------------------------------------

void SlotIndex::Clear() {
    _slots.clear();
    _queryOrder.clear();
    _gap = 0.0f;
    _bounds = Rect{};
    _valid = false;
}

bool SlotIndex::Build(std::vector<StaticEntry> slots) {
    Clear();

    slots.erase(std::remove_if(slots.begin(), slots.end(),
                               [](const StaticEntry& slot) {
                                   return slot.node == nullptr || !slot.rect.valid();
                               }),
                slots.end());
    if (slots.size() < 2) {
        return false;  // 只剩一个槽位，算术退化成「就它」，没必要
    }

    // 等宽、等高、等间距：只要有一条不满足就不要用这套（退回网格表）。
    const float width = slots.front().rect.w;
    const float height = slots.front().rect.h;
    float gap = -1.0f;
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const StaticEntry& slot = slots[i];
        if (slot.rect.w != width || slot.rect.h != height) {
            return false;
        }
        if (i == 0) {
            continue;
        }
        const float step = slot.rect.x - slots[i - 1].rect.x;
        const float stepGap = step - width;
        if (i == 1) {
            gap = stepGap;
        } else if (stepGap != gap) {
            return false;
        }
    }

    Rect bounds = slots.front().rect;
    for (const StaticEntry& slot : slots) {
        bounds = bounds.united(slot.rect);
    }

    // 行序（x 升序）：算术定位出来的 index 要能直接当下标用。
    std::stable_sort(slots.begin(), slots.end(),
                     [](const StaticEntry& lhs, const StaticEntry& rhs) {
                         return lhs.rect.x < rhs.rect.x;
                     });

    // 查询序（z 降序）：先问上面的那一槽。
    std::vector<std::uint32_t> queryOrder(slots.size());
    for (std::uint32_t i = 0; i < queryOrder.size(); ++i) {
        queryOrder[i] = i;
    }
    std::stable_sort(queryOrder.begin(), queryOrder.end(),
                     [&slots](std::uint32_t lhs, std::uint32_t rhs) {
                         const StaticEntry& a = slots[lhs];
                         const StaticEntry& b = slots[rhs];
                         return zAbove(a.z, a.order, b.z, b.order);
                     });

    _slots = std::move(slots);
    _queryOrder = std::move(queryOrder);
    _gap = gap;
    _bounds = bounds;
    _valid = true;
    return true;
}

bool SlotIndex::Valid() const noexcept {
    return _valid;
}

std::size_t SlotIndex::Size() const noexcept {
    return _slots.size();
}

float SlotIndex::Gap() const noexcept {
    return _gap;
}

const Rect& SlotIndex::Bounds() const noexcept {
    return _bounds;
}

const std::vector<StaticEntry>& SlotIndex::Slots() const noexcept {
    return _slots;
}

int SlotIndex::IndexAt(float x, float y) const noexcept {
    if (!_valid || !_bounds.contains(x, y)) {
        return -1;
    }

    // index = (x - 行首) / (槽宽 + 槽间距)：一次除法（docs/InputDesign.md §10.4）
    const float stride = _slots.front().rect.w + _gap;
    if (stride <= 0.0f) {
        return -1;
    }
    const int index = static_cast<int>(std::floor((x - _slots.front().rect.x) / stride));
    if (index < 0 || index >= static_cast<int>(_slots.size())) {
        return -1;
    }
    if (!_slots[static_cast<std::size_t>(index)].rect.contains(x, y)) {
        return -1;  // 落在槽间距里：这一行这里没有东西
    }
    return index;
}

HitTarget SlotIndex::Query(float x, float y) const noexcept {
    if (!_valid || !_bounds.contains(x, y)) {
        return HitTarget{};
    }
    return walkCandidates(
        _queryOrder.size(),
        [this](std::size_t i) -> const StaticEntry& {
            return _slots[_queryOrder[i]];
        },
        x, y);
}

}  // namespace ink
