// cSC4WinCalloutBox — the in-world callout box: the tool-tip style window cSC4ViewInputControlQuery and cSC4MySim put
// over an occupant (icon, title, text body, optional meter), drawn on a 5x5 skin taken from one background bitmap and
// pinned to a point of interest by a small beak. 29 functions at 0x1003c5e30..0x1003c8210 (plus the compiler-emitted
// non-virtual thunks), object size 0x278.
//
// The object is a cISC4WinCalloutBox interface (iid 0xc9b432cf) at +0 and a cGZWin subobject at +8; the window half
// answers QueryInterface(0xc9b432cf) itself and forwards everything else to cGZWin, so AddRef/Release/QueryInterface
// all end up counting the window's reference (the original's thunks subtract 8 to get back to the interface).
//
// Layout (the cGZWin part is 0x158 bytes, so the fields start at +0x158):
//   +0x158 constraint rect (the rectangle the box is kept inside)  +0x168 point of interest
//   +0x170 title (cRZString)                                        +0x198 text (cRZString)
//   +0x1c0 avoidance rect (default -28,-28,28,28: the gap kept around the point)
//   +0x1d0 layout flags (1 = measure, 2 = place, 4 = draw)          +0x1d8 background image
//   +0x1e0 icon                                                     +0x1e8 title font
//   +0x1f0 text font                                                +0x1f8 meter image
//   +0x200 private buffer holding the title + text block            +0x208 private buffer holding the finished box
//   +0x210 background margin x (8 when not given)                   +0x214 background margin y (3 when not given)
//   +0x218 measured text width   +0x21c content height + 2 (the text buffer's height)
//   +0x220 box width             +0x224 box height
//   +0x228 placement side (0 = box left of the point, 1 = above, 2 = right, 3 = below; 4 = not placed yet)
//   +0x22c dirty rectangle (box, grown by 1.5 skin cells on the beak side)
//   +0x23c box rectangle         +0x24c beak rectangle (both in dirty-rectangle coordinates when drawn)
//   +0x25c meter rectangle       +0x26c meter cell count   +0x270 title block top   +0x274 meter value [0, 1]
//
// The fonts are gz::IWinFont in this port (the house seam over cIGZFont); GZPaint reaches the full cIGZFont interface
// through IWinFont::AsFont() because the original calls CalculateTextArea / DrawTextParagraph / LockSurfaceForFontUse
// on the object WCM()'s SystemFont (WCM vslot 0x28) returns.
//
// The two private buffers are made by IWinCalloutBoxServices::CreateBuffer (RZ::GS() vslot 0x20), a seam the app has
// to hook up to its graphics system; without it (or without fonts) GZPaint still lays the box out and skips the
// drawing, which is the follow-up recorded in the pending ledger entry. Every geometry, buffer and font call is the
// original's.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "gz/WinFont.h"
#include "sc4/ISC4WinCalloutBox.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4WinCalloutBox = 0xc9b432cf;

// RZ::GS() vslot 0x20 — the graphic system's buffer factory, as this window needs it (a counted cIGZBuffer). The
// recovered graphics-system seam (gz/Win.h IWinGraphics::GraphicsCreateBuffer) speaks IWinBuffer, so the callout has
// its own; the app supplies the graphics system's implementation, tests a recording one.
class IWinCalloutBoxServices {
public:
    virtual ~IWinCalloutBoxServices() = default;
    virtual cIGZBuffer* CreateBuffer() = 0;      // one reference, or null when the graphics system cannot make one
};
IWinCalloutBoxServices* CalloutBoxServices();
void SetCalloutBoxServices(IWinCalloutBoxServices* services);

// The layout flags at +0x1d0.
enum WinCalloutBoxFlags : std::uint32_t {
    kCalloutFlagMeasure = 1,   // the title/text must be measured again
    kCalloutFlagPlace = 2,     // the box must be placed again
    kCalloutFlagDraw = 4,      // the buffers must be composed again
};

// The placement side at +0x228: which side of the point of interest the box sits on, i.e. where the beak points from.
enum WinCalloutBoxSide : std::uint8_t {
    kCalloutSideBoxLeft = 0,    // the box is left of the point, the beak points right
    kCalloutSideBoxAbove = 1,   // the box is above the point, the beak points down
    kCalloutSideBoxRight = 2,   // the box is right of the point, the beak points left
    kCalloutSideBoxBelow = 3,   // the box is below the point, the beak points up
    kCalloutSideUnplaced = 4,   // the constructor's value: never drawn yet
};

class cSC4WinCalloutBox : public cISC4WinCalloutBox, public cGZWin {
public:
    // The original takes no argument: cGZWin's constructor asks RZ::WM() for the window manager itself (0x1001689d2).
    explicit cSC4WinCalloutBox(cIGZWinMgr* mgr);              // @ 0x1003c5e30 (+ 0x1003c5f5c)
    ~cSC4WinCalloutBox() override;                            // @ 0x1003c5f66 (+ deleting 0x1003c615a, thunks)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1003c61c6 (+ thunk 0x1003c61e8)
    std::uint32_t AddRef() override;                          // @ 0x1003c6196 (+ thunk 0x1003c61a4)
    std::uint32_t Release() override;                         // @ 0x1003c61ae (+ thunk 0x1003c61bc)

    cIGZWin* AsIGZWin() override;                             // @ 0x1003c620e: the cGZWin subobject at +8

    bool Init() override;                                     // @ 0x1003c6218 (+ thunk 0x1003c6384)
    bool Shutdown() override;                                 // @ 0x1003c6396 (+ thunk 0x1003c6476)
    bool GZPaint() override;                                  // @ 0x1003c6a3c (+ thunk 0x1003c8210)

    void SetBackgroundImage(cIGZBuffer* image, int marginX, int marginY) override;   // @ 0x1003c6484
    cIGZBuffer* GetBackgroundImage() const override { return backgroundImage_; }     // @ 0x1003c655e
    void SetTitle(const cIGZString& title) override;          // @ 0x1003c656c
    const cIGZString& GetTitle() const override { return title_; }              // @ 0x1003c65cc
    void SetText(const cIGZString& text) override;            // @ 0x1003c65da
    const cIGZString& GetText() const override { return text_; }                // @ 0x1003c663a
    void SetTitleFontStyle(std::uint32_t style) override;     // @ 0x1003c6648
    void SetTextFontStyle(std::uint32_t style) override;      // @ 0x1003c66be
    void SetIcon(cIGZBuffer* icon) override;                  // @ 0x1003c6734
    cIGZBuffer* GetIcon() const override { return icon_; }    // @ 0x1003c67e2
    void SetIconMeter(cIGZBuffer* image, std::uint32_t count, float value) override;   // @ 0x1003c67f0
    void SetPointOfInterest(const cRZPoint& point) override;  // @ 0x1003c691e
    const cRZPoint& GetPointOfInterest() const override { return pointOfInterest_; }   // @ 0x1003c695a
    void SetAvoidanceRect(const cRZRect& rect) override;      // @ 0x1003c6968
    const cRZRect& GetAvoidanceRect() const override { return avoidanceRect_; }        // @ 0x1003c69c4
    void SetConstraintRect(const cRZRect& rect) override;     // @ 0x1003c69d2
    const cRZRect& GetConstraintRect() const override { return constraintRect_; }      // @ 0x1003c6a2e

private:
    cRZRect constraintRect_;                  // +0x158
    cRZPoint pointOfInterest_;                // +0x168
    cRZString title_;                         // +0x170
    cRZString text_;                          // +0x198
    cRZRect avoidanceRect_{-28, -28, 28, 28}; // +0x1c0
    std::uint32_t dirtyFlags_ = 0;            // +0x1d0
    cIGZBuffer* backgroundImage_ = nullptr;   // +0x1d8
    cIGZBuffer* icon_ = nullptr;              // +0x1e0
    gz::IWinFont* titleFont_ = nullptr;       // +0x1e8
    gz::IWinFont* textFont_ = nullptr;        // +0x1f0
    cIGZBuffer* meterImage_ = nullptr;        // +0x1f8
    cIGZBuffer* textBuffer_ = nullptr;        // +0x200
    cIGZBuffer* skinBuffer_ = nullptr;        // +0x208
    // The original leaves both margins uninitialised until SetBackgroundImage; 8 / 3 are the values it defaults to
    // there for a negative argument, which is what every caller passes.
    int backgroundMarginX_ = 8;               // +0x210
    int backgroundMarginY_ = 3;               // +0x214
    int contentWidth_ = 0;                    // +0x218
    int contentHeight_ = 0;                   // +0x21c
    int boxWidth_ = 0;                        // +0x220
    int boxHeight_ = 0;                       // +0x224
    std::uint8_t side_ = kCalloutSideUnplaced;   // +0x228
    cRZRect dirtyRect_;                       // +0x22c
    cRZRect boxRect_;                         // +0x23c
    cRZRect beakRect_;                        // +0x24c
    cRZRect meterRect_;                       // +0x25c
    std::uint32_t meterCount_ = 0;            // +0x26c
    int titleBlockTop_ = 0;                   // +0x270
    float meterValue_ = 0.0f;                 // +0x274
};

}  // namespace sc4
