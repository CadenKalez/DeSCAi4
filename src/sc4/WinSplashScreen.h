// cSC4WinSplashScreen — the splash screen window (16 functions at 0x1002701d0..0x100270844 plus compiler aliases):
// the full-window script window (UI script 0x96a006b0 / 0x8aa9aa14, child id 0xaaa9c9d9) shown first at start-up. It
// paints once with the driver cleared to black, starts a 3000 ms one-shot timer on its first paint and, when the timer
// fires, a key is pressed or the mouse is clicked, posts the transition message 0x09dc338f to itself; handling that
// message destroys the splash and creates the region screen (cWinSC4App::DestroyCurrentScreen / CreateRegionScreen).
//
// Vtable groups: +0 the cGZWin / cIGZWin view, +0x150 the cIGZMessageTarget2 view (iid 0x452294aa and 1 answer it).
// Layout: +0x150 message-target vptr, +0x158 an unused reference (released by Shutdown and the destructor, never set
// here), +0x160 the graphic system (SL::spGS, +1), +0x168 its iid 0xc47b747c view, +0x170 the cRZCallbackTimer
// (0x38 bytes, its running flag is +0x198), +0x1a8 first-paint flag, +0x1ac paint counter.
//
// Quirks kept: GZOnMouseDownL always answers false (it stops the timer and transitions anyway); GZPaint paints twice
// (the first paint raises the counter to 1 and invalidates the window, the second drops it back to 0 and stops);
// GZOnKeyDown forwards the key to the parent and answers whether there was one.
//
// Seams: ISplashScreenHost holds what the original reaches through SL::spGS / SL::spMS2 / SL::spWinSC4App and
// SC4CreateWindowFromScript (see SPLASH-1 in notes/todo.csv).
#pragma once
#include <cstdint>

#include "gz/Win.h"
#include "gz/cGZPersistResourceKey.h"
#include "gz/cIGZMessage2.h"
#include "rz/cRZAutoRefCount.h"
#include "rz/cRZCallbackTimer.h"

namespace sc4 {

constexpr std::uint32_t kSplashScriptGroup = 0x96a006b0;
constexpr std::uint32_t kSplashScriptInstance = 0x8aa9aa14;     // key {0, 0x96a006b0, 0x8aa9aa14}
constexpr std::uint32_t kSplashScriptChildId = 0xaaa9c9d9;
constexpr std::uint32_t kSplashMsgTransition = 0x09dc338f;      // posted by the timer / key / click
constexpr std::uint32_t kSplashIIDMessageTarget2 = 0x452294aa;
constexpr std::uint32_t kSplashIIDWindow = 0xaa38326e;          // answered with the window itself
constexpr std::uint32_t kSplashIIDGraphicSystem2 = 0xc47b747c;
constexpr std::uint32_t kSplashDurationMs = 3000;
constexpr std::uint32_t kGDClearColorBuffer = 0x4000;           // Clear() mask

class ISplashScreenHost {
public:
    virtual ~ISplashScreenHost() = default;
    // SC4CreateWindowFromScript @ 0x100039a9f: one reference on the returned window (null on failure).
    virtual cIGZWin* CreateWindowFromScript(const cGZPersistResourceKey& key, cIGZWin* parent, std::uint32_t id) = 0;
    // SL::spGS (the caller AddRefs it).
    virtual cIGZUnknown* GraphicSystemObject() = 0;
    // SL::spMS2 (null before the message server exists) and `new cRZMessage2Standard` with one reference.
    virtual cIGZMessageServer2* MessageServer2() = 0;
    virtual cIGZMessage2Standard* CreateStandardMessage() = 0;
    // The driver of the iid 0xc47b747c view (vslot 0x18): cleared to black on every paint. Null when unavailable.
    virtual void ClearScreenToBlack() = 0;     // driver ClearColor(0,0,0,0) (vslot 0x60), ColorMask(true) (0x78), Clear(0x4000) (0x58)
    // cWinSC4App::DestroyCurrentScreen / CreateRegionScreen (SL::spWinSC4App).
    virtual void DestroyCurrentScreen() = 0;
    virtual void CreateRegionScreen() = 0;
};
ISplashScreenHost* SplashScreenHost();
void SetSplashScreenHost(ISplashScreenHost* host);

class cSC4WinSplashScreen : public cGZWin, public cIGZMessageTarget2 {
public:
    explicit cSC4WinSplashScreen(cIGZWinMgr* mgr);            // @ 0x1002701d0 (+0x1002702bc alias)
    ~cSC4WinSplashScreen() override;                          // @ 0x1002702c6 (+0x1002703ac / 0x1002703b6 aliases)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x100270762
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }   // @ 0x10027072a
    std::uint32_t Release() override { return cGZWin::Release(); } // @ 0x100270746

    bool Init() override;                                     // @ 0x1002704e0
    bool Shutdown() override;                                 // @ 0x100270680
    bool SetupScriptWindow();                                 // @ 0x1002703e4
    bool GZPaint() override;                                  // @ 0x100270844
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t flags) override;   // @ 0x100270816
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;     // @ 0x1002707f0
    bool DoMessage(cIGZMessage2* message) override;           // @ 0x1002707ac
    using cGZWin::DoMessage;

    // The timer function (@ 0x1002705e4): posts kSplashMsgTransition to `context`'s message-target view.
    static void TransitionTimerCallback(void* context);

    // harness
    bool TimerRunning() const { return timer_.IsRunning(); }
    cRZCallbackTimer& Timer() { return timer_; }
    bool FirstPaintDone() const { return firstPaint_; }
    int PaintCounter() const { return paintCounter_; }
    cIGZUnknown* GraphicSystem() const { return graphics_; }
    cIGZUnknown* GraphicSystem2() const { return graphics2_; }

private:
    cIGZUnknown* unused158_ = nullptr;                    // +0x158
    cRZAutoRefCount<cIGZUnknown> graphics_;               // +0x160
    cRZAutoRefCount<cIGZUnknown> graphics2_;              // +0x168
    cRZCallbackTimer timer_;                              // +0x170
    bool firstPaint_ = false;                             // +0x1a8
    int paintCounter_ = 0;                                // +0x1ac
};

}  // namespace sc4
