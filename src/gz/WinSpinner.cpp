#include "gz/WinSpinner.h"

#include <algorithm>

#include "gz/RZDraw.h"

namespace gz {

namespace {
IWinNumberFormatter* g_formatter = nullptr;
constexpr std::uint32_t kRepeatDelayStart = 400;
constexpr std::uint32_t kRepeatDelayStep = 0x32;
constexpr std::uint32_t kKeyEnd = 0x23, kKeyHome = 0x24, kKeyUp = 0x26, kKeyDown = 0x28;
constexpr std::uint32_t kMsgKeyDown = 5, kMsgKeyUp = 6;
constexpr std::uint32_t kFontColorOwn = 0x8000000;   // "use the font's own colour"
}  // namespace

IWinNumberFormatter* WinNumberFormatter() { return g_formatter; }
void SetWinNumberFormatter(IWinNumberFormatter* formatter) { g_formatter = formatter; }

cGZWinSpinner::cGZWinSpinner(cIGZWinMgr* mgr) : cGZWin(mgr) {   // @ 0x100278588
}

cGZWinSpinner::~cGZWinSpinner() {   // @ 0x1002786ec
    cGZWinSpinner::Shutdown();
    if (image_) image_->Release();
    if (font_) font_->Release();
}

bool cGZWinSpinner::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1002787e2
    if (iid == kGZIID_cIGZWinSpinner) {
        *out = static_cast<cIGZWinSpinner*>(this);
    } else if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cGZWin*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

bool cGZWinSpinner::Init() {   // @ 0x10027886a
    if (type_ >= 3) return false;
    IWinImageSource* images = WinImageSource();
    IWinFontSource* fonts = WinFontSource();
    if (images && image_ == nullptr) SetImage(images->GetImage(kSpinnerDefaultImageId), nullptr);
    if (fonts && font_ == nullptr && type_ != 0) {
        IWinFont* font = fonts->Font(fontStyle_);
        if (font == nullptr) font = fonts->Font(kWinDefaultFontStyle);
        SetFont(font);
    }
    if (GetW() < 1) SetW(1);
    if (GetH() < 1) SetH(1);
    SetCaptionFromValue();
    return cGZWin::Init();
}

bool cGZWinSpinner::SetCaptionFromValue() {   // @ 0x100278972
    if (flags_spinner_ & kSpinnerFlagShowValue) {
        IWinNumberFormatter* formatter = WinNumberFormatter();
        const bool available = formatter && formatter->Available();
        if ((flags_spinner_ & kSpinnerFlagFormatA) && available) {
            formatter->FormatA(value_, caption_);
        } else if (!(flags_spinner_ & kSpinnerFlagFormatA) && (flags_spinner_ & kSpinnerFlagFormatB) && available) {
            formatter->FormatB(value_, caption_);
        } else {
            caption_.Sprintf("%d", static_cast<unsigned>(value_));
        }
    }
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinSpinner::Shutdown() {   // @ 0x100278a30
    HideWindow();
    if (cIGZBuffer* image = image_) { image_ = nullptr; image->Release(); }
    if (IWinFont* font = font_) { font_ = nullptr; font->Release(); }
    return cGZWin::Shutdown();
}

void cGZWinSpinner::SetArea(int l, int t, int r, int b) {   // @ 0x100278a8a
    int w = r - l, h = b - t;
    if (image_ == nullptr) {
        if (type_ == 0) {
            w = std::max<int>(w, minButton_);
            h = std::max<int>(h, minButton_);
            buttonH_ = h >> 1;
            buttonW_ = w;
        } else {
            const int minH = fontHeight_ + gutterY_ * 2;
            const int minW = minButton_ + gutterX_ * 2;
            h = std::max(h, minH);
            w = std::max(w, minW);
            buttonH_ = h / 2;
            buttonW_ = buttonH_;
            if (buttonH_ < minButton_) buttonW_ = minButton_;
            else if (buttonH_ > maxButton_) buttonW_ = maxButton_;
        }
    } else {
        int minW = buttonW_, minH;
        if (type_ == 0) {
            minH = buttonH_ * 2;
        } else {
            minW += gutterX_ * 2;
            minH = std::max(buttonH_ * 2, fontHeight_ + gutterY_ * 2);
        }
        w = std::max(w, minW);
        h = std::max(h, minH);
    }
    if (area_.left == l && area_.top == t && area_.right == w + l && area_.bottom == h + t) return;
    cGZWin::SetArea(l, t, w + l, h + t);
    InvalidateSelfAndParents();
}

void cGZWinSpinner::CalcImageArea() {   // @ 0x100278c06
    if (image_ == nullptr) return;
    cRZRect area;
    if (flags_spinner_ & kSpinnerFlagImageArea) area = imageArea_;
    else image_->GetBufferArea(area);
    buttonW_ = (area.right - area.left) / 4;
    buttonH_ = (area.bottom - area.top) / 2;
    cellX_ = static_cast<std::uint16_t>(area.left);
    cellY_ = static_cast<std::uint16_t>(area.top);
}

bool cGZWinSpinner::SetImage(cIGZBuffer* image, const cRZRect* area) {   // @ 0x100278c92
    if (image_ != image) {
        if (image) image->AddRef();
        cIGZBuffer* previous = image_;
        image_ = image;
        if (previous) previous->Release();
    }
    if (area == nullptr) {
        flags_spinner_ &= ~kSpinnerFlagImageArea;
    } else {
        flags_spinner_ |= kSpinnerFlagImageArea;
        imageArea_ = *area;
    }
    CalcImageArea();
    ResizeToMinimum();
    return true;
}

bool cGZWinSpinner::SetButtonImage(cIGZBuffer* image) {   // @ 0x100278d54
    if (image_ != image) {
        if (image) image->AddRef();
        cIGZBuffer* previous = image_;
        image_ = image;
        if (previous) previous->Release();
    }
    CalcImageArea();
    ResizeToMinimum();
    return true;
}

bool cGZWinSpinner::SetButtonImageArea(const cRZRect& area) {   // @ 0x100278de0
    imageArea_ = area;
    CalcImageArea();
    ResizeToMinimum();
    return true;
}

bool cGZWinSpinner::GZPaint() {   // @ 0x100278e9c
    cRZRect frame{}, text{};
    int buttonOffset = 0;
    bool drawText = false, fill = false;
    if (type_ == 1 || type_ == 2) {
        int right;
        if (type_ == 2) {
            frame.left = buttonW_ + drawArea_.left + gutterX_;
            right = drawArea_.right;
            buttonOffset = 0;
        } else {
            buttonOffset = GetW() - buttonW_;
            frame.left = drawArea_.left;
            right = (drawArea_.left + buttonOffset) - gutterX_;
        }
        frame.top = drawArea_.top;
        frame.right = right;
        frame.bottom = drawArea_.bottom;
        text.left = gutterX_ + frame.left;
        text.top = (GetH() - fontHeight_) / 2 + frame.top;
        text.right = right - gutterX_;
        text.bottom = text.top + fontHeight_;
        drawText = font_ != nullptr;
        fill = (flags_spinner_ & kSpinnerFlagOpaque) != 0;
        if (flags_spinner_ & kSpinnerFlagOutline) {
            const int r = frame.right - 1, b = frame.bottom - 1;
            Context()->SetColor(outline_[0]);
            Context()->Line(frame.left, frame.top, frame.left, b);
            Context()->SetColor(outline_[1]);
            Context()->Line(frame.left, frame.top, r, frame.top);
            Context()->SetColor(outline_[2]);
            Context()->Line(r, frame.top, r, b);
            Context()->SetColor(outline_[3]);
            Context()->Line(frame.left, b, r, b);
            frame = cRZRect{frame.left + 1, frame.top + 1, r, b};
        }
        if (fill) {
            Context()->SetColor(GetFillColorRGB());
            Context()->Fill(frame);
        }
    }
    if (image_ == nullptr) PaintButtonsFlat(buttonOffset);
    else PaintButtonsImage(buttonOffset);

    if (drawText) {
        const std::uint32_t packed = !GetFlag(kWinEnabled) ? textColors_[2] : (hovered_ == 0 ? textColors_[0] : textColors_[1]);
        std::uint32_t first = kFontColorOwn;
        if (packed != kWinUnsetTextColor) first = ConvertPackedRGBToNative(packed);
        font_->DrawTextBox(drawBuffer_, text, caption_.Data(), static_cast<std::uint32_t>(caption_.Strlen()), alignment_,
                           first, kFontColorOwn, nullptr, 0);
    }

    int direction;
    if (pressed_ == 1) {
        if (hovered_ != 1) return true;
        direction = 1;
    } else if (pressed_ == 2) {
        if (hovered_ != 2) return true;
        direction = -1;
    } else {
        return true;
    }
    if (repeatDelay_ < timer_.GetElapsedTimeU32()) {
        SetValue(value_ + direction * step_);
        if (repeatDelay_ > kRepeatDelayStep) repeatDelay_ -= kRepeatDelayStep;
        timer_.Restart();
    }
    InvalidateSelfAndParents();
    return true;
}

void cGZWinSpinner::PaintButtonsFlat(int offset) {   // @ 0x100279374
    cRZRect rect{offset + drawArea_.left, drawArea_.top, 0, 0};
    rect.right = rect.left + buttonW_;
    rect.bottom = rect.top + buttonH_;
    RZDrawRaisedRect(Context(), rect, buttonColor_, !(pressed_ == 1 && hovered_ == 1), true);
    rect.top += buttonH_;
    rect.bottom = drawArea_.bottom;
    RZDrawRaisedRect(Context(), rect, buttonColor_, !(pressed_ == 2 && hovered_ == 2), true);
}

void cGZWinSpinner::PaintButtonsImage(int offset) {   // @ 0x100279444
    const int half = GetH() / 2;
    const bool enabled = GetFlag(kWinEnabled);
    cRZRect dest{offset + drawArea_.left, (half - buttonH_) / 2 + drawArea_.top, 0, 0};
    dest.right = dest.left + buttonW_;
    dest.bottom = dest.top + buttonH_;
    cRZRect source{0, cellY_, 0, buttonH_ + cellY_};
    if (!enabled) {
        source.left = cellX_;
        source.right = buttonW_ + cellX_;
        Context()->Blt(image_, source, dest);
        source.top += buttonH_;
        source.bottom += buttonH_;
    } else {
        if (pressed_ == 1) source.left = (buttonW_ << (hovered_ == 1 ? 1 : 0)) + cellX_;
        else if (pressed_ == 0) source.left = (hovered_ == 1 ? buttonW_ * 3 : buttonW_) + cellX_;
        else source.left = cellX_ + buttonW_;
        source.right = buttonW_ + source.left;
        Context()->Blt(image_, source, dest);
        source.top += buttonH_;
        source.bottom += buttonH_;
        if (pressed_ == 2) source.left = (buttonW_ << (hovered_ == 2 ? 1 : 0)) + cellX_;
        else if (pressed_ == 0) source.left = (hovered_ == 2 ? buttonW_ * 3 : buttonW_) + cellX_;
        else source.left = cellX_ + buttonW_;
        source.right = buttonW_ + source.left;
    }
    dest.top += half;
    dest.bottom += half;
    Context()->Blt(image_, source, dest);
}

bool cGZWinSpinner::GZOnMouseDownL(int x, int y, std::uint32_t) {   // @ 0x10027965e
    CalcMousePos(pressed_, x, y);
    repeatDelay_ = kRepeatDelayStart;
    timer_.Restart();
    if (pressed_ == 2) SetValue(value_ - step_);
    else if (pressed_ == 1) SetValue(step_ + value_);
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinSpinner::CalcMousePos(std::uint32_t& out, int x, int y) const {   // @ 0x1002796e0
    if (!IsPointInWindowWindowCoordinates(x, y)) {
        out = kPosNone;
        return false;
    }
    const int height = GetH();
    if (type_ == 1) {
        if (x < GetW() - buttonW_) { out = kPosNone; return true; }
    } else if (type_ != 0 && buttonW_ < x) {
        out = kPosNone;
        return true;
    }
    out = 2 - (y < height / 2 ? 1 : 0);
    return true;
}

bool cGZWinSpinner::GZOnMouseUpL(int, int, std::uint32_t) {   // @ 0x10027978e
    repeatDelay_ = kRepeatDelayStart;
    if (hovered_ == 0) GZReleaseCapture();
    pressed_ = 0;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinSpinner::GZOnMouseMove(int x, int y, std::uint32_t) {   // @ 0x1002797d2
    const std::uint32_t previous = hovered_;
    CalcMousePos(hovered_, x, y);
    if (pressed_ == 0) {
        if (previous == hovered_) return true;
        if (hovered_ == 0) GZReleaseCapture();
        else GZSetCapture();
    } else {
        if (previous == hovered_) return true;
        repeatDelay_ = kRepeatDelayStart;
    }
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinSpinner::GZOnCaptureChanged(cIGZWin*, cIGZWin* next) {   // @ 0x100279842
    if (static_cast<cIGZWin*>(this) != next) {
        pressed_ = 0;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinSpinner::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x100279862
    switch (key) {
        case kKeyEnd: SetValue(max_); return true;
        case kKeyHome: SetValue(min_); return true;
        case kKeyUp: SetValue(step_ + value_); return true;
        case kKeyDown: SetValue(value_ - step_); return true;
        default: SendMsg(notify_, kMsgKeyDown, key, modifiers, 0); return false;
    }
}

bool cGZWinSpinner::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x1002798f8
    SendMsg(notify_, kMsgKeyUp, key, modifiers, 0);
    return false;
}

bool cGZWinSpinner::SetSpinnerType(tSpinnerType type) {   // @ 0x100279954
    if (type - 1U < 2) {
        if (font_ == nullptr) SetFontStyle(fontStyle_);
    } else if (type != kButtonsOnly) {
        return false;
    }
    type_ = type;
    ResizeToMinimum();
    return true;
}

void cGZWinSpinner::SetFlagInternal(std::uint32_t flag, bool on) {   // @ 0x1002799e6
    flags_spinner_ = on ? (flags_spinner_ | flag) : (flags_spinner_ & ~flag);
    if (flag == kSpinnerFlagOutline || flag == kSpinnerFlagOpaque) {
        InvalidateSelfAndParents();
        return;
    }
    if (flag == kSpinnerFlagFormatA) {
        if (on) flags_spinner_ &= ~kSpinnerFlagFormatB;
    } else if (flag == kSpinnerFlagFormatB) {
        if (on) flags_spinner_ &= ~kSpinnerFlagFormatA;
    } else if (flag == kSpinnerFlagImageArea) {
        CalcImageArea();
        ResizeToMinimum();
        return;
    } else {
        return;
    }
    SetCaptionFromValue();
}

bool cGZWinSpinner::SetValue(int value) {   // @ 0x100279ae6
    value = std::max(value, min_);
    if (max_ < value) value = max_;
    const int previous = value_;
    value_ = value;
    if (previous != value) {
        SetCaptionFromValue();
        SendMsg(notify_, kGZMsgSpinnerChanged, GetID(), static_cast<std::uint64_t>(static_cast<std::int64_t>(value_)), 0);
    }
    return true;
}

bool cGZWinSpinner::SetFont(IWinFont* font) {   // @ 0x100279bf6
    if (font_ == font) return true;
    if (font) font->AddRef();
    IWinFont* previous = font_;
    font_ = font;
    if (previous) previous->Release();
    if (font_) fontHeight_ = font_->LineHeight(0, 0);
    ResizeToMinimum();
    return true;
}

bool cGZWinSpinner::SetFontStyle(std::uint32_t styleId) {   // @ 0x100279c9e
    fontStyle_ = styleId;
    if (get_init_flag()) {
        IWinFontSource* fonts = WinFontSource();
        IWinFont* font = fonts ? fonts->Font(fontStyle_) : nullptr;
        if (font) SetFont(font);
    }
    return true;
}

bool cGZWinSpinner::SetFontColor(tFontColorIdx index, std::uint32_t native) {   // @ 0x100279d1a
    const std::uint32_t packed = ConvertNativeToPackedRGB(native);
    if (textColors_[index] != packed) {
        textColors_[index] = packed;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinSpinner::GetFontColor(tFontColorIdx index, std::uint32_t& native) const {   // @ 0x100279dac
    const std::uint32_t packed = textColors_[index];
    if (packed != kWinUnsetTextColor) native = ConvertPackedRGBToNative(packed);
    return packed != kWinUnsetTextColor;
}

bool cGZWinSpinner::SetFontColor(tFontColorIdx index, cRZColor color) {   // @ 0x100279e14
    if (textColors_[index] != color.rgb) {
        textColors_[index] = color.rgb;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinSpinner::GetFontColor(tFontColorIdx index, cRZColor& color) const {   // @ 0x100279e60
    if (textColors_[index] == kWinUnsetTextColor) return false;
    color.rgb = textColors_[index];
    return true;
}

bool cGZWinSpinner::SetTextAlignment(std::uint32_t alignment) {   // @ 0x100279ed2
    if (alignment < 3) {
        alignment_ = static_cast<std::uint8_t>(alignment);
        InvalidateSelfAndParents();
    }
    return alignment < 3;
}

bool cGZWinSpinner::GetTextAlignment(std::uint32_t& alignment) const {   // @ 0x100279f1c
    if (alignment_ < 3) alignment = alignment_;
    return alignment_ < 3;
}

bool cGZWinSpinner::SetGutters(std::uint8_t x, std::uint8_t y) {   // @ 0x100279f4e
    gutterX_ = x;
    gutterY_ = y;
    ResizeToMinimum();
    return true;
}

bool cGZWinSpinner::SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) {   // @ 0x10027a010
    outline_[0] = ConvertNativeToPackedRGB(left);
    outline_[1] = ConvertNativeToPackedRGB(top);
    outline_[2] = ConvertNativeToPackedRGB(right);
    outline_[3] = ConvertNativeToPackedRGB(bottom);
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinSpinner::GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) const {   // @ 0x10027a0a0
    left = ConvertPackedRGBToNative(outline_[0]);
    top = ConvertPackedRGBToNative(outline_[1]);
    right = ConvertPackedRGBToNative(outline_[2]);
    bottom = ConvertPackedRGBToNative(outline_[3]);
    return true;
}

bool cGZWinSpinner::SetOutlineColor(tOutlineIndex index, const cRZColor& color) {   // @ 0x10027a136
    outline_[index] = color.rgb;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinSpinner::GetOutlineColor(tOutlineIndex index, cRZColor& color) const {   // @ 0x10027a17a
    color.rgb = outline_[index];
    return true;
}

}  // namespace gz
