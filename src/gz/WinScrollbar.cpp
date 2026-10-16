#include "gz/WinScrollbar.h"

namespace gz {

namespace {
constexpr std::uint32_t kRepeatMilliseconds = 200;
constexpr int kCells = 12;     // the image is twelve cells wide
}  // namespace

cGZWinScrollbar::cGZWinScrollbar(cIGZWinMgr* mgr) : cGZWinSlider(mgr) {   // @ 0x10027a1f8
    flags_ &= ~0x8000u;
    lineCount_ = pageCount_ = static_cast<std::uint32_t>(max_ - min_);
    margin_ = 0;
}

bool cGZWinScrollbar::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10027a3a4
    if (iid == kGZIID_cIGZWinScrollbar) {
        *out = static_cast<cIGZWinScrollbar*>(this);
    } else if (iid == kGZIID_cIGZWinSlider) {
        *out = static_cast<cIGZWinSlider*>(this);
    } else if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cGZWin*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Interaction

bool cGZWinScrollbar::DoCursorPositionHitTest(tScrollHitTestResult& result, int x, int y) {   // @ 0x10027b02c
    result = kScrollNothing;
    if (!IsPointInWindowWindowCoordinates(x, y)) return false;
    const bool horizontal = direction_ == kSliderHorizontal;
    const int coordinate = horizontal ? x : y;
    const int button = horizontal ? GetPrevNextButtonWidth() : GetPrevNextButtonHeight();
    const int extent = horizontal ? GetW() : GetH();
    const int thumbLength = horizontal ? GetThumbWidth() : GetThumbHeight();
    if (coordinate < button) result = kScrollPreviousLine;
    else if (coordinate < thumbPosition_) result = kScrollPreviousPage;
    else if (coordinate < thumbLength + thumbPosition_) result = kScrollThumb;
    else result = (coordinate < extent - button) ? kScrollNextPage : kScrollNextLine;
    return true;
}

void cGZWinScrollbar::MoveLinePrevious() {   // @ 0x10027ad02
    if (lineCount_ != 0) SetValue(GetValue() - static_cast<int>(static_cast<std::uint32_t>(max_ - min_) / lineCount_));
}

void cGZWinScrollbar::MoveLineNext() {   // @ 0x10027ad56
    if (lineCount_ != 0) SetValue(GetValue() + static_cast<int>(static_cast<std::uint32_t>(max_ - min_) / lineCount_));
}

void cGZWinScrollbar::MovePagePrevious() {   // @ 0x10027ada8
    if (pageCount_ != 0) SetValue(GetValue() - static_cast<int>(static_cast<std::uint32_t>(max_ + 1 - min_) / pageCount_));
}

void cGZWinScrollbar::MovePageNext() {   // @ 0x10027adfe
    if (pageCount_ != 0) SetValue(GetValue() + static_cast<int>(static_cast<std::uint32_t>(max_ + 1 - min_) / pageCount_));
}

bool cGZWinScrollbar::GZOnMouseDownL(int x, int y, std::uint32_t) {   // @ 0x10027ab34
    tScrollHitTestResult part;
    if (DoCursorPositionHitTest(part, x, y)) {
        pressed_ = part;
        armed_ = true;
        GZSetCapture();
        if (GZAmCapturing()) {
            repeat_.Restart();
            InvalidateSelfAndParents();
            switch (pressed_) {
                case kScrollPreviousLine: MoveLinePrevious(); break;
                case kScrollNextLine: MoveLineNext(); break;
                case kScrollPreviousPage: MovePagePrevious(); break;
                case kScrollNextPage: MovePageNext(); break;
                default: break;
            }
        }
    } else {
        pressed_ = part;
    }
    return true;
}

bool cGZWinScrollbar::GZOnMouseUpL(int x, int y, std::uint32_t) {   // @ 0x10027abf4
    if (GZAmCapturing()) {
        GZReleaseCapture();
        if (pressed_ == kScrollThumb) {
            float fraction;
            if (ConvertCursorPositionToPercentage(fraction, x, y)) SetPercentage(fraction);
        }
        pressed_ = kScrollNothing;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cGZWinScrollbar::GZOnMouseMove(int x, int y, std::uint32_t) {   // @ 0x10027ac70
    if (GZAmCapturing()) {
        if (pressed_ == kScrollThumb) {
            float fraction;
            if (ConvertCursorPositionToPercentage(fraction, x, y)) SetPercentage(fraction);
        } else {
            tScrollHitTestResult part;
            armed_ = DoCursorPositionHitTest(part, x, y) ? (part == pressed_) : false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Metrics and the line / page model

void cGZWinScrollbar::CalculateAllMetrics() {   // @ 0x10027b146
    if (direction_ == kSliderHorizontal) {
        trackStart_ = GetPrevNextButtonWidth();
        trackEnd_ = (GetW() - GetPrevNextButtonWidth()) - GetThumbWidth();
    } else {
        trackStart_ = GetPrevNextButtonHeight();
        trackEnd_ = (GetH() - GetPrevNextButtonHeight()) - GetThumbHeight();
    }
    CalculateThumbPosition();
}

void cGZWinScrollbar::SetLineAndPageCount(std::uint32_t lines, std::uint32_t pages) {   // @ 0x10027ae52
    if (lines == 0) lines = static_cast<std::uint32_t>(max_ - min_);
    lineCount_ = lines;
    pageCount_ = pages != 0 ? pages : 1;
    CalculateAllMetrics();
}

long cGZWinScrollbar::GetCurrentLine() {   // @ 0x10027aee8
    if (lineCount_ == 1) return 0;
    const int value = GetValue();
    return static_cast<long>((static_cast<float>(lineCount_ - 1) * static_cast<float>(value - min_)) / static_cast<float>(max_ - min_));
}

int cGZWinScrollbar::GetValueOfGivenLine(std::uint32_t line) {   // @ 0x10027af48
    int value = min_;
    if (lineCount_ != 1) value += static_cast<int>((static_cast<float>(max_ - min_) * static_cast<float>(line)) / static_cast<float>(lineCount_ - 1));
    return value;
}

long cGZWinScrollbar::GetCurrentPage() {   // @ 0x10027af8a
    if (pageCount_ == 1) return 0;
    const int value = GetValue();
    return static_cast<long>((static_cast<float>(pageCount_ - 1) * static_cast<float>(value - min_)) / static_cast<float>(max_ - min_));
}

int cGZWinScrollbar::GetValueOfGivenPage(std::uint32_t page) {   // @ 0x10027afea
    int value = min_;
    if (pageCount_ != 1) value += static_cast<int>((static_cast<float>(max_ - min_) * static_cast<float>(page)) / static_cast<float>(pageCount_ - 1));
    return value;
}

bool cGZWinScrollbar::SetMinMax(int minimum, int maximum) {   // @ 0x10027b560
    SetMinimumValue(minimum);
    SetMaximumValue(maximum);
    return true;
}

bool cGZWinScrollbar::GetMinMax(int& minimum, int& maximum) const {   // @ 0x10027b5d4
    minimum = min_;
    maximum = max_;
    return true;
}

bool cGZWinScrollbar::SetOrientation(int orientation) {   // @ 0x10027b3a4
    direction_ = orientation == 1 ? 1 : (orientation == 2 ? 2 : 0);
    return true;
}

int cGZWinScrollbar::GetOrientation() const {   // @ 0x10027b3de
    return direction_ == 1 ? 1 : (direction_ == 2 ? 2 : 0);
}

bool cGZWinScrollbar::SetPageSize(int size) {   // @ 0x10027b414
    if (size != 0) {
        pageCount_ = static_cast<std::uint32_t>((max_ - min_) / size);
        CalculateAllMetrics();
    }
    return size != 0;
}

std::uint32_t cGZWinScrollbar::GetPageSize() const {   // @ 0x10027b478
    return pageCount_ == 0 ? 0 : static_cast<std::uint32_t>((max_ + 1) - min_) / pageCount_;
}

bool cGZWinScrollbar::SetLineSize(int size) {   // @ 0x10027b4bc
    if (size != 0) {
        lineCount_ = static_cast<std::uint32_t>((max_ - min_) / size);
        CalculateAllMetrics();
    }
    return size != 0;
}

std::uint32_t cGZWinScrollbar::GetLineSize() const {   // @ 0x10027b520
    return lineCount_ == 0 ? 0 : static_cast<std::uint32_t>(max_ - min_) / lineCount_;
}

bool cGZWinScrollbar::SetImage(cIGZBuffer* image, const cRZRect* area) {   // @ 0x10027b200
    if (image) image->AddRef();
    if (image_) image_->Release();
    image_ = image;
    bool ok = true;
    if (image) {
        const cRZRect source = area ? *area : *image->GetBufferArea();
        const int cell = (source.right - source.left) / kCells;
        segment_ = cRZRect{source.left, source.top, source.left + cell, source.bottom};
        if (direction_ == kSliderVertical) {
            SetW(cell);
            segment_.left += cell * 6;      // vertical bars use the second half of the image
            segment_.right += cell * 6;
        } else if (direction_ == kSliderHorizontal) {
            SetH(source.bottom - source.top);
        } else {
            ok = false;
        }
    }
    return ok;
}

bool cGZWinScrollbar::GetImage(cRZRect& area, cIGZBuffer*& image, int& flags) const {   // @ 0x10027b63e
    area = segment_;
    image = image_;
    flags = 0;
    return image != nullptr;
}

bool cGZWinScrollbar::GetMinMaxInfo(cRZRect& limits) const {   // @ 0x10027b69a
    if (direction_ == kSliderVertical) {
        limits = cRZRect{GetThumbWidth(), GetThumbHeight() * 3, GetThumbWidth(), 0xffff};
    } else if (direction_ == kSliderHorizontal) {
        limits = cRZRect{GetThumbWidth() * 3, GetThumbHeight(), 0xffff, GetThumbHeight()};
    } else {
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------
// Painting

bool cGZWinScrollbar::GZPaint() {   // @ 0x10027aa50
    if (GZAmCapturing() && armed_ && static_cast<std::uint32_t>(pressed_ - 1) < 4) {
        if (repeat_.GetElapsedTimeU32() > kRepeatMilliseconds) {
            switch (pressed_) {
                case kScrollPreviousLine: MoveLinePrevious(); break;
                case kScrollNextLine: MoveLineNext(); break;
                case kScrollPreviousPage: MovePagePrevious(); break;
                case kScrollNextPage: MovePageNext(); break;
                default: break;
            }
            repeat_.Restart();
        }
        InvalidateSelfAndParents();
    }
    if (image_ == nullptr) PaintFlat(); else PaintBmp();
    return true;
}

void cGZWinScrollbar::PaintFlat() {   // @ 0x10027a760
    cIGZDrawContext* ctx = static_cast<cIGZDrawContext*>(drawContext_);
    const int tw = GetThumbWidth(), th = GetThumbHeight();
    const std::uint32_t color = GetFillColorRGB();
    const int l = drawArea_.left, t = drawArea_.top;
    ctx->SetColor(color);
    if (direction_ == kSliderHorizontal) {
        ctx->Fill(cRZRect{l, t, l + tw, t + th});                         // previous button
        const int nextLeft = drawArea_.right - tw;
        ctx->SetColor(~color);
        ctx->Fill(cRZRect{l + tw, t, nextLeft, t + th});                  // track
        ctx->SetColor(color);
        ctx->Fill(cRZRect{nextLeft, t, drawArea_.right, t + th});         // next button
        const int thumbLeft = thumb_.left + l;
        ctx->Fill(cRZRect{thumbLeft, t, thumbLeft + tw, t + th});         // thumb
    } else {
        ctx->Fill(cRZRect{l, t, l + tw, t + th});
        const int nextTop = drawArea_.bottom - th;
        ctx->SetColor(~color);
        ctx->Fill(cRZRect{l, t + th, l + tw, nextTop});
        ctx->SetColor(color);
        ctx->Fill(cRZRect{l, nextTop, l + tw, drawArea_.bottom});
        const int thumbTop = thumb_.top + t;
        ctx->Fill(cRZRect{l, thumbTop, l + tw, thumbTop + th});
    }
}

void cGZWinScrollbar::PaintBmp() {   // @ 0x10027a420
    cIGZDrawContext* ctx = static_cast<cIGZDrawContext*>(drawContext_);
    const int w = segment_.right - segment_.left, h = segment_.bottom - segment_.top;
    auto cell = [&](int index) { return cRZRect{segment_.left + w * index, segment_.top, segment_.right + w * index, segment_.bottom}; };
    const int l = drawArea_.left, t = drawArea_.top;
    // cells: 0 previous button (1 pressed), 2 thumb, 3 track, 4 next button (5 pressed)
    const int previousCell = pressed_ == kScrollPreviousLine ? 1 : 0;
    const int nextCell = pressed_ == kScrollNextLine ? 5 : 4;
    if (direction_ == kSliderHorizontal) {
        ctx->Blt(image_, cell(previousCell), cRZRect{l, t, l + w, t + h});
        ctx->BltTiled(image_, cell(3), cRZRect{l + w, t, drawArea_.right - w, t + h}, 0, 0);
        ctx->Blt(image_, cell(nextCell), cRZRect{drawArea_.right - w, t, drawArea_.right, t + h});
        const int thumbLeft = thumb_.left + l;
        ctx->Blt(image_, cell(2), cRZRect{thumbLeft, t, thumbLeft + w, t + h});
    } else {
        ctx->Blt(image_, cell(previousCell), cRZRect{l, t, l + w, t + h});
        ctx->BltTiled(image_, cell(3), cRZRect{l, t + h, l + w, drawArea_.bottom - h}, 0, 0);
        ctx->Blt(image_, cell(nextCell), cRZRect{l, drawArea_.bottom - h, l + w, drawArea_.bottom});
        const int thumbTop = thumb_.top + t;
        ctx->Blt(image_, cell(2), cRZRect{l, thumbTop, l + w, thumbTop + h});
    }
}

}  // namespace gz
