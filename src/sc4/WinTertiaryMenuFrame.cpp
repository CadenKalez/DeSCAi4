// cSC4WinTertiaryMenuFrame — see WinTertiaryMenuFrame.h for the layout, the vtable order and the quirks.
//
// The original reaches RZ::GS() directly for two things the port keeps as a seam: the scratch buffer factory
// (vslot 0x20) and the window's draw buffer (cGZWin +0x98) as the full cIGZBuffer the blit needs.  Everything else
// (cGZWin's Init / Shutdown / SetFlag / SetArea / ScreenToWindowCoordinates / PostMsg / capture, the timer) is the
// recovered framework under src/gz and src/rz.
#include "sc4/WinTertiaryMenuFrame.h"

#include <cstdint>

#include "gz/RZDraw.h"

namespace sc4 {

namespace {

IWinTertiaryMenuFrameHost* g_host = nullptr;

// The original stores 0 into the field first, then calls Release on the saved pointer.
template <class T>
void ReleaseAndClear(T*& member) {
    T* old = member;
    member = nullptr;
    if (old != nullptr) old->Release();
}

// Lock flags: 0x8001 is read | write (the clear pass), 0x8010 is the original's own "compose / copy" flag.
constexpr auto kComposeLock =
    static_cast<cIGZBuffer::eLockFlags>(cIGZBuffer::kLockRead | cIGZBuffer::kLockWrite);
constexpr auto kDrawLock = static_cast<cIGZBuffer::eLockFlags>(0x8010);

}  // namespace

IWinTertiaryMenuFrameHost* WinTertiaryMenuFrameHost() { return g_host; }
void SetWinTertiaryMenuFrameHost(IWinTertiaryMenuFrameHost* host) { g_host = host; }

// The original takes no argument (cGZWin's constructor finds RZ::WM()); the port passes the manager in.
cSC4WinTertiaryMenuFrame::cSC4WinTertiaryMenuFrame(cIGZWinMgr* mgr) : cGZWin(mgr) {
    // 0x1002fe35b..0x1002fe385: the whole flag block starts cleared.  The original writes a 32-bit 0 over
    // +0x19c..+0x19f, a 16-bit 0 over +0x1a0, and a 64-bit 0 over +0x1a4..+0x1ab.
    repaintFlags_ = 0;
    upArrowEnabled_ = false;
    downArrowEnabled_ = false;
    hoverSide_ = kWinTertiaryMenuSideNone;
    heldSide_ = kWinTertiaryMenuSideNone;
    backgroundImage_ = nullptr;
    scratchBuffer_ = nullptr;
}

cSC4WinTertiaryMenuFrame::~cSC4WinTertiaryMenuFrame() {
    // 0x1002fe3e0: Shutdown, then (DestructInPlace of the timer at +0x1b0), then the scratch buffer (+0x160) and
    // the background (+0x158) — the opposite order to Shutdown's.  The timer is a plain member here, so the
    // compiler destroys it right after this body (the same Stop / held-reference release DestructInPlace does).
    Shutdown();
    ReleaseAndClear(scratchBuffer_);
    ReleaseAndClear(backgroundImage_);
}

bool cSC4WinTertiaryMenuFrame::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1002fe536
    if (iid == kGZIID_cISC4WinTertiaryMenuFrame) {
        *out = static_cast<cISC4WinTertiaryMenuFrame*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

std::uint32_t cSC4WinTertiaryMenuFrame::AddRef() { return cGZWin::AddRef(); }     // @ 0x1002fe506
std::uint32_t cSC4WinTertiaryMenuFrame::Release() { return cGZWin::Release(); }   // @ 0x1002fe51e

cIGZWin* cSC4WinTertiaryMenuFrame::AsIGZWin() { return static_cast<cGZWin*>(this); }   // @ 0x1002ff09c

bool cSC4WinTertiaryMenuFrame::Init() {   // @ 0x1002fe57e
    if (!get_init_flag()) {
        // vslot 0x220 = cGZWin::SetFlag: set flag 4, clear the private-buffer flag 0x10000.
        SetFlag(kWinFlag4, true);
        SetFlag(kWinPrivateBuffer, false);
        cGZWin::Init();
    }
    return true;
}

bool cSC4WinTertiaryMenuFrame::Shutdown() {   // @ 0x1002fe5e4
    if (get_init_flag()) {
        ReleaseAndClear(backgroundImage_);   // +0x158 first (the destructor uses the other order)
        ReleaseAndClear(scratchBuffer_);     // then +0x160
        timer_.Stop();
        return cGZWin::Shutdown();
    }
    return true;
}

// The 0x1002fe68c block: the composed buffer is blitted as a whole into whatever buffer this window draws to.
bool cSC4WinTertiaryMenuFrame::BlitToDrawBuffer() {
    if (scratchBuffer_ == nullptr) return true;   // 0x1002fe68c
    if (drawBuffer_ == nullptr) return true;      // port guard: the original assumes SetBufferToDrawTo ran
    IWinTertiaryMenuFrameHost* host = WinTertiaryMenuFrameHost();
    cIGZBuffer* destination = host ? host->WindowBuffer(drawBuffer_) : nullptr;
    if (destination == nullptr) return true;
    if (destination->Lock(kDrawLock)) {
        // vslot 0x68 of the scratch buffer supplies the source rectangle; the destination is cGZWin's draw area
        // (+0x30 in the window, object +0x38).
        destination->Blt(scratchBuffer_, *scratchBuffer_->GetBufferArea(), drawArea_, nullptr);
        destination->Unlock(kDrawLock);
    }
    return true;
}

bool cSC4WinTertiaryMenuFrame::GZPaint() {   // @ 0x1002fe668
    if ((repaintFlags_ & kWinTertiaryMenuRepaint) != 0) {
        IWinGraphics* graphics = WinGraphics();
        // The original zeroes the whole 32-bit word at +0x19c, i.e. the flag byte and its three padding bytes.
        repaintFlags_ = 0;
        const int width = GetW();
        const int height = GetH();
        if (scratchBuffer_ == nullptr || static_cast<int>(scratchBuffer_->Width()) != width ||
            static_cast<int>(scratchBuffer_->Height()) != height) {
            if (graphics != nullptr) {
                // RZ::GS() vslot 0x20: a fresh display buffer into the member (the original passes &+0x160).
                ReleaseAndClear(scratchBuffer_);
                IWinTertiaryMenuFrameHost* host = WinTertiaryMenuFrameHost();
                if (host != nullptr) scratchBuffer_ = host->CreateBuffer();
            }
            if (scratchBuffer_ == nullptr) return true;
            cGZBufferColorType type;
            type.type = kBufferColorDisplay;   // colour type 1, bits per pixel 0
            type.bitsPerPixel = 0;
            scratchBuffer_->Init(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height), type);
        }

        if (scratchBuffer_ == nullptr) return true;
        if (scratchBuffer_->Lock(kComposeLock)) {
            // vslot 0xb0 = Fill(color, clip): clear the whole scratch buffer with colour 0.
            scratchBuffer_->Fill(0, nullptr);
            scratchBuffer_->Unlock(kComposeLock);
        }

        if (scratchBuffer_ == nullptr) return true;   // the original re-tests the member here (0x1002fe7e5)
        if (scratchBuffer_->Lock(kDrawLock)) {
            // The two arrow tiles slide their source column right by two margins while the arrow is hovered and by
            // one when it is held; the held state suppresses the extra hover strip (0x1002fe812..0x1002fe868).
            int upOffset = bodyTileWidth_;
            if (upArrowEnabled_) {
                upOffset = marginX_ + bodyTileWidth_;
                if (hoverSide_ == kWinTertiaryMenuSideUp) {
                    upOffset += marginX_;
                    if (heldSide_ != kWinTertiaryMenuSideUp) upOffset += marginX_;
                }
            }
            int downOffset = bodyTileWidth_;
            if (downArrowEnabled_) {
                downOffset = marginX_ + bodyTileWidth_;
                if (hoverSide_ == kWinTertiaryMenuSideDown) {
                    downOffset += marginX_;
                    if (heldSide_ != kWinTertiaryMenuSideDown) downOffset += marginX_;
                }
            }

            // 1. Top-right corner tile (0x1002fe804).
            const cRZRect upSource{upOffset, 0, upOffset + marginX_, marginY_};
            const cRZRect upDestination{width - marginX_, 0, width, marginY_};
            scratchBuffer_->Blt(backgroundImage_, upSource, upDestination, nullptr);

            // 2. Right edge, tiled from the middle-height source column (0x1002fe8db).
            const cRZRect edgeSource{bodyTileWidth_, marginY_, marginX_ + bodyTileWidth_,
                                     innerHeight_ + marginY_};
            const cRZRect edgeDestination{width - marginX_, marginY_, width, height - marginY_};
            RZDrawBltTiled(scratchBuffer_, backgroundImage_, edgeSource, edgeDestination, 0, 0, nullptr);

            // 3. Bottom-right corner tile (0x1002fe965).
            const cRZRect downSource{downOffset, innerHeight_ + marginY_, downOffset + marginX_,
                                     innerHeight_ + 2 * marginY_};
            const cRZRect downDestination{width - marginX_, height - marginY_, width, height};
            scratchBuffer_->Blt(backgroundImage_, downSource, downDestination, nullptr);

            // 4. The body tile at the content inset (0x1002fe9fa).
            const cRZRect bodySource{0, 0, bodyTileWidth_, bodyTileHeight_};
            const cRZRect bodyDestination{0, contentInset_, bodyTileWidth_, contentInset_ + bodyTileHeight_};
            scratchBuffer_->Blt(backgroundImage_, bodySource, bodyDestination, nullptr);

            scratchBuffer_->Unlock(kDrawLock);
        }
    }
    return BlitToDrawBuffer();
}

void cSC4WinTertiaryMenuFrame::SetBackgroundImage(cIGZBuffer* image, int marginX, int marginY, int bodyTileWidth,
                                                  int bodyTileHeight, int rightColumnWidth, int anchorOffsetX,
                                                  int anchorOffsetY) {   // @ 0x1002fea92
    cIGZBuffer* old = backgroundImage_;
    if (old != image) {
        if (image != nullptr) image->AddRef();
        backgroundImage_ = image;
        if (old != nullptr) {
            old->Release();
            image = backgroundImage_;   // the original re-reads the member after releasing the old image
        }
    }
    marginX_ = marginX;
    marginY_ = marginY;
    // Quirk kept: the image is dereferenced without a null test (callers always pass one).
    innerHeight_ = static_cast<int>(image->Height()) - marginY * 2;
    bodyTileWidth_ = bodyTileWidth;
    bodyTileHeight_ = bodyTileHeight;
    rightColumnWidth_ = rightColumnWidth;
    anchorOffsetX_ = anchorOffsetX;
    anchorOffsetY_ = anchorOffsetY;
    repaintFlags_ |= kWinTertiaryMenuRepaint;
}

void cSC4WinTertiaryMenuFrame::PositionFrame(int contentWidth, int contentHeight, int anchorX, int anchorY,
                                             int minTop, int maxBottom) {   // @ 0x1002feb52
    // The original's locals (0x1002feb6d..0x1002fec64); names follow what they feed.
    const int frameWidth = (bodyTileWidth_ + marginX_) - rightColumnWidth_;
    const int frameHeight = (contentHeight > bodyTileHeight_ ? contentHeight : bodyTileHeight_) +
                            2 * marginY_;
    const int belowAnchor = anchorY - anchorOffsetY_;
    int top = (bodyTileHeight_ / 2 + belowAnchor) - frameHeight / 2;
    if (top < minTop) top = minTop;
    int clamped = maxBottom - frameHeight;
    if (top <= clamped) clamped = top;
    int limit = belowAnchor - marginY_;
    if (clamped <= limit) limit = clamped;
    int placeTop = ((marginY_ + bodyTileHeight_) - frameHeight) + belowAnchor;
    if (placeTop <= limit) placeTop = limit;

    const int left = anchorX - anchorOffsetX_;
    cGZWin::SetArea(left, placeTop, left + frameWidth, placeTop + frameHeight);

    contentLeft_ = frameWidth - (marginX_ + contentWidth) / 2 - 1;
    contentTop_ = (frameHeight - contentHeight) / 2;
    contentRight_ = contentLeft_ + contentWidth;   // the original adds x to the reduced frame width
    contentBottom_ = contentTop_ + contentHeight;
    contentInset_ = belowAnchor - placeTop;
    repaintFlags_ |= kWinTertiaryMenuRepaint;
}

bool cSC4WinTertiaryMenuFrame::IsPointInWindowWindowCoordinates(int px, int py) const {   // @ 0x1002fec82
    // Quirk kept: after the inside test this answers "is x at or right of width - margin x" (the arrow column),
    // not "is the point inside the window".
    if ((px | py) < 0) return false;
    const int width = GetW();
    if (width <= px) return false;
    const int height = GetH();
    if (height <= py) return false;
    return width - marginX_ <= px;
}

bool cSC4WinTertiaryMenuFrame::IsPointInWindowScreenCoordinates(int px, int py) const {   // @ 0x1002fecfa
    int x = px;
    int y = py;
    cGZWin::ScreenToWindowCoordinates(x, y);   // vslot 0x1d8 through the cGZWin subobject
    // The original calls the primary vtable's slot 0x68 (its own IsPointInWindowWindowCoordinates).
    return IsPointInWindowWindowCoordinates(x, y);
}

bool cSC4WinTertiaryMenuFrame::GZOnMouseMove(int px, int py, std::uint32_t flags) {   // @ 0x1002fed94
    (void)flags;
    if (px < GetW() - marginX_) {   // left of the arrow column
        if (hoverSide_ == kWinTertiaryMenuSideNone) return true;
        hoverSide_ = kWinTertiaryMenuSideNone;
    } else if (py < contentTop_) {   // top of the arrow column: the up arrow
        if (hoverSide_ == kWinTertiaryMenuSideUp) return true;
        hoverSide_ = kWinTertiaryMenuSideUp;
    } else if (py > contentBottom_) {   // below the content: the down arrow
        if (hoverSide_ == kWinTertiaryMenuSideDown) return true;
        hoverSide_ = kWinTertiaryMenuSideDown;
    } else {   // the middle of the arrow column
        if (hoverSide_ == kWinTertiaryMenuSideNone) return true;
        hoverSide_ = kWinTertiaryMenuSideNone;
    }
    repaintFlags_ |= kWinTertiaryMenuRepaint;
    InvalidateSelfAndParents();
    return true;
}

bool cSC4WinTertiaryMenuFrame::GZOnMouseExit(cIGZWin* to) {   // @ 0x1002fee2e
    (void)to;
    if (hoverSide_ != kWinTertiaryMenuSideNone) {
        hoverSide_ = kWinTertiaryMenuSideNone;
        repaintFlags_ |= kWinTertiaryMenuRepaint;
        InvalidateSelfAndParents();
    }
    return true;
}

bool cSC4WinTertiaryMenuFrame::GZOnMouseDownL(int px, int py, std::uint32_t flags) {   // @ 0x1002fee92
    (void)px;
    (void)py;
    (void)flags;
    if (hoverSide_ == kWinTertiaryMenuSideNone) return true;
    heldSide_ = hoverSide_;
    repaintFlags_ |= kWinTertiaryMenuRepaint;
    InvalidateSelfAndParents();
    if (heldSide_ != kWinTertiaryMenuSideNone) {
        // vslot 0x498 = cGZWin::PostMsg(notificationTarget, 0xca35e4ba, direction, 0, 0): -1 for up, +1 for down.
        const std::uint64_t direction =
            heldSide_ == kWinTertiaryMenuSideUp ? ~static_cast<std::uint64_t>(0) : 1;
        cGZWin::PostMsg(GetNotificationTarget(), kWinTertiaryMenuScrollMsg, direction, 0, 0);
    }
    timer_.SetCallbackFunction(&cSC4WinTertiaryMenuFrame::ScrollContentCallback, this);
    // The timer holds the window itself while armed: the original passes AsIGZWin()'s reference.
    timer_.StartPeriodic(kWinTertiaryMenuInitialScrollPeriodMS, AsIGZWin());
    cGZWin::GZSetCapture();
    return true;
}

void cSC4WinTertiaryMenuFrame::ScrollContentCallback(void* context) {   // @ 0x1002fef52
    auto* self = static_cast<cSC4WinTertiaryMenuFrame*>(context);
    if (self->heldSide_ != kWinTertiaryMenuSideNone) {
        const std::uint64_t direction =
            self->heldSide_ == kWinTertiaryMenuSideUp ? ~static_cast<std::uint64_t>(0) : 1;
        self->cGZWin::PostMsg(self->GetNotificationTarget(), kWinTertiaryMenuScrollMsg, direction, 0, 0);
    }
    self->timer_.SetPeriod(kWinTertiaryMenuScrollRepeatPeriodMS);
}

bool cSC4WinTertiaryMenuFrame::GZOnMouseUpL(int px, int py, std::uint32_t flags) {   // @ 0x1002fefc4
    (void)px;
    (void)py;
    (void)flags;
    cGZWin::GZReleaseCapture();
    if (heldSide_ != kWinTertiaryMenuSideNone) {
        heldSide_ = kWinTertiaryMenuSideNone;
        repaintFlags_ |= kWinTertiaryMenuRepaint;
        InvalidateSelfAndParents();
    }
    timer_.Stop();
    return true;
}

void cSC4WinTertiaryMenuFrame::GetContentRect(cRZRect& rect) {   // @ 0x1002ff0a6
    rect.left = contentLeft_;
    rect.top = contentTop_;
    rect.right = contentRight_;
    rect.bottom = contentBottom_;
}

void cSC4WinTertiaryMenuFrame::EnableScrollArrow(bool up, bool down) {   // @ 0x1002ff0c2
    if (upArrowEnabled_ == up && downArrowEnabled_ == down) return;
    upArrowEnabled_ = up;
    downArrowEnabled_ = down;
    repaintFlags_ |= kWinTertiaryMenuRepaint;
    InvalidateSelfAndParents();   // the original tail-jumps to cGZWin vslot 0x2e0
}

}  // namespace sc4
