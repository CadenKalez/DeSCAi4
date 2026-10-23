#include "sc4/WinSmallViews.h"

namespace sc4 {

// ------------------------------------------------------------------------------------------------ game delay

cSC4WinGameDelay::cSC4WinGameDelay(std::uint32_t delay, DelayType type, cIGZWinMgr* mgr)
    : cGZWin(mgr), type_(type), delay_(delay), counter_(static_cast<std::int32_t>(delay)) {}

bool cSC4WinGameDelay::Init() {
    cGZWin::Init();
    timer_.Start();
    // Quirk kept: Init already consumes one paint of the counter, and the delay ends here when it was 1.
    if (--counter_ == 0) mgr_->EndDialog(kWinGameDelayEndCode, this, 0);
    return true;
}

bool cSC4WinGameDelay::GZPaint() {
    if (type_ == kDelayMilliseconds) {
        if (timer_.GetElapsedTimeU32() < delay_) return true;
    } else if (--counter_ != 0) {
        return true;
    }
    mgr_->EndDialog(kWinGameDelayEndCode, this, 0);
    return true;
}

// ------------------------------------------------------------------------------------------ gen transparent

bool cSC4WinGenTransparent::IsPointInWindowWindowCoordinates(int x, int y) const {
    if ((x | y) < 0 || x >= area_.right - area_.left || y >= area_.bottom - area_.top) return false;
    int cx = x, cy = y;
    WindowToScreenCoordinates(cx, cy);   // vslot 0x1e0
    ChildList* list = LockChildListRead();
    bool hit = false;
    for (cIGZWin* child : list->items) {
        if (child->IsVisible() && !child->GetFlag(kWinIgnoreCursor) && child->IsPointInWindowScreenCoordinates(cx, cy)) {
            hit = true;
            break;
        }
    }
    UnlockChildListRead(list);
    return hit;
}

// ----------------------------------------------------------------------------------------------- region 3d

namespace {
IRegion3DRenderer* g_renderer = nullptr;
}
IRegion3DRenderer* WinRegion3DRendererService() { return g_renderer; }
void SetWinRegion3DRendererService(IRegion3DRenderer* renderer) { g_renderer = renderer; }

cSC4WinRegion3DView::~cSC4WinRegion3DView() {
    if (renderer_) renderer_->Release();
}

bool cSC4WinRegion3DView::Init() {
    IRegion3DRenderer* service = g_renderer;
    IRegion3DRenderer* old = renderer_;
    if (old != service) {
        if (service) service->AddRef();
        renderer_ = service;
        if (old) old->Release();
    }
    return cGZWin::Init();
}

bool cSC4WinRegion3DView::Shutdown() {
    if (IRegion3DRenderer* r = renderer_) {
        renderer_ = nullptr;
        r->Release();
    }
    return cGZWin::Shutdown();
}

bool cSC4WinRegion3DView::GZPaint() {
    auto* context = static_cast<gz::cIGZDrawContext*>(drawContext_);
    context->Suspend();                // vslot 0x150
    renderer_->DrawRegion3D();         // renderer vslot 0xa8
    context->Resume();                 // vslot 0x158
    return true;
}

}  // namespace sc4
