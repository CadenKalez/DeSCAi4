#include "gz/WinSlider.h"

namespace gz {

cGZWinSlider::cGZWinSlider(cIGZWinMgr* mgr) : cGZWin(mgr) {
    flags_ &= ~0x8000u;
    cGZWinSlider::SetArea(0, 0, 0x20, 0x60);
}

cGZWinSlider::~cGZWinSlider() {
    if (image_) image_->Release();
}

bool cGZWinSlider::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinSlider) {
        *out = static_cast<cIGZWinSlider*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cGZWin*>(this);
        AddRef();
        return true;
    }
    return false;
}

// With an image the window is as thin as the thumb across the track.
void cGZWinSlider::SetArea(int l, int t, int r, int b) {
    if (image_) {
        if (direction_ == kSliderHorizontal) {
            b = (segment_.bottom + t) - segment_.top;
        } else if (direction_ == kSliderVertical) {
            r = (segment_.right + l) - segment_.left;
        } else {
            CalculateAllMetrics();
            return;
        }
    }
    cGZWin::SetArea(l, t, r, b);
    CalculateAllMetrics();
}

bool cGZWinSlider::Init() {
    if (!get_init_flag()) CalculateAllMetrics();
    return cGZWin::Init();
}

bool cGZWinSlider::Shutdown() {
    if (image_) {
        image_->Release();
        image_ = nullptr;
    }
    return cGZWin::Shutdown();
}

int cGZWinSlider::GetThumbWidth() const {
    if (image_) return segment_.right - segment_.left;
    return direction_ == kSliderHorizontal ? GetH() : GetW();
}

int cGZWinSlider::GetThumbHeight() const {
    if (image_) return segment_.bottom - segment_.top;
    return direction_ == kSliderHorizontal ? GetH() : GetW();
}

void cGZWinSlider::CalculateAllMetrics() {
    if (direction_ == kSliderHorizontal) {
        trackStart_ = margin_;
        trackEnd_ = (GetW() - GetThumbWidth()) - margin_;
    } else {
        trackStart_ = (GetH() - GetThumbHeight()) - margin_;
        trackEnd_ = margin_;
    }
    CalculateThumbPosition();
    InvalidateSelfAndParents();
}

void cGZWinSlider::CalculateThumbPosition() {
    bool horizontal;
    if (max_ == min_) {
        thumbPosition_ = trackStart_;
        horizontal = direction_ == kSliderHorizontal;
    } else if (direction_ == kSliderHorizontal) {
        thumbPosition_ = static_cast<int>(static_cast<float>(trackEnd_ - trackStart_) * fraction_) + trackStart_;
        horizontal = true;
    } else {
        thumbPosition_ = trackStart_ - static_cast<int>(static_cast<float>(trackStart_ - trackEnd_) * fraction_);
        horizontal = false;
    }
    if (horizontal) {
        thumb_.left = thumbPosition_;
        thumb_.right = GetThumbWidth() + thumbPosition_;
        const int top = GetH() / 2 - GetThumbHeight() / 2;
        thumb_.top = top;
        thumb_.bottom = GetThumbHeight() + top;
    } else {
        const int left = GetW() / 2 - GetThumbWidth() / 2;
        thumb_.left = left;
        thumb_.right = GetThumbWidth() + left;
        thumb_.top = thumbPosition_;
        thumb_.bottom = GetThumbHeight() + thumbPosition_;
    }
}

int cGZWinSlider::GetValue() const {
    return static_cast<int>(static_cast<float>(max_ - min_) * fraction_ + 0.5f) + min_;
}

bool cGZWinSlider::SetValue(int value) {
    if (busy_) return false;
    busy_ = true;
    if (GetValue() == value) {
        busy_ = false;
        return false;
    }
    if (max_ <= value) value = max_;
    if (value < min_) value = min_;
    fraction_ = static_cast<float>(value - min_) / static_cast<float>(max_ - min_);
    CalculateThumbPosition();
    SendMsg(notify_, kGZMsgCommand, kGZSliderValueChanged, GetID(), 0);
    busy_ = false;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinSlider::SetPercentage(float fraction) {
    if (busy_) return false;
    busy_ = true;
    if (fraction_ != fraction || fraction_ != fraction_ || fraction != fraction) {
        float clamped = 0.0f <= fraction ? fraction : 0.0f;
        clamped = clamped <= 1.0f ? clamped : 1.0f;
        fraction_ = clamped;
        CalculateThumbPosition();
        SendMsg(notify_, kGZMsgCommand, kGZSliderValueChanged, GetID(), 0);
        busy_ = false;
        InvalidateSelfAndParents();
        return true;
    }
    busy_ = false;
    return false;
}

void cGZWinSlider::SetMinimumValue(int value) {
    min_ = value;
    CalculateAllMetrics();
    InvalidateSelfAndParents();
}

void cGZWinSlider::SetMaximumValue(int value) {
    max_ = value;
    CalculateAllMetrics();
    InvalidateSelfAndParents();
}

// The image is cut into four segments along the direction of travel (end cap, tiled middle, end cap, thumb); the
// segment rectangle is the first one (a quarter of the image's length).
bool cGZWinSlider::SetImage(cIGZBuffer* image, const cRZRect* area) {
    if (image) image->AddRef();
    if (image_) image_->Release();
    image_ = image;
    cRZRect source{0, 0, 0, 0};
    if (!image_) {
        segment_ = {0, 0, 0x10, 0x10};
    } else {
        image_->GetBufferArea(source);
    }
    if (area) source = *area;
    if (direction_ == kSliderVertical) {
        int right, left;
        if (image_) {
            segment_.left = source.left;
            segment_.top = source.top;
            segment_.right = source.right - source.left;
            segment_.bottom = (source.bottom - source.top) / 4;
        }
        left = segment_.left;
        right = segment_.right;
        SetW(right - left);
        return true;
    }
    if (direction_ != kSliderHorizontal) return false;
    int bottom, top;
    if (image_) {
        segment_.left = source.left;
        segment_.top = source.top;
        segment_.right = (source.right - source.left) / 4;
        segment_.bottom = source.bottom - source.top;
        bottom = source.bottom - source.top;
        top = source.top;
    } else {
        top = segment_.top;
        bottom = segment_.bottom;
    }
    SetH(bottom - top);
    return true;
}

bool cGZWinSlider::GetImage(cIGZBuffer*& image, cRZRect& area) const {
    image = image_;
    area = segment_;
    return true;
}

bool cGZWinSlider::GZPaint() {
    if (!image_) PaintFlat();
    else PaintBmp();
    return true;
}

// Fill, a centre line along the track in the inverse colour, then the thumb rectangle.
bool cGZWinSlider::PaintFlat() {
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    context->SetColor(GetFillColorRGB());
    context->Fill(drawArea_);
    context->SetColor(~GetFillColorRGB());
    int x0, y0, x1, y1;
    if (direction_ == kSliderHorizontal) {
        x0 = drawArea_.left + margin_;
        y0 = drawArea_.top + GetH() / 2;
        x1 = drawArea_.right - margin_;
        y1 = drawArea_.top + GetH() / 2;
    } else {
        x0 = drawArea_.left + GetW() / 2;
        y0 = margin_ + drawArea_.top;
        x1 = drawArea_.right - GetW() / 2;
        y1 = (drawArea_.bottom - 1) - margin_;
    }
    context->Line(x0, y0, x1, y1);
    const int left = thumb_.left + drawArea_.left, top = thumb_.top + drawArea_.top;
    context->Fill(cRZRect{left, top, GetThumbWidth() + left, GetThumbHeight() + top});
    return true;
}

bool cGZWinSlider::PaintBmp() {
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    cRZRect source = segment_;
    cRZRect dest{drawArea_.left + segment_.left, drawArea_.top + segment_.top, drawArea_.left + segment_.right,
                 drawArea_.top + segment_.bottom};
    context->Blt(image_, source, dest);                                   // first end cap
    if (direction_ == kSliderVertical) {
        const int step = segment_.bottom - segment_.top;
        source.top += step; source.bottom += step;
        dest.top = dest.bottom;
        dest.bottom = drawArea_.bottom - step;
        context->BltTiled(image_, source, dest, 0, 0);                    // middle
        source.top += step; source.bottom += step;
        dest.top = dest.bottom;
        dest.bottom = drawArea_.bottom;
        context->Blt(image_, source, dest);                               // second end cap
        source.top += step; source.bottom += step;
    } else if (direction_ == kSliderHorizontal) {
        const int step = segment_.right - segment_.left;
        source.left += step; source.right += step;
        dest.left += step;
        dest.right = drawArea_.right - step;
        context->BltTiled(image_, source, dest, 0, 0);
        source.left += step; source.right += step;
        dest.left = dest.right;
        dest.right = drawArea_.right;
        context->Blt(image_, source, dest);
        source.left += step; source.right += step;
    } else {
        return true;
    }
    const cRZRect thumb{segment_.left + drawArea_.left + thumb_.left, segment_.top + drawArea_.top + thumb_.top,
                        segment_.right + drawArea_.left + thumb_.left, segment_.bottom + drawArea_.top + thumb_.top};
    context->Blt(image_, source, thumb);                                  // the thumb
    return true;
}

// Pixel along the track -> 0..1 (the thumb is centred on the cursor and kept between the track ends).
bool cGZWinSlider::ConvertCursorPositionToPercentage(float& fraction, int x, int y) {
    int position, thumb;
    if (direction_ == kSliderHorizontal) {
        thumb = GetThumbWidth();
        position = x;
    } else {
        thumb = GetThumbHeight();
        position = y;
    }
    position -= thumb / 2;
    const int low = trackEnd_ < trackStart_ ? trackEnd_ : trackStart_;
    const int high = trackEnd_ < trackStart_ ? trackStart_ : trackEnd_;
    if (position <= low) position = low;
    if (high <= position) position = high;
    fraction = static_cast<float>(position - trackStart_) /
               static_cast<float>((trackEnd_ - trackStart_) + margin_ * -2);
    return true;
}

bool cGZWinSlider::GZOnMouseDownL(int x, int y, std::uint32_t) {
    GZSetCapture();
    float fraction = 0.0f;
    if (ConvertCursorPositionToPercentage(fraction, x, y)) {
        SetPercentage(fraction);
        return true;
    }
    return false;
}

bool cGZWinSlider::GZOnMouseUpL(int x, int y, std::uint32_t) {
    if (!GZAmCapturing()) return false;
    GZReleaseCapture();
    float fraction = 0.0f;
    if (!ConvertCursorPositionToPercentage(fraction, x, y)) return false;
    SetPercentage(fraction);
    SendMsg(notify_, kGZMsgCommand, kGZSliderReleased, GetID(), 0);
    return true;
}

bool cGZWinSlider::GZOnMouseMove(int x, int y, std::uint32_t) {
    if (!GZAmCapturing()) return false;
    float fraction = 0.0f;
    if (!ConvertCursorPositionToPercentage(fraction, x, y)) return false;
    SetPercentage(fraction);
    return true;
}

bool cGZWinSlider::GZOnKeyDown(std::uint32_t key, std::uint32_t flags) {
    SendMsg(notify_, kGZMsgKeyDown, key, flags, 0);
    return true;
}

bool cGZWinSlider::GZOnKeyUp(std::uint32_t key, std::uint32_t flags) {
    SendMsg(notify_, kGZMsgKeyUp, key, flags, 0);
    return true;
}

}  // namespace gz
