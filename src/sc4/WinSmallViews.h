// Three tiny windows of the city UI:
//  * cSC4WinGameDelay (0x10003aa12..0x10003ab60): an invisible window that ends itself after N paints or N milliseconds. It tells
//    the window manager "dialog finished" (EndDialog, code 0xf) through the manager's slot 9 (+0x48).
//  * cSC4WinGenTransparent (0x1004640a6..0x10046413c): a cGZWinGen that only counts as hit where one of its visible children is.
//  * cSC4WinRegion3DView (0x10023d4d2..0x10023d5d8): paints the 3D region through the renderer service.
#pragma once
#include "gz/DrawContext.h"
#include "gz/Win.h"
#include "gz/WinGen.h"
#include "rz/cRZTimer.h"

namespace sc4 {

constexpr std::uint32_t kWinGameDelayEndCode = 0xf;   // EndDialog code sent to the window manager

class cSC4WinGameDelay : public cGZWin {
public:
    enum DelayType : std::uint32_t { kDelayMilliseconds = 0, kDelayPaints = 1 };   // +0x150 (0 = timer, non-zero = paint counter)
    cSC4WinGameDelay(std::uint32_t delay, DelayType type, cIGZWinMgr* mgr);   // @ 0x10003aa12
    ~cSC4WinGameDelay() override = default;                                    // @ 0x10003ab18 / 0x10003ab60

    bool Init() override;       // @ 0x10003aa80
    bool GZPaint() override;    // @ 0x10003aac0

private:
    DelayType type_;                      // +0x150
    cRZTimer timer_{cRZTimer::kMilliseconds};   // +0x158
    std::uint32_t delay_;                 // +0x170
    std::int32_t counter_;                // +0x174: paints left
};

class cSC4WinGenTransparent : public gz::cGZWinGen {
public:
    explicit cSC4WinGenTransparent(cIGZWinMgr* mgr) : gz::cGZWinGen(mgr) {}   // @ 0x1004640a6
    ~cSC4WinGenTransparent() override = default;                           // @ 0x1004640d4 / 0x1004640f6
    bool IsPointInWindowWindowCoordinates(int x, int y) const override;    // @ 0x10046413c
};

// The renderer service (SL::spRenderer): slot 0xa8 draws the region.
class IRegion3DRenderer : public cIGZUnknown {
public:
    virtual void DrawRegion3D() = 0;   // vslot 0xa8
};
// Stands for the global SL::spRenderer read by cSC4WinRegion3DView::Init.
IRegion3DRenderer* WinRegion3DRendererService();
void SetWinRegion3DRendererService(IRegion3DRenderer* renderer);

class cSC4WinRegion3DView : public cGZWin {
public:
    explicit cSC4WinRegion3DView(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    ~cSC4WinRegion3DView() override;       // @ 0x10023d5d8 (+ 0x10023d5a0 / 0x10023d5aa)

    bool Init() override;       // @ 0x10023d4d2
    bool Shutdown() override;   // @ 0x10023d52a
    bool GZPaint() override;    // @ 0x10023d55e

private:
    IRegion3DRenderer* renderer_ = nullptr;   // +0x150
};

}  // namespace sc4
