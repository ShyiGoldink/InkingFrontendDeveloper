#include <ink/ui/HitTable.h>

#include <algorithm>
#include <cmath>

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

/// 落在第几格（终点方向）。矩形是半开区间，所以正好压在格子边界上时
/// 要退到左边一格，否则会多登记一格（虽然不影响正确性，但白占内存）。
int lastCell(float value, float origin, float cellSize, int limit) noexcept {
    if (value <= origin) {
        return 0;
    }
    const int index = static_cast<int>(std::ceil((value - origin) / cellSize)) - 1;
    return std::clamp(index, 0, limit - 1);
}

}  // namespace

void HitTable::clear() {
    _entries.clear();
    _offsets.clear();
    _candidates.clear();
    _bounds = Rect{};
    _cellSize = kDefaultCellSize;
    _cols = 0;
    _rows = 0;
}

void HitTable::build(std::vector<HitEntry> entries, float cellSize) {
    clear();

    // 空指针、零面积的条目直接筛掉：它们永远不可能是答案。
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [](const HitEntry& entry) {
                                     return entry.target == nullptr
                                            || !entry.rect.valid();
                                 }),
                  entries.end());
    if (entries.empty()) {
        return;
    }

    // 按 order 降序排一次，后面填候选时天然就是"上面的在前"。
    std::stable_sort(entries.begin(), entries.end(),
                     [](const HitEntry& lhs, const HitEntry& rhs) {
                         return lhs.order > rhs.order;
                     });

    Rect bounds = entries.front().rect;
    for (const HitEntry& entry : entries) {
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
    const std::size_t cellCount = static_cast<std::size_t>(cols) * static_cast<std::size_t>(rows);

    _entries = std::move(entries);
    _bounds = bounds;
    _cellSize = size;
    _cols = cols;
    _rows = rows;

    // 第一遍：数每格有几条候选。
    std::vector<std::uint32_t> counts(cellCount, 0u);
    for (const HitEntry& entry : _entries) {
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

    // 第三遍：按行偏移往里填，填的顺序就是 order 降序。
    _candidates.assign(_offsets.back(), 0u);
    std::vector<std::uint32_t> cursor(_offsets.begin(), _offsets.end() - 1);
    for (std::uint32_t index = 0; index < _entries.size(); ++index) {
        const HitEntry& entry = _entries[index];
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

bool HitTable::empty() const noexcept {
    return _entries.empty();
}

std::size_t HitTable::size() const noexcept {
    return _entries.size();
}

const Rect& HitTable::bounds() const noexcept {
    return _bounds;
}

float HitTable::cellSize() const noexcept {
    return _cellSize;
}

int HitTable::columns() const noexcept {
    return _cols;
}

int HitTable::rows() const noexcept {
    return _rows;
}

const std::vector<HitEntry>& HitTable::entries() const noexcept {
    return _entries;
}

std::size_t HitTable::candidateCount() const noexcept {
    return _candidates.size();
}

HitResult HitTable::hit(float x, float y) const noexcept {
    if (_entries.empty() || !_bounds.contains(x, y)) {
        return HitResult{};
    }

    const int cx = std::clamp(static_cast<int>((x - _bounds.x) / _cellSize), 0, _cols - 1);
    const int cy = std::clamp(static_cast<int>((y - _bounds.y) / _cellSize), 0, _rows - 1);
    const std::size_t cell =
        static_cast<std::size_t>(cy) * static_cast<std::size_t>(_cols)
        + static_cast<std::size_t>(cx);

    for (std::uint32_t cursor = _offsets[cell]; cursor < _offsets[cell + 1]; ++cursor) {
        const HitEntry& entry = _entries[_candidates[cursor]];
        if (entry.rect.contains(x, y)) {
            // 候选是 order 降序，第一个真命中的就是最上面那个。
            return HitResult{true, entry.target, entry.order};
        }
    }
    return HitResult{};
}

}  // namespace ink
