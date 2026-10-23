#include "sc4/WinSplashScreen.h"

namespace sc4 {

namespace {
ISplashScreenHost* g_host = nullptr;
}
ISplashScreenHost* SplashScreenHost() { return g_host; }
void SetSplashScreenHost(ISplashScreenHost* host) { g_host = host; }

cSC4WinSplashScreen::cSC4WinSplashScreen(cIGZWinMgr* mgr) : cGZWin(mgr) {   // @ 0x1002701d0
    SetFlag(kWinPrivateBuffer, false);   // vslot 0x220 (0x10000, 0)
}

cSC4WinSplashScreen::~cSC4WinSplashScreen() {   // @ 0x1002702c6
    Shutdown();
    // the timer, +0x168, +0x160 and +0x158 are released by their members / already null after Shutdown
}

bool cSC4WinSplashScreen::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x100270762
    if (iid == kSplashIIDMessageTarget2 || iid == 1) {
        *out = static_cast<cIGZMessageTarget2*>(this);   // +0x150
    } else if (iid == kSplashIIDWindow) {
        *out = static_cast<cIGZWin*>(this);
    } else {
        return cGZWin::QueryInterface(iid, out);
    }
    AddRef();
    return true;
}

bool cSC4WinSplashScreen::SetupScriptWindow() {   // @ 0x1002703e4
    ISplashScreenHost* host = SplashScreenHost();
    if (!host) return false;
    const cGZPersistResourceKey key{0, kSplashScriptGroup, kSplashScriptInstance};
    cIGZWin* child = host->CreateWindowFromScript(key, this, kSplashScriptChildId);
    if (!child) return false;
    // centre the script window; a window wider than the screen sits at 0 (the test is on the difference, not the half)
    const int width = GetW(), childWidth = child->GetW();
    const int height = GetH(), childHeight = child->GetH();
    int x = (width - childWidth) / 2;
    if (width - childWidth < -1) x = 0;
    int y = (height - childHeight) / 2;
    if (height - childHeight < -1) y = 0;
    child->GZWinMoveTo(x, y);        // vslot 0x1c0
    child->PullToFront();            // vslot 0xb8
    child->SetFlag(kWinFlag800, false);   // vslot 0x220
    child->Release();                // vslot 0x10
    return true;
}

bool cSC4WinSplashScreen::Init() {   // @ 0x1002704e0
    if (get_init_flag()) return true;
    cGZWin::Init();
    cIGZWin* main = GetMainWindow();   // vslot 0x50
    SetArea(0, 0, main->GetW(), main->GetH());   // vslot 0x1b0
    SetupScriptWindow();
    ISplashScreenHost* host = SplashScreenHost();
    graphics_ = host ? host->GraphicSystemObject() : nullptr;   // +1 reference, the old one is released
    graphics2_ = nullptr;
    if (graphics_) {
        void* view = nullptr;
        if (graphics_->QueryInterface(kSplashIIDGraphicSystem2, &view)) graphics2_ = static_cast<cIGZUnknown*>(view);   // the query's own reference
        if (view) static_cast<cIGZUnknown*>(view)->Release();   // graphics2_ took its own
    }
    timer_.SetCallbackFunction(&cSC4WinSplashScreen::TransitionTimerCallback, this);
    return true;
}

bool cSC4WinSplashScreen::Shutdown() {   // @ 0x100270680
    if (get_init_flag()) {
        set_init_flag(false);
        if (timer_.IsRunning()) timer_.Stop();
        ChildDeleteAll();   // vslot 0x90
        unused158_ = nullptr;
        graphics_ = nullptr;
        graphics2_ = nullptr;
        cGZWin::Shutdown();
    }
    return true;
}

void cSC4WinSplashScreen::TransitionTimerCallback(void* context) {   // @ 0x1002705e4
    ISplashScreenHost* host = SplashScreenHost();
    cIGZMessageServer2* server = host ? host->MessageServer2() : nullptr;
    if (!server) return;
    cIGZMessage2Standard* message = host->CreateStandardMessage();
    if (!message) return;
    message->SetType(kSplashMsgTransition);   // vslot 0x28
    auto* screen = static_cast<cSC4WinSplashScreen*>(context);
    server->GeneralMessagePostToTarget(message, screen ? static_cast<cIGZMessageTarget2*>(screen) : nullptr);   // vslot 0x38
    message->Release();
}

bool cSC4WinSplashScreen::DoMessage(cIGZMessage2* message) {   // @ 0x1002707ac
    const std::uint32_t type = message->GetType();   // vslot 0x20
    if (type != kSplashMsgTransition) return false;
    if (ISplashScreenHost* host = SplashScreenHost()) {
        host->DestroyCurrentScreen();
        host->CreateRegionScreen();
    }
    return true;
}

bool cSC4WinSplashScreen::GZOnMouseDownL(int, int, std::uint32_t) {   // @ 0x1002707f0
    timer_.Stop();
    TransitionTimerCallback(this);
    return false;
}

bool cSC4WinSplashScreen::GZOnKeyDown(std::uint32_t key, std::uint32_t flags) {   // @ 0x100270816
    if (!parent_) return false;
    SendMsg(parent_, kGZMsgKeyDown, key, flags, 0);   // vslot 0x488
    return true;
}

bool cSC4WinSplashScreen::GZPaint() {   // @ 0x100270844
    if (!firstPaint_) {
        firstPaint_ = true;
        timer_.StartOneShot(kSplashDurationMs, static_cast<cIGZMessageTarget2*>(this));   // held while armed
    }
    if (ISplashScreenHost* host = SplashScreenHost()) host->ClearScreenToBlack();
    if (paintCounter_ == 0) {
        paintCounter_ = 1;
    } else {
        if (--paintCounter_ == 0) return true;
    }
    InvalidateSelf();   // vslot 0x2d8
    return true;
}

}  // namespace sc4
