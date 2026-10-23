// cSC4WinTertiaryMenuFrame — the frame of a tertiary (tool palette) menu: a thin vertical bar on the right of the
// palette, composed from one background bitmap (a body tile, a tiled right edge and the two arrow corner tiles),
// with an optional scroll-up / scroll-down arrow pair in the right margin column.  The bar reports a content
// rectangle to its owner and forwards arrow clicks as 0xca35e4ba ("scroll by -1 / +1 rows") messages to its
// notification target, repeating while held down.
//
// 23 recovered symbols at 0x1002fe310..0x1002ff102 — the 20 hand-written functions plus the C1 constructor and the
// D1 / D0 destructor aliases (the 14 compiler-emitted this-adjusting thunks are not written by hand); object size
// 0x1e8 (the allocation at 0x10078d411 asks operator new for 0x1e8).
//
// The object is the interface cISC4WinTertiaryMenuFrame (iid 0x8a25ce2c) at +0 followed by the cGZWin subobject at
// +8.  The primary vtable (0x100a95360) is the interface's own: [0] QueryInterface, [1] AddRef, [2] Release,
// [3] 0x18 AsIGZWin, [4] 0x20 SetBackgroundImage, [5] 0x28 PositionFrame, [6] 0x30 GetContentRect,
// [7] 0x38 EnableScrollArrow, [8]/[9] 0x40/0x48 destructors, [10] 0x50 Init, [11] 0x58 Shutdown, [12] 0x60 GZPaint,
// [13] 0x68 IsPointInWindowWindowCoordinates, [14] 0x70 IsPointInWindowScreenCoordinates, [15] 0x78 GZOnMouseMove,
// [16] 0x80 GZOnMouseExit, [17] 0x88 GZOnMouseDownL, [18] 0x90 GZOnMouseUpL.  The cGZWin half answers the same
// window methods through non-virtual thunks, and QueryInterface(0x8a25ce2c) hands out the interface at +0.
//
// The iid is the "parent scroll buttons" interface cSC4WinCatalogView queries (see WinCatalogView.h's
// ICatalogScrollTarget, a partial view of the same vtable: its EnableScrollArrow is vslot 0x38).
//
// Layout (the cGZWin part is 0x150 bytes, so the fields start at +0x158):
//   +0x158 background bitmap (cIGZBuffer, one reference)        +0x160 the composed scratch buffer (cIGZBuffer)
//   +0x168 margin x                    +0x16c margin y          +0x170 background height - 2 * margin y
//   +0x174 body tile width (= where the arrow column starts in the background image)
//   +0x178 body tile height            +0x17c right column width (= window width - body width - margin x)
//   +0x180 horizontal anchor offset    +0x184 vertical anchor offset
//   +0x188 content inset (distance from the window top to the body tile)
//   +0x18c/+0x190/+0x194/+0x198 content rectangle (left/top/right/bottom: what GetContentRect reports)
//   +0x19c repaint flags byte (bit 0 = the scratch buffer must be composed again)
//   +0x1a0 up arrow enabled (bool)     +0x1a1 down arrow enabled (bool)
//   +0x1a4 hovered side (0 none, 1 up, 2 down)   +0x1a8 held side (0 none, 1 up, 2 down)
//   +0x1b0 the auto-repeat timer (cRZCallbackTimer, 0x38 bytes)
//
// Quirks kept from the original:
//  * IsPointInWindowWindowCoordinates does not answer "is the point inside the window": after the inside test it
//    returns "is x at or right of window width - margin x", i.e. "is the point in the arrow column" (0x1002fec82).
//  * IsPointInWindowScreenCoordinates converts with cGZWin::ScreenToWindowCoordinates and then calls the
//    interface's own IsPointInWindowWindowCoordinates through the primary vtable (vslot 0x68).
//  * SetBackgroundImage dereferences the image without a null test (0x1002feafe) and, when the image replaces an
//    earlier one, re-reads the member before measuring it; an unchanged pointer skips the AddRef but still
//    recomputes the margins (0x1002fea92).
//  * Shutdown releases the background (0x158) before the scratch buffer (0x160); the destructor releases them in
//    the opposite order after Shutdown returned (0x1002fe3e0).
//  * GZPaint clears the whole 32-bit word at +0x19c (flag byte and padding) and, when the scratch buffer already
//    has the right size, composes it even if the graphics system is gone (0x1002fe668).
//  * The up/down arrow tiles only widen when the arrow is hovered; while it is *held* the extra hover strip
//    disappears again (0x1002fe812..0x1002fe868).
//
// The scratch buffer and the window's draw buffer are cIGZBuffers in the original: the window creates the former
// with RZ::GS() vslot 0x20 and the latter is cGZWin +0x98, which this port keeps as IWinBuffer.  The seam below is
// the same shape cGZGraph and cSC4WinCalloutBox use.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "gz/Win.h"
#include "rz/cRZCallbackTimer.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4WinTertiaryMenuFrame = 0x8a25ce2c;   // the parent scroll-buttons interface
constexpr std::uint32_t kWinTertiaryMenuScrollMsg = 0xca35e4ba;          // param1 = -1 (up) / +1 (down) rows
constexpr std::uint32_t kWinTertiaryMenuInitialScrollPeriodMS = 300;     // _kInitialScrollRepeatPeriodMS 0x100b06490
constexpr std::uint32_t kWinTertiaryMenuScrollRepeatPeriodMS = 100;      // _kScrollRepeatPeriodMS 0x100b06494

// The numeric side values at +0x1a4 / +0x1a8 (and the arrow index the tile maths compares against).
enum WinTertiaryMenuSide : int {
    kWinTertiaryMenuSideNone = 0,
    kWinTertiaryMenuSideUp = 1,
    kWinTertiaryMenuSideDown = 2,
};

// +0x19c / +0x1a0 / +0x1a1.
enum WinTertiaryMenuFlags : std::uint8_t {
    kWinTertiaryMenuRepaint = 1,   // compose the scratch buffer again before the next blit
};

// RZ::GS() vslot 0x20 (a fresh display buffer) and cGZWin's draw buffer as the full buffer interface.
class IWinTertiaryMenuFrameHost {
public:
    virtual ~IWinTertiaryMenuFrameHost() = default;
    virtual cIGZBuffer* CreateBuffer() = 0;                     // one reference, or null when it cannot be made
    virtual cIGZBuffer* WindowBuffer(IWinBuffer* buffer) = 0;   // the cGZWin draw buffer at +0x98 as a cIGZBuffer
};
IWinTertiaryMenuFrameHost* WinTertiaryMenuFrameHost();
void SetWinTertiaryMenuFrameHost(IWinTertiaryMenuFrameHost* host);

// cISC4WinTertiaryMenuFrame (iid 0x8a25ce2c): the frame's interface, in the original's vtable order.  The three
// unknown methods are declared here rather than inherited from cIGZUnknown so that the virtual destructor lands in
// slots 8/9 exactly as in the original; the class's single overrides answer both this interface and cGZWin.
class cISC4WinTertiaryMenuFrame {
public:
    virtual bool QueryInterface(std::uint32_t iid, void** out) = 0;      // [0] 0x00
    virtual std::uint32_t AddRef() = 0;                                   // [1] 0x08
    virtual std::uint32_t Release() = 0;                                  // [2] 0x10
    virtual cIGZWin* AsIGZWin() = 0;                                      // [3] 0x18 (the cGZWin at +8)
    // [4] 0x20 @ 0x1002fea92: the background bitmap and its nine-slice metrics.
    virtual void SetBackgroundImage(cIGZBuffer* image, int marginX, int marginY, int bodyTileWidth,
                                    int bodyTileHeight, int rightColumnWidth, int anchorOffsetX,
                                    int anchorOffsetY) = 0;
    // [5] 0x28 @ 0x1002feb52: size the frame around a content area of contentWidth x contentHeight attached to
    // the point (anchorX, anchorY), keeping the frame between minTop and maxBottom (both in parent coordinates).
    virtual void PositionFrame(int contentWidth, int contentHeight, int anchorX, int anchorY, int minTop,
                               int maxBottom) = 0;
    virtual void GetContentRect(cRZRect& rect) = 0;                       // [6] 0x30
    virtual void EnableScrollArrow(bool up, bool down) = 0;               // [7] 0x38
    virtual ~cISC4WinTertiaryMenuFrame() = default;                       // [8]/[9] 0x40/0x48
    virtual bool Init() = 0;                                              // [10] 0x50
    virtual bool Shutdown() = 0;                                          // [11] 0x58
    virtual bool GZPaint() = 0;                                           // [12] 0x60
    virtual bool IsPointInWindowWindowCoordinates(int x, int y) const = 0;   // [13] 0x68
    virtual bool IsPointInWindowScreenCoordinates(int x, int y) const = 0;   // [14] 0x70
    virtual bool GZOnMouseMove(int x, int y, std::uint32_t flags) = 0;       // [15] 0x78
    virtual bool GZOnMouseExit(cIGZWin* to) = 0;                             // [16] 0x80
    virtual bool GZOnMouseDownL(int x, int y, std::uint32_t flags) = 0;      // [17] 0x88
    virtual bool GZOnMouseUpL(int x, int y, std::uint32_t flags) = 0;        // [18] 0x90
};

class cSC4WinTertiaryMenuFrame : public cISC4WinTertiaryMenuFrame, public cGZWin {
public:
    // The original's constructor takes no argument: cGZWin's constructor asks RZ::WM() for the manager itself.
    explicit cSC4WinTertiaryMenuFrame(cIGZWinMgr* mgr);       // @ 0x1002fe310 (+ C1 alias 0x1002fe3d6)
    ~cSC4WinTertiaryMenuFrame() override;                     // @ 0x1002fe3e0 (+ 0x1002fe4b2, deleting 0x1002fe4ca)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1002fe536 (+ thunk 0x1002fe558)
    std::uint32_t AddRef() override;                               // @ 0x1002fe506 (+ thunk 0x1002fe514)
    std::uint32_t Release() override;                              // @ 0x1002fe51e (+ thunk 0x1002fe52c)
    cIGZWin* AsIGZWin() override;                                  // @ 0x1002ff09c

    bool Init() override;                                      // @ 0x1002fe57e (+ thunk 0x1002fe5d2)
    bool Shutdown() override;                                  // @ 0x1002fe5e4 (+ thunk 0x1002fe65a)
    bool GZPaint() override;                                   // @ 0x1002fe668 (+ thunk 0x1002fea80)

    void SetBackgroundImage(cIGZBuffer* image, int marginX, int marginY, int bodyTileWidth, int bodyTileHeight,
                            int rightColumnWidth, int anchorOffsetX, int anchorOffsetY) override;   // @ 0x1002fea92
    void PositionFrame(int contentWidth, int contentHeight, int anchorX, int anchorY, int minTop,
                       int maxBottom) override;   // @ 0x1002feb52
    void GetContentRect(cRZRect& rect) override;               // @ 0x1002ff0a6
    void EnableScrollArrow(bool up, bool down) override;       // @ 0x1002ff0c2

    bool IsPointInWindowWindowCoordinates(int x, int y) const override;   // @ 0x1002fec82 (+ thunk 0x1002fecbe)
    bool IsPointInWindowScreenCoordinates(int x, int y) const override;   // @ 0x1002fecfa (+ thunk 0x1002fed44)

    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;       // @ 0x1002fed94 (+ thunk 0x1002fee1c)
    bool GZOnMouseExit(cIGZWin* to) override;                             // @ 0x1002fee2e (+ thunk 0x1002fee5e)
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;      // @ 0x1002fee92 (+ thunk 0x1002fefb2)
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;        // @ 0x1002fefc4 (+ thunk 0x1002ff018)

private:
    // The auto-repeat timer callback: a private static member in the original as well (the timer takes its address
    // with `this` as the user pointer).
    static void ScrollContentCallback(void* context);          // @ 0x1002fef52

    // The 0x1002fe68c block of GZPaint: blit the composed buffer into the window's draw buffer.
    bool BlitToDrawBuffer();

    cIGZBuffer* backgroundImage_ = nullptr;    // +0x158 (one reference)
    cIGZBuffer* scratchBuffer_ = nullptr;      // +0x160 (one reference)
    int marginX_ = 0;                          // +0x168
    int marginY_ = 0;                          // +0x16c
    int innerHeight_ = 0;                      // +0x170 background height - 2 * margin y
    int bodyTileWidth_ = 0;                    // +0x174
    int bodyTileHeight_ = 0;                   // +0x178
    int rightColumnWidth_ = 0;                 // +0x17c
    int anchorOffsetX_ = 0;                    // +0x180
    int anchorOffsetY_ = 0;                    // +0x184
    int contentInset_ = 0;                     // +0x188
    int contentLeft_ = 0;                      // +0x18c
    int contentTop_ = 0;                       // +0x190
    int contentRight_ = 0;                     // +0x194
    int contentBottom_ = 0;                    // +0x198
    std::uint8_t repaintFlags_ = 0;            // +0x19c
    bool upArrowEnabled_ = false;              // +0x1a0
    bool downArrowEnabled_ = false;            // +0x1a1
    int hoverSide_ = kWinTertiaryMenuSideNone;  // +0x1a4
    int heldSide_ = kWinTertiaryMenuSideNone;   // +0x1a8
    cRZCallbackTimer timer_;                   // +0x1b0
};

}  // namespace sc4
