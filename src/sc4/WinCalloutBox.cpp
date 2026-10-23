// cSC4WinCalloutBox — see WinCalloutBox.h for the layout and the seams.
//
// The original reaches three things the port cannot call directly: WCM()'s font lookup (vslot 0x28, here
// gz::WinFontSource()), the graphic system's buffer factory (RZ::GS() vslot 0x20, here IWinCalloutBoxServices) and the
// full cIGZFont interface of the fonts (IWinFont::AsFont()).
#include "sc4/WinCalloutBox.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "gz/DrawContext.h"
#include "gz/Font.h"
#include "gz/RZDraw.h"

namespace sc4 {

namespace {

IWinCalloutBoxServices* g_services = nullptr;

// Releases a member and clears it (the original stores 0 into the field first, then calls Release on the saved pointer).
template <class T>
void ReleaseAndClear(T*& member) {
    T* old = member;
    member = nullptr;
    if (old) old->Release();
}

// The original's 0x8010 lock flags (0x8000 = write, 0x10 = the callout's own flag).
constexpr auto kCalloutLock = static_cast<cIGZBuffer::eLockFlags>(0x8010);

// WCM() vslot 0x28 (SystemFont) through the port's font seam.
gz::IWinFont* LookupFont(std::uint32_t style) {
    gz::IWinFontSource* source = gz::WinFontSource();
    return (source && source->Available()) ? source->Font(style) : nullptr;
}

// The full cIGZFont behind an IWinFont (a font without one measures and draws nothing).
cIGZFont* FullFont(gz::IWinFont* font) { return font ? font->AsFont() : nullptr; }

// RZDrawBltTiled @ 0x1006a3062 with the callout's arguments (no offsets, no clip).
void BlitTiled(cIGZBuffer* dest, cIGZBuffer* source, const cRZRect& sourceRect, const cRZRect& destRect) {
    RZDrawBltTiled(dest, source, sourceRect, destRect, 0, 0, nullptr);
}

// The colour SetBackgroundImage / SetIcon / SetIconMeter make transparent: RZ::GS() vslot 0x118 (0xff, 0, 0xff).
std::uint32_t TransparentKey() {
    IWinGraphics* graphics = WinGraphics();
    return graphics ? graphics->MakeNativeColor(0xff, 0, 0xff) : 0;
}

bool SameRect(const cRZRect& a, const cRZRect& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}

}  // namespace

IWinCalloutBoxServices* CalloutBoxServices() { return g_services; }
void SetCalloutBoxServices(IWinCalloutBoxServices* services) { g_services = services; }

// The original takes no argument (it lets cGZWin's constructor find RZ::WM()); the port passes the manager in.
cSC4WinCalloutBox::cSC4WinCalloutBox(cIGZWinMgr* mgr) : cGZWin(mgr) {
    // 0x1003c5e30 ends with `this[0x14a] |= 0x20`: a direct byte OR of cGZWin's flag word (byte 2), i.e. the
    // kWinIgnoreCursor bit without SetFlag's side effects.
    flags_ |= kWinIgnoreCursor;
}

cSC4WinCalloutBox::~cSC4WinCalloutBox() {
    // 0x1003c5f66: Shutdown first, then release whatever it left behind. Its own release order is +0x208, +0x200,
    // +0x1f8, +0x1f0, +0x1e8, +0x1e0, +0x1d8.
    Shutdown();
    ReleaseAndClear(skinBuffer_);
    ReleaseAndClear(textBuffer_);
    ReleaseAndClear(meterImage_);
    ReleaseAndClear(textFont_);
    ReleaseAndClear(titleFont_);
    ReleaseAndClear(icon_);
    ReleaseAndClear(backgroundImage_);
}

bool cSC4WinCalloutBox::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cISC4WinCalloutBox) {
        *out = static_cast<cISC4WinCalloutBox*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

std::uint32_t cSC4WinCalloutBox::AddRef() { return cGZWin::AddRef(); }

std::uint32_t cSC4WinCalloutBox::Release() { return cGZWin::Release(); }

cIGZWin* cSC4WinCalloutBox::AsIGZWin() { return static_cast<cGZWin*>(this); }

bool cSC4WinCalloutBox::Init() {
    if (get_init_flag()) return true;
    SetID(0x2aab8cc1);
    SetFlag(kWinPrivateBuffer, false);
    SetFlag(kWinFlag4, true);
    cGZWin::Init();
    // The original calls RZ::WCM() here with an unused result: the two setters below ask it for the fonts themselves.
    if (!textFont_) SetTextFontStyle(gz::kWinDefaultFontStyle);
    if (!titleFont_) SetTitleFontStyle(gz::kWinDefaultFontStyle);
    // Quirk (kept): only the width of the constraint rectangle is tested for emptiness, not the height.
    if (constraintRect_.right == constraintRect_.left) {
        cRZRect mainArea;
        if (cIGZWin* mainWindow = mgr_ ? mgr_->GetMainWindow() : nullptr) mainWindow->GetArea(mainArea);
        cRZRect parentArea;
        if (cIGZWin* parent = GetParentWin()) parent->GetAreaAbsolute(parentArea);
        const cRZRect area{mainArea.left - parentArea.left, mainArea.top - parentArea.top,
                           mainArea.right - parentArea.left, mainArea.bottom - parentArea.top};
        SetArea(area);
        constraintRect_ = cRZRect{mainArea.left + 0x10, mainArea.top + 0x10, mainArea.right - 0x10,
                                  mainArea.bottom - 0x10};
    }
    return true;
}

bool cSC4WinCalloutBox::Shutdown() {
    if (!get_init_flag()) return true;
    ReleaseAndClear(backgroundImage_);   // +0x1d8
    ReleaseAndClear(icon_);              // +0x1e0
    ReleaseAndClear(textBuffer_);        // +0x200
    ReleaseAndClear(skinBuffer_);        // +0x208
    ReleaseAndClear(textFont_);          // +0x1f0
    ReleaseAndClear(titleFont_);         // +0x1e8
    // Quirk (kept): the meter image (+0x1f8) is not released here; only the destructor releases it.
    return cGZWin::Shutdown();
}

void cSC4WinCalloutBox::SetBackgroundImage(cIGZBuffer* image, int marginX, int marginY) {
    cIGZBuffer* old = backgroundImage_;
    if (old != image) {
        if (image) image->AddRef();
        backgroundImage_ = image;
        if (old) {
            old->Release();
            image = backgroundImage_;   // the original reloads the field after the release
        }
        dirtyFlags_ |= kCalloutFlagMeasure | kCalloutFlagPlace;
        if (image) image->SetTransparency(TransparentKey());
        InvalidateSelfAndParents();
    }
    backgroundMarginX_ = (marginX < 0) ? 8 : marginX;
    backgroundMarginY_ = (marginY < 0) ? 3 : marginY;
}

void cSC4WinCalloutBox::SetTitle(const cIGZString& title) {
    if (title_.IsEqualToStringPointer(&title, true)) return;
    title_.AssignFromIGZString(title);
    dirtyFlags_ |= kCalloutFlagMeasure;
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetText(const cIGZString& text) {
    if (text_.IsEqualToStringPointer(&text, true)) return;
    text_.AssignFromIGZString(text);
    dirtyFlags_ |= kCalloutFlagMeasure;
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetTitleFontStyle(std::uint32_t style) {
    gz::IWinFont* font = LookupFont(style);
    gz::IWinFont* old = titleFont_;
    if (old != font) {
        if (font) font->AddRef();
        titleFont_ = font;
        if (old) old->Release();
    }
    dirtyFlags_ |= kCalloutFlagMeasure;
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetTextFontStyle(std::uint32_t style) {
    gz::IWinFont* font = LookupFont(style);
    gz::IWinFont* old = textFont_;
    if (old != font) {
        if (font) font->AddRef();
        textFont_ = font;
        if (old) old->Release();
    }
    dirtyFlags_ |= kCalloutFlagMeasure;
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetIcon(cIGZBuffer* icon) {
    cIGZBuffer* old = icon_;
    if (old == icon) return;
    if (icon) icon->AddRef();
    icon_ = icon;
    if (old) {
        old->Release();
        icon = icon_;   // the original reloads the field after the release
    }
    dirtyFlags_ |= kCalloutFlagMeasure;
    if (icon) icon->SetTransparency(TransparentKey());
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetIconMeter(cIGZBuffer* image, std::uint32_t count, float value) {
    const float clampedLow = (0.0f <= value) ? value : 0.0f;   // max(value, 0); NaN becomes 0 (the original's compare)
    cIGZBuffer* old = meterImage_;
    if (old != image) {
        if (image) image->AddRef();
        meterImage_ = image;
        if (old) {
            old->Release();
            image = meterImage_;
        }
        dirtyFlags_ |= kCalloutFlagMeasure;
        if (image) image->SetTransparency(TransparentKey());
        InvalidateSelfAndParents();
    }
    const float clamped = (clampedLow <= 1.0f) ? clampedLow : 1.0f;   // min(value, 1)
    if (meterCount_ != count) {
        meterCount_ = count;
        dirtyFlags_ |= kCalloutFlagMeasure;
        InvalidateSelfAndParents();
    }
    // The original also checks both operands for NaN: an unordered compare never reports equal, so `==` alone keeps
    // the same behaviour (a NaN value always invalidates).
    if (clamped == meterValue_ && !std::isnan(clamped) && !std::isnan(meterValue_)) return;
    meterValue_ = clamped;
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetPointOfInterest(const cRZPoint& point) {
    if (pointOfInterest_.x == point.x && pointOfInterest_.y == point.y) return;
    pointOfInterest_ = point;
    dirtyFlags_ |= kCalloutFlagPlace;
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetAvoidanceRect(const cRZRect& rect) {
    if (SameRect(avoidanceRect_, rect)) return;
    avoidanceRect_ = rect;
    dirtyFlags_ |= kCalloutFlagPlace;
    InvalidateSelfAndParents();
}

void cSC4WinCalloutBox::SetConstraintRect(const cRZRect& rect) {
    if (SameRect(constraintRect_, rect)) return;
    constraintRect_ = rect;
    dirtyFlags_ |= kCalloutFlagPlace;
    InvalidateSelfAndParents();
}

// Measure the text, size the box, compose the title/text buffer; place the box and its beak; compose the skin buffer
// and blit the finished box, its icon and its meter through the window's draw context.
bool cSC4WinCalloutBox::GZPaint() {
    // The 5x5 skin cell size (a fifth of the background image; 16 without one) and the icon cell.
    int cellW = 16;
    int cellH = 16;
    if (backgroundImage_) {
        cellW = static_cast<int>(backgroundImage_->Width() / 5);
        cellH = static_cast<int>(backgroundImage_->Height() / 5);
    }
    int iconSpan = 0;   // the icon's width plus 4
    int iconHeight = 0;
    if (icon_) {
        iconSpan = static_cast<int>(icon_->Width()) + 4;
        iconHeight = static_cast<int>(icon_->Height());
    }

    // ---- 1. measure: title, text and meter, then the private title/text buffer ------------------------------------
    if (dirtyFlags_ & kCalloutFlagMeasure) {
        cRZRect titleArea{0, 0, 0, 0};
        cRZRect bodyArea{0, 0, 0, 0};
        const int wrapWidth = (constraintRect_.bottom - constraintRect_.top) - backgroundMarginX_ * 2;
        contentWidth_ = 0;
        contentHeight_ = 0;
        if (title_.Strlen() != 0) {
            if (cIGZFont* font = FullFont(titleFont_)) {
                if (font->CalculateTextArea(title_.ToChar(), title_.Strlen(), titleArea, 0, 0xfa, wrapWidth)) {
                    contentWidth_ = (titleArea.right + 4) - titleArea.left;
                    contentHeight_ = titleArea.bottom - titleArea.top;
                }
            }
        }
        titleBlockTop_ = contentHeight_ + 3 + backgroundMarginY_;
        if (text_.Strlen() != 0) {
            if (cIGZFont* font = FullFont(textFont_)) {
                if (font->CalculateTextArea(text_.ToChar(), text_.Strlen(), bodyArea, 0, 0xfa, wrapWidth)) {
                    const int width = bodyArea.right - bodyArea.left;
                    if (contentWidth_ < width) contentWidth_ = width;
                    if (contentHeight_ > 0) contentHeight_ += 4;
                    contentHeight_ += bodyArea.bottom - bodyArea.top;
                }
            }
        }
        if (!meterImage_) {
            meterRect_ = cRZRect{};
        } else {
            const int span = static_cast<int>(meterImage_->Width()) * static_cast<int>(meterCount_);
            const int height = static_cast<int>(meterImage_->Height());
            if (contentWidth_ < span) contentWidth_ = span;
            contentHeight_ = (height >> 1) + 8 + contentHeight_;
            meterRect_.left = (contentWidth_ - span) >> 1;
            meterRect_.right = meterRect_.left + span;
            meterRect_.top = (titleArea.bottom - titleArea.top) + 4;
            meterRect_.bottom = meterRect_.top + (height >> 1);
        }
        boxWidth_ = contentWidth_ + iconSpan + backgroundMarginX_ * 2;
        boxHeight_ = std::max(contentHeight_, iconHeight) + backgroundMarginY_ * 2;
        contentHeight_ += 2;   // +0x21c ends up as the text buffer's height

        // Quirk (kept): the test compares the buffer against the box size (+0x220/+0x224) but Init gets the content
        // size (+0x218/+0x21c), so any measure that changes the text makes a fresh text buffer even when the content
        // size did not change.
        if (textBuffer_ == nullptr || static_cast<int>(textBuffer_->Width()) != boxWidth_ ||
            static_cast<int>(textBuffer_->Height()) != boxHeight_) {
            if (IWinCalloutBoxServices* services = CalloutBoxServices()) {
                ReleaseAndClear(textBuffer_);
                textBuffer_ = services->CreateBuffer();
            }
            if (textBuffer_) {
                textBuffer_->Init(static_cast<std::uint32_t>(contentWidth_), static_cast<std::uint32_t>(contentHeight_),
                                  cGZBufferColorType{1, 0});
            }
        }
        if (textBuffer_ && textBuffer_->Lock(kCalloutLock)) {
            if (backgroundImage_) {
                const int top = titleBlockTop_ - (boxHeight_ - contentHeight_) / 2;
                const int bottom = contentHeight_ - top;
                if (top > 0) {
                    BlitTiled(textBuffer_, backgroundImage_, cRZRect{cellW, cellH, cellW * 2, cellH * 2},
                              cRZRect{0, 0, contentWidth_, top});
                }
                if (bottom > 0) {
                    BlitTiled(textBuffer_, backgroundImage_, cRZRect{cellW, cellH * 2, cellW * 2, cellH * 3},
                              cRZRect{0, contentHeight_ - bottom, contentWidth_, contentHeight_});
                }
            }
            if (cIGZFont* titleFont = FullFont(titleFont_); titleFont && titleFont->LockSurfaceForFontUse(textBuffer_)) {
                const std::uint32_t color = textBuffer_->ConvertRGBValueToNative(0, 0, 0);
                if (title_.Strlen() != 0) {
                    const cRZRect area{0, 0, titleArea.right - titleArea.left, titleArea.bottom - titleArea.top};
                    titleFont->DrawTextParagraph(textBuffer_, area, title_.ToChar(), title_.Strlen(), 0, color,
                                                 kFontDefaultColor, nullptr, nullptr);
                }
                if (text_.Strlen() != 0) {
                    cRZRect area{0, 0, bodyArea.right - bodyArea.left, bodyArea.bottom - bodyArea.top};
                    if (title_.Strlen() != 0) {
                        const int dy = (titleArea.bottom - titleArea.top) + 4;
                        area.top += dy;
                        area.bottom += dy;
                    }
                    if (meterImage_) {
                        const int dy = (meterRect_.bottom - meterRect_.top) + 4;
                        area.top += dy;
                        area.bottom += dy;
                    }
                    if (cIGZFont* textFont = FullFont(textFont_)) {
                        textFont->DrawTextParagraph(textBuffer_, area, text_.ToChar(), text_.Strlen(), 0, color,
                                                    kFontDefaultColor, nullptr, nullptr);
                    }
                }
                titleFont->UnlockSurfaceForFontUse(textBuffer_);
            }
            // Quirk (kept): the text buffer is locked with 0x8010 and never unlocked - the original's only Unlock is
            // the skin buffer's (0x1003c7ecb), so the text buffer's lock count grows by one on every repaint.
        }
        dirtyFlags_ |= kCalloutFlagPlace | kCalloutFlagDraw;
    }

    // ---- 2. place: pick a side and compute the box, dirty and beak rectangles -------------------------------------
    if (dirtyFlags_ & kCalloutFlagPlace) {
        const int below = constraintRect_.bottom - pointOfInterest_.y;
        const int above = pointOfInterest_.y - constraintRect_.top;
        const int right = constraintRect_.right - pointOfInterest_.x;
        const int left = pointOfInterest_.x - constraintRect_.left;
        const bool verticalTight = (below <= cellW) || (above <= cellW);
        int side = kCalloutSideBoxLeft;
        bool placed = false;
        if (!verticalTight && left < cellW) {
            side = kCalloutSideBoxRight;
            placed = true;
        }
        if (!placed) {
            side = kCalloutSideBoxLeft;
            if (verticalTight || right >= cellW) {
                if (above > boxHeight_ - avoidanceRect_.top) {
                    side = kCalloutSideBoxAbove;
                } else if (below > boxHeight_ + avoidanceRect_.bottom) {
                    side = kCalloutSideBoxBelow;
                } else if (left > boxWidth_ - avoidanceRect_.left) {
                    side = kCalloutSideBoxLeft;
                } else {
                    side = static_cast<int>(kCalloutSideBoxAbove) + ((right > boxWidth_ + avoidanceRect_.right) ? 1 : 0);
                }
            }
        }
        if (side != side_) {
            side_ = static_cast<std::uint8_t>(side);
            dirtyFlags_ |= kCalloutFlagDraw;
        }

        int beakLeft = beakRect_.left;
        int beakTop = beakRect_.top;
        int beakRight = beakRect_.right;
        int beakBottom = beakRect_.bottom;
        switch (side_) {
            case kCalloutSideBoxLeft: {   // the box is left of the point, the beak points right
                const int twoCells = cellW * 2;
                int height = boxHeight_;
                if (height < twoCells) {
                    boxHeight_ = twoCells;
                    height = twoCells;
                }
                const int edge = pointOfInterest_.x + avoidanceRect_.left;
                boxRect_.left = edge - boxWidth_;
                boxRect_.right = edge;
                const int centred = pointOfInterest_.y - height / 2;
                int top = constraintRect_.bottom - height;
                if (centred <= constraintRect_.bottom - height) top = centred;
                if (centred < constraintRect_.top) top = constraintRect_.top;
                boxRect_.top = top;
                boxRect_.bottom = height + top;
                dirtyRect_ = boxRect_;
                dirtyRect_.right += (cellW * 3) / 2;
                int beakY = boxRect_.bottom - cellH;
                if (pointOfInterest_.y <= beakY) beakY = pointOfInterest_.y;
                if (pointOfInterest_.y < top + cellH) beakY = top + cellH;
                const int half = cellW / 2;
                beakTop = beakY - half;
                beakBottom = beakTop + cellH;
                beakLeft = (half - cellW) + edge;
                beakRight = beakLeft + twoCells;
                break;
            }
            case kCalloutSideBoxAbove: {   // the box is above the point, the beak points down
                int width = boxWidth_;
                if (width < cellW * 3) {
                    boxWidth_ = cellW * 3;
                    width = cellW * 3;
                }
                const int minHeight = (cellW * 3) >> 1;
                int height = boxHeight_;
                if (height < minHeight) {
                    boxHeight_ = minHeight;
                    height = minHeight;
                }
                const int half = cellW / 2;
                const int offset = half + cellW;
                const int centred = pointOfInterest_.x - width / 2;
                int edge = constraintRect_.right - width;
                if (centred <= edge) edge = centred;
                if (centred < constraintRect_.left) edge = constraintRect_.left;
                boxRect_.left = edge;
                boxRect_.right = width + edge;
                const int pointEdge = pointOfInterest_.y + avoidanceRect_.top;
                boxRect_.top = pointEdge - height;
                boxRect_.bottom = pointEdge;
                dirtyRect_ = boxRect_;
                dirtyRect_.bottom += (cellH * 3) / 2;
                int lower = edge + offset;
                int upper = (width + edge) - offset;
                if (pointOfInterest_.x <= upper) upper = pointOfInterest_.x;
                if (pointOfInterest_.x < lower) upper = lower;
                beakLeft = upper - half;
                beakRight = beakLeft + cellW;
                beakTop = ((cellH >> 1) - cellH) + pointEdge;
                beakBottom = beakTop + cellH * 2;
                break;
            }
            case kCalloutSideBoxRight: {   // the box is right of the point, the beak points left
                int height = boxHeight_;
                if (height < cellW * 2) {
                    boxHeight_ = cellW * 2;
                    height = cellW * 2;
                }
                const int edge = pointOfInterest_.x + avoidanceRect_.right;
                boxRect_.left = edge;
                boxRect_.right = boxWidth_ + edge;
                const int centred = pointOfInterest_.y - height / 2;
                int top = constraintRect_.bottom - height;
                if (centred <= constraintRect_.bottom - height) top = centred;
                if (centred < constraintRect_.top) top = constraintRect_.top;
                boxRect_.top = top;
                boxRect_.bottom = height + top;
                dirtyRect_ = boxRect_;
                dirtyRect_.left -= (cellW * 3) / 2;
                int beakY = boxRect_.bottom - cellH;
                if (pointOfInterest_.y <= beakY) beakY = pointOfInterest_.y;
                if (pointOfInterest_.y < top + cellH) beakY = top + cellH;
                const int half = cellW / 2;
                beakTop = beakY - half;
                beakBottom = beakTop + cellH;
                beakRight = half + edge;
                beakLeft = beakRight - cellW * 2;
                break;
            }
            case kCalloutSideBoxBelow: {   // the box is below the point, the beak points up
                int width = boxWidth_;
                if (width < cellW * 3) {
                    boxWidth_ = cellW * 3;
                    width = cellW * 3;
                }
                const int minHeight = (cellW * 3) >> 1;
                int height = boxHeight_;
                if (height < minHeight) {
                    boxHeight_ = minHeight;
                    height = minHeight;
                }
                const int half = cellW / 2;
                const int offset = half + cellW;
                const int centred = pointOfInterest_.x - width / 2;
                int edge = constraintRect_.right - width;
                if (centred <= edge) edge = centred;
                if (centred < constraintRect_.left) edge = constraintRect_.left;
                boxRect_.left = edge;
                boxRect_.right = width + edge;
                const int pointEdge = pointOfInterest_.y + avoidanceRect_.bottom;
                boxRect_.top = pointEdge;
                boxRect_.bottom = height + pointEdge;
                dirtyRect_ = boxRect_;
                dirtyRect_.top -= (cellH * 3) / 2;
                int lower = edge + offset;
                int upper = (width + edge) - offset;
                if (pointOfInterest_.x <= upper) upper = pointOfInterest_.x;
                if (pointOfInterest_.x < lower) upper = lower;
                beakLeft = upper - half;
                beakRight = beakLeft + cellW;
                const int bottom = (cellH >> 1) + pointEdge;
                beakBottom = bottom;
                beakTop = bottom - cellH * 2;
                break;
            }
            default:
                break;
        }
        const cRZRect beak{beakLeft, beakTop, beakRight, beakBottom};
        if (!SameRect(beakRect_, beak)) {
            beakRect_ = beak;
            dirtyFlags_ |= kCalloutFlagDraw;
        }
    }

    // ---- 3. draw: compose the 5x5 skin into the skin buffer, then blit everything through the draw context --------
    if (dirtyFlags_ & kCalloutFlagDraw) {
        const cRZRect beak{beakRect_.left - dirtyRect_.left, beakRect_.top - dirtyRect_.top,
                           beakRect_.right - dirtyRect_.left, beakRect_.bottom - dirtyRect_.top};
        const int dirtyWidth = dirtyRect_.right - dirtyRect_.left;
        const int dirtyHeight = dirtyRect_.bottom - dirtyRect_.top;
        if (skinBuffer_ == nullptr || static_cast<int>(skinBuffer_->Width()) != dirtyWidth ||
            static_cast<int>(skinBuffer_->Height()) != dirtyHeight) {
            if (CalloutBoxServices()) {
                ReleaseAndClear(skinBuffer_);
                skinBuffer_ = CalloutBoxServices()->CreateBuffer();
            }
            if (skinBuffer_) {
                skinBuffer_->Init(static_cast<std::uint32_t>(dirtyWidth), static_cast<std::uint32_t>(dirtyHeight),
                                  cGZBufferColorType{1, 0});
            }
        }
        if (skinBuffer_ && skinBuffer_->Lock(kCalloutLock)) {
            skinBuffer_->Fill(0, nullptr);
            if (backgroundImage_) {
                const int x0 = boxRect_.left - dirtyRect_.left;
                const int y0 = boxRect_.top - dirtyRect_.top;
                const int x1 = boxRect_.right - dirtyRect_.left;
                const int y1 = boxRect_.bottom - dirtyRect_.top;
                const int titleTop = titleBlockTop_;
                const int bandTop = y0 + cellH;
                const int belowTitle = y1 - bandTop - titleTop;
                if (titleTop != cellH) {
                    // row 1..2: the band behind the title (left corner, right corner, tiled middle)
                    BlitTiled(skinBuffer_, backgroundImage_, cRZRect{0, cellH, cellW, cellH * 2},
                              cRZRect{x0, bandTop, x0 + cellW, y0 + titleTop});
                    BlitTiled(skinBuffer_, backgroundImage_, cRZRect{cellW * 3, cellH, cellW * 4, cellH * 2},
                              cRZRect{x1 - cellW, bandTop, x1, y0 + titleTop});
                    BlitTiled(skinBuffer_, backgroundImage_, cRZRect{cellW, cellH, cellW * 2, cellH * 2},
                              cRZRect{x0 + cellW, bandTop, x1 - cellW, y0 + titleTop});
                }
                const int lowTop = y1 - cellH;
                if (belowTitle != 0) {
                    // row 2..3: the body band, only when the title band does not reach the bottom band
                    const int bodyTop = y1 - cellH - belowTitle;
                    BlitTiled(skinBuffer_, backgroundImage_, cRZRect{0, cellH * 2, cellW, cellH * 3},
                              cRZRect{x0, bodyTop, x0 + cellW, lowTop});
                    BlitTiled(skinBuffer_, backgroundImage_, cRZRect{cellW * 3, cellH * 2, cellW * 4, cellH * 3},
                              cRZRect{x1 - cellW, bodyTop, x1, lowTop});
                    BlitTiled(skinBuffer_, backgroundImage_, cRZRect{cellW, cellH * 2, cellW * 2, cellH * 3},
                              cRZRect{x0 + cellW, bodyTop, x1 - cellW, lowTop});
                }
                // the four corner cells (one cell each, so a plain blit)
                skinBuffer_->Blt(backgroundImage_, cRZRect{0, 0, cellW, cellH},
                                 cRZRect{x0, y0, x0 + cellW, y0 + cellH}, nullptr);
                skinBuffer_->Blt(backgroundImage_, cRZRect{cellW * 3, 0, cellW * 4, cellH},
                                 cRZRect{x1 - cellW, y0, x1, y0 + cellH}, nullptr);
                skinBuffer_->Blt(backgroundImage_, cRZRect{0, cellH * 3, cellW, cellH * 4},
                                 cRZRect{x0, lowTop, x0 + cellW, y1}, nullptr);
                skinBuffer_->Blt(backgroundImage_, cRZRect{cellW * 3, cellH * 3, cellW * 4, cellH * 4},
                                 cRZRect{x1 - cellW, lowTop, x1, y1}, nullptr);
                // row 0..1 and row 3..4: the top and bottom edges
                BlitTiled(skinBuffer_, backgroundImage_, cRZRect{cellW, 0, cellW * 2, cellH},
                          cRZRect{x0 + cellW, y0, x1 - cellW, y0 + cellH});
                BlitTiled(skinBuffer_, backgroundImage_, cRZRect{cellW, cellH * 3, cellW * 2, cellH * 4},
                          cRZRect{x0 + cellW, lowTop, x1 - cellW, y1});
                // the beak: one cell of column 4 / row 4, picked by the side
                switch (side_) {
                    case kCalloutSideBoxLeft:
                        skinBuffer_->Blt(backgroundImage_, cRZRect{cellW * 2, cellH * 4, cellW * 4, cellH * 5}, beak,
                                         nullptr);
                        break;
                    case kCalloutSideBoxAbove:
                        skinBuffer_->Blt(backgroundImage_, cRZRect{cellW * 4, cellH * 2, cellW * 5, cellH * 4}, beak,
                                         nullptr);
                        break;
                    case kCalloutSideBoxRight:
                        skinBuffer_->Blt(backgroundImage_, cRZRect{0, cellH * 4, cellW * 2, cellH * 5}, beak,
                                         nullptr);
                        break;
                    case kCalloutSideBoxBelow:
                        skinBuffer_->Blt(backgroundImage_, cRZRect{cellW * 4, 0, cellW * 5, cellH * 2}, beak,
                                         nullptr);
                        break;
                    default:
                        break;
                }
            }
            if (textBuffer_) {
                // the title + text block, right of the icon, centred vertically in the box
                const int left = (boxRect_.left - dirtyRect_.left) + iconSpan + backgroundMarginX_;
                const int top = (boxRect_.top - dirtyRect_.top) + (boxHeight_ - contentHeight_) / 2;
                const cRZRect block{left, top, left + contentWidth_, top + contentHeight_};
                skinBuffer_->Blt(textBuffer_, *textBuffer_->GetBufferArea(), block, nullptr);
            }
            skinBuffer_->Unlock(kCalloutLock);
        }
    }

    // ---- 4. blit the finished box, its icon and its meter through the draw context (in parent coordinates) --------
    dirtyFlags_ = 0;
    cRZRect parentArea;
    if (cIGZWin* parent = GetParentWin()) parent->GetAreaAbsolute(parentArea);
    gz::cIGZDrawContext* context = static_cast<gz::cIGZDrawContext*>(drawContext_);
    if (context && skinBuffer_) {
        const cRZRect dest{dirtyRect_.left - parentArea.left, dirtyRect_.top - parentArea.top,
                           dirtyRect_.right - parentArea.left, dirtyRect_.bottom - parentArea.top};
        context->Blt(skinBuffer_, *skinBuffer_->GetBufferArea(), dest);
    }
    if (context && icon_) {
        const int width = static_cast<int>(icon_->Width());
        const int height = static_cast<int>(icon_->Height());
        cRZRect dest;
        dest.left = (backgroundMarginX_ + boxRect_.left) - parentArea.left;
        dest.top = ((boxRect_.bottom - (boxRect_.top + height)) / 2 + boxRect_.top) - parentArea.top;
        dest.right = dest.left + width;
        dest.bottom = dest.top + height;
        context->Blt(icon_, *icon_->GetBufferArea(), dest);
    }
    if (context && meterImage_) {
        const int baseX = iconSpan + boxRect_.left + backgroundMarginX_;
        const int baseY = backgroundMarginX_ + boxRect_.top;
        cRZRect dest{baseX + meterRect_.left - parentArea.left, baseY + meterRect_.top - parentArea.top,
                     baseX + meterRect_.right - parentArea.left, baseY + meterRect_.bottom - parentArea.top};
        const int meterWidth = static_cast<int>(meterImage_->Width());
        const int meterHeight = static_cast<int>(meterImage_->Height());
        // The image holds two halves: the empty one (bottom half) is tiled over the whole meter, then the filled one
        // (top half) over the part the value selects.
        const cRZRect emptyTile{0, meterHeight >> 1, meterWidth, (meterHeight >> 1) + (dest.bottom - dest.top)};
        context->BltTiled(meterImage_, emptyTile, dest, 0, 0);
        dest.right = dest.left + static_cast<int>(static_cast<float>(dest.right - dest.left) * meterValue_);
        const cRZRect filledTile{0, 0, meterWidth, dest.bottom - dest.top};
        context->BltTiled(meterImage_, filledTile, dest, 0, 0);
    }
    return true;
}

}  // namespace sc4
