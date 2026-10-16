#include "gz/WinScrollbar2.h"

#include <algorithm>

#include "gz/RZDraw.h"

namespace gz {

namespace {
IWinImageSource* g_images = nullptr;
constexpr std::uint32_t kRepeatFirstDelay = 300;
constexpr std::uint32_t kRepeatPeriod = 70;
}  // namespace

IWinImageSource* WinImageSource() { return g_images; }
void SetWinImageSource(IWinImageSource* source) { g_images = source; }

cGZWinScrollbar2::cGZWinScrollbar2(cIGZWinMgr* mgr) : cGZWin(mgr) {   // @ 0x10069fd52
    flags_ &= ~0x8000u;
    cGZWinScrollbar2::SetArea(0, 0, 0x20, 0x60);
}

cGZWinScrollbar2::~cGZWinScrollbar2() {   // @ 0x10069fe9c
    timer_.Stop();
    for (cIGZBuffer** slot : {&thumb_, &container_, &arrows_})
        if (*slot) (*slot)->Release();
}

bool cGZWinScrollbar2::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10069ffa4
    if (iid == kGZIID_cIGZWinScrollbar2) {
        *out = static_cast<cIGZWinScrollbar2*>(this);
    } else if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cGZWin*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

bool cGZWinScrollbar2::Init() {   // @ 0x1006a002c
    if (!get_init_flag()) {
        timer_.SetCallbackFunction(&cGZWinScrollbar2::TimerCallback, this);
        IWinImageSource* source = WinImageSource();
        if (arrows_ == nullptr && container_ == nullptr && source) {
            const std::uint32_t* ids = kScrollbar2ImageIds[orientation_ == 1 ? 1 : 0];
            auto adopt = [](cIGZBuffer*& slot, cIGZBuffer* image) {
                if (slot != image) {
                    if (image) image->AddRef();
                    cIGZBuffer* previous = slot;
                    slot = image;
                    if (previous) previous->Release();
                }
            };
            adopt(arrows_, source->GetImage(ids[0]));
            adopt(container_, source->GetImage(ids[1]));
            adopt(thumb_, source->GetImage(ids[2]));
        }
        CalcImageSizes();
    }
    return cGZWin::Init();
}

bool cGZWinScrollbar2::Shutdown() {   // @ 0x1006a0484
    timer_.Stop();
    timer_.SetCallbackFunction(nullptr, this);
    for (cIGZBuffer** slot : {&arrows_, &container_, &thumb_}) {
        if (*slot) {
            cIGZBuffer* image = *slot;
            *slot = nullptr;
            image->Release();
        }
    }
    return cGZWin::Shutdown();
}

void cGZWinScrollbar2::TimerCallback(void* self) {   // @ 0x1006a0188
    auto* bar = static_cast<cGZWinScrollbar2*>(self);
    bar->timer_.SetPeriod(kRepeatPeriod);
    bar->PostMsg(bar, kGZMsgScrollbarRepeat, 0, 0, 0);
}

// Sizes derived from the skin: the arrows image holds two arrows side by side (along the bar) in four states
// stacked across it; the container has end caps; the thumb has the arrows' layout.
void cGZWinScrollbar2::CalcImageSizes() {   // @ 0x1006a01c8
    dirty_ &= 0xfe;
    minWidth_ = minHeight_ = 0;
    if (arrows_ == nullptr && container_ == nullptr && thumb_ == nullptr) {
        arrowLength_ = 0x12;
        thumbCap_ = 4;
        if (orientation_ == 0) { minWidth_ = 0x36; minHeight_ = 0x12; }
        else { minWidth_ = 0x12; minHeight_ = 0x36; }
        return;
    }
    const bool horizontal = orientation_ == 0;
    auto along = [&](cIGZBuffer* image) { return static_cast<std::int32_t>(horizontal ? image->Width() : image->Height()); };
    auto across = [&](cIGZBuffer* image) { return static_cast<std::int32_t>(horizontal ? image->Height() : image->Width()); };
    std::int32_t& thickness = horizontal ? minHeight_ : minWidth_;
    arrowLength_ = arrows_ ? along(arrows_) >> 1 : 0;
    if (arrows_) thickness = std::max(across(arrows_) >> 2, thickness);
    if (container_ == nullptr) {
        containerHalf_ = 0;
        containerCap_ = 0;
    } else {
        thickness = std::max(thickness, across(container_));
        containerHalf_ = along(container_) >> 1;
        containerCap_ = along(container_) >> 2;
    }
    if (thumb_ == nullptr) {
        thumbHalf_ = 0;
        thumbCap_ = 0;
    } else {
        thickness = std::max(across(thumb_) >> 2, thickness);
        thumbHalf_ = along(thumb_) >> 1;
        thumbCap_ = along(thumb_) >> 2;
    }
    const std::int32_t length = std::max(thumbCap_ * 2, containerCap_ * 2) + arrowLength_ * 2;
    (horizontal ? minWidth_ : minHeight_) = length;
}

void cGZWinScrollbar2::CalcPositions() {   // @ 0x1006a050e
    if (dirty_ & 1) CalcImageSizes();
    dirty_ &= 0xfd;
    const std::int32_t extent = orientation_ == 0 ? area_.right - area_.left : area_.bottom - area_.top;
    const std::int32_t track = extent - arrowLength_ * 2;
    std::int32_t offset = 0;
    const std::int32_t span = max_ - min_;
    const std::int32_t range = span >= 0 ? span : 0;
    if (thumbCap_ * 2 + 2 < track) {
        if (range < 1 || range <= visible_) {
            thumbLength_ = track;
        } else {
            if (span < 1) return;
            const std::int32_t length = std::max(thumbCap_ * 2, (visible_ * track) / span);
            thumbLength_ = length;
            offset = ((span - visible_) / 2 + (track - length) * (value_ - min_)) / (span - visible_);
        }
    } else {
        thumbLength_ = 0;
    }
    thumbOffset_ = offset;
}

void cGZWinScrollbar2::SetArea(int l, int t, int r, int b) {   // @ 0x1006a060c
    if (dirty_ & 1) CalcImageSizes();
    if (orientation_ == 0) {
        if (minHeight_ != 0) b = minHeight_ + t;
    } else if (minWidth_ != 0) {
        r = minWidth_ + l;
    }
    cGZWin::SetArea(l, t, r, b);
    dirty_ |= 2;
}

bool cGZWinScrollbar2::SetOrientation(int orientation) {   // @ 0x1006a0c2
    if (orientation != 1 && orientation_ != orientation) {
        dirty_ |= 3;
        orientation_ = static_cast<std::uint8_t>(orientation);
    }
    return true;
}

bool cGZWinScrollbar2::SetValue(int value, bool notify) {   // @ 0x1006a0726
    int clamped = max_ - visible_;
    if (value < clamped) clamped = value;
    if (clamped < min_) clamped = min_;
    if (clamped != value_) {
        dirty_ |= 2;
        InvalidateSelfAndParents();
        value_ = clamped;
        if (notify && get_init_flag())
            SendMsg(notify_, kGZMsgScrollbarChanged, reinterpret_cast<std::uint64_t>(this), GetID(), static_cast<std::uint64_t>(static_cast<std::int64_t>(clamped)));
    }
    return true;
}

bool cGZWinScrollbar2::SetMinimumValue(int value) {   // @ 0x1006a0810
    if (min_ != value) {
        min_ = value;
        SetValue(value_, true);
        dirty_ |= 2;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinScrollbar2::SetMaximumValue(int value) {   // @ 0x1006a088c
    if (max_ != value) {
        max_ = value;
        SetValue(value_, true);
        dirty_ |= 2;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinScrollbar2::SetVisibleRange(int range) {   // @ 0x1006a0908
    const int clamped = range < 0 ? 0 : range;
    if (visible_ != clamped) {
        visible_ = clamped;
        SetValue(value_, true);
        dirty_ |= 2;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinScrollbar2::SetImage(cIGZBuffer*& slot, cIGZBuffer* image) {
    dirty_ |= 3;
    if (slot != image) {
        if (image) image->AddRef();
        cIGZBuffer* previous = slot;
        slot = image;
        if (previous) previous->Release();
    }
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinScrollbar2::GetMinimumSize(cRZSize& size) const {   // @ 0x1006a0bbc
    if (dirty_ & 1) const_cast<cGZWinScrollbar2*>(this)->CalcImageSizes();
    size = cRZSize{minWidth_, minHeight_};
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Interaction

int cGZWinScrollbar2::PickPart(int x, int y) {   // @ 0x1006a0c1c
    if (dirty_ & 2) CalcPositions();
    const int width = area_.right - area_.left, height = area_.bottom - area_.top;
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    const int coordinate = orientation_ == 0 ? x : y;
    const int extent = orientation_ == 0 ? width : height;
    if (coordinate < arrowLength_) return 1;
    if (coordinate < thumbOffset_ + arrowLength_) return 2;
    if (coordinate < thumbOffset_ + arrowLength_ + thumbLength_) return 5;
    return (extent - arrowLength_) > coordinate ? 3 : 4;
}

int cGZWinScrollbar2::PageStep() const {
    const int step = visible_ - overlap_;
    return (step != 0 && overlap_ <= visible_) ? step : 1;
}

bool cGZWinScrollbar2::GZOnMouseDownL(int x, int y, std::uint32_t) {   // @ 0x1006a1b56
    const int part = PickPart(x, y);
    const int page = PageStep();
    GZSetCapture();
    if (!GZAmCapturing()) return false;
    int target = 0;
    switch (part) {
        case 1: target = value_ - increment_; break;
        case 2: target = value_ - page; break;
        case 3: target = page + value_; break;
        case 4: target = increment_ + value_; break;
        case 5: dragAnchor_ = (orientation_ == 0 ? x : y) - thumbOffset_; break;
        default: break;
    }
    if (part >= 1 && part <= 4) {
        SetValue(target, true);
        timer_.StartPeriodic(kRepeatFirstDelay, AsIGZWin());
    }
    pressed_ = static_cast<std::uint8_t>(part);
    InvalidateSelfAndParents();
    return false;
}

bool cGZWinScrollbar2::GZOnMouseUpL(int x, int y, std::uint32_t) {   // @ 0x1006a1c8c
    if (GZAmCapturing()) {
        GZReleaseCapture();
        pressed_ = 0;
        hovered_ = static_cast<std::uint8_t>(PickPart(x, y));
        InvalidateSelfAndParents();
        timer_.Stop();
    }
    return false;
}

bool cGZWinScrollbar2::GZOnMouseMove(int x, int y, std::uint32_t) {   // @ 0x1006a1cf4
    if (!GZAmCapturing() || static_cast<std::uint8_t>(pressed_ - 1) < 4) {
        const int part = PickPart(x, y);
        if (part != hovered_) {
            hovered_ = static_cast<std::uint8_t>(part);
            InvalidateSelfAndParents();
        }
    } else if (pressed_ == 5 && thumbLength_ > 0) {
        const int span = max_ - min_ < 0 ? 0 : max_ - min_;
        const int extent = orientation_ == 0 ? area_.right - area_.left : area_.bottom - area_.top;
        const int travel = (extent - thumbLength_) + arrowLength_ * -2;
        std::int64_t value = 0;
        if (travel >= 1) {
            const int coordinate = orientation_ == 0 ? x : y;
            const int scrollable = std::max(span - visible_, 0);
            value = static_cast<std::int64_t>((coordinate - dragAnchor_) * scrollable) / travel;
        }
        SetValue(static_cast<int>(value), true);
    }
    return false;
}

bool cGZWinScrollbar2::GZOnMouseExit(cIGZWin*) {   // @ 0x1006a1e06
    if (!GZAmCapturing() && hovered_ != 0) {
        hovered_ = 0;
        InvalidateSelfAndParents();
    }
    return false;
}

bool cGZWinScrollbar2::DoMessage(cGZMessage& message) {   // @ 0x1006a1e3e
    if (message.type != kGZMsgScrollbarRepeat) return cGZWin::DoMessage(message);
    const int page = PageStep();
    switch (pressed_) {   // the repeat only continues while the cursor is still on the pressed part
        case 1: if (hovered_ == 1) SetValue(value_ - increment_, true); break;
        case 2: if (hovered_ == 2) SetValue(value_ - page, true); break;
        case 3: if (hovered_ == 3) SetValue(page + value_, true); break;
        case 4: if (hovered_ == 4) SetValue(increment_ + value_, true); break;
        default: break;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Painting

bool cGZWinScrollbar2::GZPaint() {   // @ 0x1006a0d4a
    if (dirty_ & 2) CalcPositions();
    if (arrows_ || container_ || thumb_) PaintSkinned(); else PaintFlat();
    return true;
}

void cGZWinScrollbar2::PaintSkinned() {
    cIGZDrawContext* ctx = static_cast<cIGZDrawContext*>(drawContext_);
    const bool enabled = GetFlag(kWinEnabled);
    const bool horizontal = orientation_ == 0;
    const int width = area_.right - area_.left, height = area_.bottom - area_.top;
    const int extent = horizontal ? width : height;
    const int arrow = arrowLength_;
    // Rectangles are built along the bar (a0..a1) and across it (c0..c1) and mapped to x/y by orientation.
    auto rect = [&](int a0, int c0, int a1, int c1) {
        return horizontal ? cRZRect{a0, c0, a1, c1} : cRZRect{c0, a0, c1, a1};
    };
    auto state = [&](int part) { return enabled ? (pressed_ == part ? 3 : (hovered_ == part ? 2 : 1)) : 0; };
    if (arrows_) {
        const int cell = static_cast<int>(horizontal ? arrows_->Height() : arrows_->Width()) >> 2;
        const int first = state(1) * cell, last = state(4) * cell;
        ctx->Blt(arrows_, rect(0, first, arrow, first + cell), rect(0, 0, arrow, cell));
        ctx->Blt(arrows_, rect(arrow, last, arrow * 2, last + cell), rect(extent - arrow, 0, extent, cell));
    }
    if (container_) {
        const int along = static_cast<int>(horizontal ? container_->Width() : container_->Height());
        const int across = static_cast<int>(horizontal ? container_->Height() : container_->Width());
        const int cap = containerCap_;
        ctx->Blt(container_, rect(0, 0, cap, across), rect(arrow, 0, arrow + cap, across));
        ctx->Blt(container_, rect(along - cap, 0, along, across), rect(extent - arrow - cap, 0, extent - arrow, across));
        ctx->BltTiled(container_, rect(cap, 0, along - cap, across), rect(cap + arrow, 0, extent - cap - arrow, across), 0, 0);
    }
    if (thumb_) {
        const int along = static_cast<int>(horizontal ? thumb_->Width() : thumb_->Height());
        const int cell = static_cast<int>(horizontal ? thumb_->Height() : thumb_->Width()) >> 2;
        const int shade = (enabled ? (pressed_ == 5 ? 3 : (hovered_ == 5 ? 2 : 1)) : 0) * cell;
        const int cap = thumbCap_;
        const int start = thumbOffset_ + arrow, end = start + thumbLength_;
        ctx->Blt(thumb_, rect(0, shade, cap, shade + cell), rect(start, 0, start + cap, cell));
        ctx->Blt(thumb_, rect(along - cap, shade, along, shade + cell), rect(end - cap, 0, end, cell));
        ctx->BltTiled(thumb_, rect(cap, shade, along - cap, shade + cell), rect(start + cap, 0, end - cap, cell), 0, 0);
    }
}

void cGZWinScrollbar2::PaintFlat() {
    cIGZDrawContext* ctx = static_cast<cIGZDrawContext*>(drawContext_);
    const bool enabled = GetFlag(kWinEnabled);
    const int width = area_.right - area_.left, height = area_.bottom - area_.top;
    const int arrow = arrowLength_;
    cRZRect nextArrow, previousArrow, thumbRect, track;
    if (orientation_ == 0) {
        nextArrow = cRZRect{0, 0, arrow, height};                 // (sic) the horizontal bar names its left arrow "next"
        previousArrow = cRZRect{width - arrow, 0, width, height};
        thumbRect = cRZRect{arrow + thumbOffset_, 0, arrow + thumbOffset_ + thumbLength_, height};
        track = cRZRect{arrow, 0, width - arrow, height};
    } else {
        nextArrow = cRZRect{0, height - arrow, width, height};
        previousArrow = cRZRect{0, 0, width, arrow};
        thumbRect = cRZRect{0, arrow + thumbOffset_, width, arrow + thumbOffset_ + thumbLength_};
        track = cRZRect{0, arrow, width, height - arrow};
    }
    if (!enabled) {
        RZDrawRaisedRect(ctx, nextArrow, 0xff808080, true, true);
        RZDrawRaisedRect(ctx, previousArrow, 0xff808080, true, true);
    } else if (hovered_ == 1) {
        RZDrawRaisedRect(ctx, nextArrow, 0xffc8c8ff, true, true);
        RZDrawRaisedRect(ctx, previousArrow, pressed_ == 1 ? 0xffb4b4ff : 0xffdcdcff, pressed_ != 1, true);
    } else if (hovered_ == 4) {
        RZDrawRaisedRect(ctx, previousArrow, 0xffc8c8ff, true, true);
        RZDrawRaisedRect(ctx, nextArrow, pressed_ == 4 ? 0xffb4b4ff : 0xffdcdcff, pressed_ != 4, true);
    } else {
        RZDrawRaisedRect(ctx, nextArrow, 0xffc8c8ff, true, true);
        RZDrawRaisedRect(ctx, previousArrow, 0xffc8c8ff, true, true);
    }
    ctx->SetColor(0xff646482);
    auto triangle = [&](const cRZRect& r, bool pointsUp) {
        const int w = r.right - r.left, h = r.bottom - r.top;
        const float pts[6] = {
            static_cast<float>(w / 2 + r.left), static_cast<float>(pointsUp ? r.top + h / 4 : r.bottom - h / 4),
            static_cast<float>(pointsUp ? r.left + w / 4 : r.right - w / 4), static_cast<float>(pointsUp ? r.bottom - h / 4 : r.top + h / 4),
            static_cast<float>(pointsUp ? r.right - w / 4 : r.left + w / 4), static_cast<float>(pointsUp ? r.bottom - h / 4 : r.top + h / 4)};
        ctx->Draw(eDrawingOp::FillTri, pts, 3);
    };
    triangle(previousArrow, true);
    triangle(nextArrow, false);
    ctx->SetColor(0xff9696be);
    ctx->Fill(track);
    if (!enabled) {
        ctx->SetColor(0xffbebebe);
        ctx->Fill(thumbRect);
    } else if (pressed_ == 5) {
        RZDrawRaisedRect(ctx, thumbRect, 0xffb4b4ff, false, true);
    } else {
        RZDrawRaisedRect(ctx, thumbRect, hovered_ == 5 ? 0xffdcdcff : 0xffc8c8ff, true, true);
    }
}

}  // namespace gz
