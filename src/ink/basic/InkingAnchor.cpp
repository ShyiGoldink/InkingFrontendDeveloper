#include <ink/basic/InkingAnchor.h>

namespace ink {

// ---------------------------------------------------------------------------
// 构造
// ---------------------------------------------------------------------------

InkingAnchor::InkingAnchor(InkingAnchor* parent, const std::string& name)
    : _name(name), _parent(parent) {}

InkingAnchor::InkingAnchor(InkingAnchor* parent, const AnchorData& data)
    : _name(),
      _selfAnchor(data.selfAnchor),
      _traceAnchor(data.traceAnchor),
      _offsetX(data.offsetX),
      _offsetY(data.offsetY),
      _width(data.width > 0 ? data.width : 0),
      _height(data.height > 0 ? data.height : 0),
      _zIndex(data.zIndex),
      _parent(parent) {}

// ---------------------------------------------------------------------------
// 只读查询
// ---------------------------------------------------------------------------

const std::string& InkingAnchor::GetName() const noexcept {
    return _name;
}

InkingAnchor* InkingAnchor::GetParent() const noexcept {
    return _parent;
}

const Anchor& InkingAnchor::GetSelfAnchor() const noexcept {
    return _selfAnchor;
}

const Anchor& InkingAnchor::GetTraceAnchor() const noexcept {
    return _traceAnchor;
}

int InkingAnchor::GetWidth() const noexcept {
    return _width;
}

int InkingAnchor::GetHeight() const noexcept {
    return _height;
}

float InkingAnchor::GetOffsetX() const noexcept {
    return _offsetX;
}

float InkingAnchor::GetOffsetY() const noexcept {
    return _offsetY;
}

// 自己矩形里的「自身锚点」那个点，落在父级矩形里的「上级锚点」那个点上：
//   parentW * traceAnchor.x == GetX() + ownW * selfAnchor.x
float InkingAnchor::GetX() const noexcept {
    if (_parent == nullptr) {
        return _offsetX;
    }
    return static_cast<float>(_parent->_width) * _traceAnchor.x
         - static_cast<float>(_width) * _selfAnchor.x
         + _offsetX;
}

float InkingAnchor::GetY() const noexcept {
    if (_parent == nullptr) {
        return _offsetY;
    }
    return static_cast<float>(_parent->_height) * _traceAnchor.y
         - static_cast<float>(_height) * _selfAnchor.y
         + _offsetY;
}

float InkingAnchor::GetAbsX() const noexcept {
    if (_parent == nullptr) {
        return _offsetX;
    }
    return _parent->GetAbsX() + GetX();
}

float InkingAnchor::GetAbsY() const noexcept {
    if (_parent == nullptr) {
        return _offsetY;
    }
    return _parent->GetAbsY() + GetY();
}

int InkingAnchor::GetZIndex() const noexcept {
    return _zIndex;
}

int InkingAnchor::GetRegisterOrder() const noexcept {
    return _registerOrder;
}

float InkingAnchor::GetMagnification() const noexcept {
    return _magnification;
}

float InkingAnchor::GetDeviceWidth() const noexcept {
    return static_cast<float>(_width) * _magnification;
}

float InkingAnchor::GetDeviceHeight() const noexcept {
    return static_cast<float>(_height) * _magnification;
}

bool InkingAnchor::IsDirty() const noexcept {
    return _dirty;
}

void InkingAnchor::ClearDirty() noexcept {
    _dirty = false;
}

// ---------------------------------------------------------------------------
// 层级
// ---------------------------------------------------------------------------

bool InkingAnchor::IsAbove(const InkingAnchor& a,
                           const InkingAnchor& b) noexcept {
    if (a._zIndex != b._zIndex) {
        return a._zIndex > b._zIndex;
    }
    return a._registerOrder > b._registerOrder;
}

void InkingAnchor::MarkDirty() noexcept {
    _dirty = true;
    onDirty();
}

// ---------------------------------------------------------------------------
// [final] 写入口
// ---------------------------------------------------------------------------

bool InkingAnchor::Resize(int width, int height) noexcept {
    const bool changeWidth =
        (width != InkingResize::none) && (width >= 0) && (width != _width);
    const bool changeHeight =
        (height != InkingResize::none) && (height >= 0) && (height != _height);

    if (!changeWidth && !changeHeight) {
        return false;
    }
    if (changeWidth) {
        _width = width;
    }
    if (changeHeight) {
        _height = height;
    }

    MarkDirty();
    onSizeChanged();
    return true;
}

bool InkingAnchor::ChangeSelfAnchor(const Anchor& anchor) noexcept {
    if (_selfAnchor.x == anchor.x && _selfAnchor.y == anchor.y) {
        return false;
    }
    _selfAnchor = anchor;
    MarkDirty();
    onSelfAnchorChanged();
    return true;
}

bool InkingAnchor::ChangeTraceAnchor(const Anchor& anchor) noexcept {
    if (_traceAnchor.x == anchor.x && _traceAnchor.y == anchor.y) {
        return false;
    }
    _traceAnchor = anchor;
    MarkDirty();
    onTraceAnchorChanged();
    return true;
}

bool InkingAnchor::ChangeOffset(float offsetX, float offsetY) noexcept {
    if (_offsetX == offsetX && _offsetY == offsetY) {
        return false;
    }
    _offsetX = offsetX;
    _offsetY = offsetY;
    MarkDirty();
    onOffsetChanged();
    return true;
}

bool InkingAnchor::ChangeZIndex(int zIndex) noexcept {
    if (_zIndex == zIndex) {
        return false;
    }
    _zIndex = zIndex;
    MarkDirty();
    onZIndexChanged(zIndex);
    return true;
}

bool InkingAnchor::SetParent(InkingAnchor* parent) noexcept {
    if (_parent == parent) {
        return false;
    }
    _parent = parent;
    MarkDirty();
    onParentChanged(parent);
    return true;
}

void InkingAnchor::SetMagnification(float magnification) noexcept {
    // 只影响绘制换算，不影响标脏
    if (magnification > 0.0f) {
        _magnification = magnification;
    }
}

void InkingAnchor::SetRegisterOrder(int order) noexcept {
    _registerOrder = order;
}

// ---------------------------------------------------------------------------
// [√] 可重写钩子：基类里都是空的，子类按需重写
// ---------------------------------------------------------------------------

void InkingAnchor::onSizeChanged() {}
void InkingAnchor::onSelfAnchorChanged() {}
void InkingAnchor::onTraceAnchorChanged() {}
void InkingAnchor::onOffsetChanged() {}
void InkingAnchor::onZIndexChanged(int /*zIndex*/) {}
void InkingAnchor::onParentChanged(InkingAnchor* /*parent*/) {}
void InkingAnchor::onDirty() {}

}  // namespace ink
