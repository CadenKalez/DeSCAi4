// cSC4WinResizableOutline (10 functions at 0x100371628..0x100371ef8): the thin rectangle (the lot / selection frame of the UI) that
// can be dragged by its body (moving) and by an 8 pixel border (resizing). Eight resize zones: 1 left, 2 right, 3 top, 4 bottom,
// 5 top-left, 6 top-right, 7 bottom-left, 8 bottom-right; the cursor follows the zone. While a drag is on it prints "W x H" at (8, 8).
// The constructor is not in the recovered set (created through the class factory); the defaults below are what Init's sentinel tests
// imply (the three colours start at -0xff = "not set", the font null, no zone, no cursor).
#pragma once
#include "gz/Win.h"

namespace sc4 {

// The font object the outline asks for (RZ::FO() vslot 0x48 creates it; 0x78 / 0x80 get / set its style, 0x198 draws).
class IOutlineFont : public cIGZUnknown {
public:
    virtual std::uint32_t GetStyle() = 0;                  // vslot 0x78
    virtual void SetStyle(std::uint32_t style) = 0;        // vslot 0x80
    virtual void DrawText(IWinBuffer* target, int x, int y, const char* text, std::uint32_t length, std::uint32_t color,
                          std::uint32_t color2, const cRZRect* clip, int flags) = 0;   // vslot 0x198
};

class IResizableOutlineHost {
public:
    virtual ~IResizableOutlineHost() = default;
    // RZ::FO() vslot 0x48 (name, 0, 14, 0, 1): the caller owns one reference. Init asks for "Arial MT".
    virtual IOutlineFont* CreateFont(const char* face, int a, int size, int b, int c) = 0;
    // Buffer vslot 0xf0 (x0, y0, x1, y1, color, thickness).
    virtual void DrawLine(IWinBuffer* target, int x0, int y0, int x1, int y1, std::uint32_t color, std::uint32_t thickness) = 0;
};
IResizableOutlineHost* ResizableOutlineHost();
void SetResizableOutlineHost(IResizableOutlineHost* host);

class cSC4WinResizableOutline : public cGZWin {
public:
    enum ResizeType : std::int32_t { kNone = 0, kLeft = 1, kRight = 2, kTop = 3, kBottom = 4, kTopLeft = 5, kTopRight = 6, kBottomLeft = 7, kBottomRight = 8 };
    explicit cSC4WinResizableOutline(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    ~cSC4WinResizableOutline() override;                 // @ 0x100371628 (+ 0x10037163c)

    bool Init() override;                                // @ 0x100371674
    bool Shutdown() override;                            // @ 0x100371832
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;   // @ 0x100371866
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;     // @ 0x100371a22
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;    // @ 0x100371a62
    bool GZPaint() override;                             // @ 0x100371c98

    // The zone under the point and the cursor id for it; true when that action is currently allowed.     @ 0x1003718d0
    bool GetResizeTypeAndCursorForPosition(int x, int y, ResizeType& type, std::uint32_t& cursorId);
    void SetResizingAndMoving(bool resizing, bool moving);   // @ 0x100371ef8

    // colours are native; the sentinel -0xff means "pick the default in Init"
    void SetColors(std::int32_t normal, std::int32_t active, std::int32_t idle) { normalColor_ = normal; activeColor_ = active; idleColor_ = idle; }
    bool Dragging() const { return dragging_; }
    bool Resizing() const { return resizing_; }
    bool Moving() const { return moving_; }
    ResizeType ActiveZone() const { return zone_; }
    std::uint32_t CurrentCursorId() const { return cursorId_; }

private:
    bool resizing_ = false;      // +0x150
    bool moving_ = false;        // +0x151
    bool dragging_ = false;      // +0x152
    ResizeType zone_ = kNone;    // +0x154
    std::int32_t normalColor_ = -0xff;   // +0x158: drawn while resizing / moving is allowed
    std::int32_t activeColor_ = -0xff;   // +0x15c: drawn during a drag
    std::int32_t idleColor_ = -0xff;     // +0x160: drawn when neither is allowed
    std::uint32_t cursorId_ = 0;         // +0x164
    int lastX_ = 0, lastY_ = 0;          // +0x168 / +0x16c
    IOutlineFont* font_ = nullptr;       // +0x170
};

}  // namespace sc4
