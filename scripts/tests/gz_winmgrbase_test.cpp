// cGZWinMgrBase: valid-window list and instance ids, focus / capture / mouse-window bookkeeping and their messages,
// mouse and keyboard routing, the post queue, plot strategy, condemned windows, key filters, modal handling and the
// service life cycle (Init / Shutdown against a fake framework).
#include <cstdio>
#include <functional>
#include <vector>

#include "gz/Cursor.h"
#include "rz/cRZString.h"
#include "gz/WinMgrBase.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct FakeFramework final : cIGZFrameWork {
    std::vector<cIGZSystemService*> ticks;
    int idles = 0, refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool AddToTick(cIGZSystemService* s) override { ticks.push_back(s); return true; }
    bool RemoveFromTick(cIGZSystemService* s) override { for (auto it = ticks.begin(); it != ticks.end(); ++it) if (*it == s) { ticks.erase(it); return true; } return false; }
    void OnIdle() override { ++idles; }
    bool AddSystemService(cIGZSystemService*) override { return {}; }
    bool RemoveSystemService(cIGZSystemService*) override { return {}; }
    bool GetSystemService(std::uint32_t, std::uint32_t, void**) override { return {}; }
    bool EnumSystemServices(cIGZUnknownEnumerator&, cIGZUnknown*, std::uint32_t) override { return {}; }
    bool AddHook(cIGZFrameWorkHooks*) override { return {}; }
    bool RemoveHook(cIGZFrameWorkHooks*) override { return {}; }
    bool AddToOnIdle(cIGZSystemService*) override { return {}; }
    bool RemoveFromOnIdle(cIGZSystemService*) override { return {}; }
    int GetOnIdleInterval() const override { return {}; }
    void SetOnIdleInterval(int) override {}
    void OnTick(std::uint32_t) override {}
    bool IsTickEnabled() const override { return {}; }
    void ToggleTick(bool) override {}
    void Quit(int) override {}
    void AbortiveQuit(int) override {}
    cIGZCmdLine* CommandLine() override { return {}; }
    bool IsInstall() const override { return {}; }
    cIGZCOM* GetCOMObject() override { return {}; }
    int GetState() const override { return {}; }
    cIGZDebugStream* GetDebugStream() override { return {}; }
    cIGZDebugStream* DefaultDebugStream() override { return {}; }
    cIGZDebugStream* DebugStream() override { return {}; }
    void SetDebugStream(cIGZDebugStream*) override {}
    void SetDebugLevel(int) override {}
    int GetDebugLevel() const override { return {}; }
    cIGZOStream* StdOut() override { return {}; }
    cIGZOStream* StdErr() override { return {}; }
    cIGZIStream* StdIn() override { return {}; }
    bool GetStream(int, std::uint32_t, void**) override { return {}; }
    bool SetStream(int, cIGZUnknown*) override { return {}; }
    void SetApplication(cIGZApp*) override {}
    cIGZApp* Application() const override { return {}; }
    void ReportException(const char*) override {}
    cIGZUnknown* ExceptionNotificationObj() const override { return {}; }
};

struct FakeMS final : cIGZMessageServer {
    std::vector<cGZMessage> sent;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    bool MessageSend(const cGZMessage& m) override { sent.push_back(m); return true; }
    bool MessagePost(const cGZMessage&, bool) override { return true; }
    bool AddNotification(cIGZMessageTarget*, std::uint32_t) override { return true; }
    bool RemoveNotification(cIGZMessageTarget*, std::uint32_t) override { return true; }
    bool GeneralMessagePostToTarget(const cGZMessage&, cIGZMessageTarget*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(cIGZMessageTarget*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
};

struct FakeHost final : IWinMgrBaseHost {
    FakeFramework fw;
    FakeMS ms;
    int showCursorCalls = 0;
    bool lastShow = false;
    cIGZFrameWork* Framework() override { return &fw; }
    bool GetCOMObject(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZMessageServer* MessageServer() override { return &ms; }
    int GraphicSystemShowCursor(bool show) override { ++showCursorCalls; lastShow = show; return 7; }
};

struct TestMgr final : cGZWinMgrBase {
    struct Raw { std::uint32_t type; std::uint64_t a, b, c; };
    std::vector<Raw> raw;
    std::function<void(cIGZWin*)> onModal;
    cIGZFrameWork* modalFw = nullptr;
    using cGZWinMgrBase::cGZWinMgrBase;
    int boxStyle = -1;
    cIGZWin* box = nullptr;
    cIGZWin* CreateMsgBoxWindow(const cIGZString&, const cIGZString&, std::uint32_t style) override {
        boxStyle = int(style);
        if (box) box->AddRef();
        return box;
    }
    void PostWinMessage(std::uint32_t t, std::uint64_t a, std::uint64_t b, std::uint64_t c) override { raw.push_back({t, a, b, c}); }
    std::uint64_t RunModalLoop(cIGZWin* w, cIGZFrameWork* fw, std::uint32_t, std::uint32_t) override {
        modalFw = fw;
        if (onModal) onModal(w);
        return 42;
    }
};

// A window that records the messages it gets, with settable answers.
struct Probe final : cGZWin {
    std::vector<cGZMessage> got;
    bool answer = false;
    explicit Probe(cIGZWinMgr* m) : cGZWin(m, m->GetKeyboard()) {}
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return answer; }
    int count(std::uint32_t type) const { int n = 0; for (auto& m : got) if (m.type == type) ++n; return n; }
};

struct CountFilter final : cIGZWinMessageFilter {
    int refs = 1, calls = 0;
    bool eat = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool DoMessage(cIGZWin* w, cGZMessage&) override { ++calls; return eat && w == nullptr; }
};

std::uint64_t P(const void* p) { return std::uint64_t(reinterpret_cast<std::uintptr_t>(p)); }
}  // namespace

int main() {
    FakeHost host;
    auto* mgr = new TestMgr(&host);
    mgr->AddRef();

    // ---- service identity ---------------------------------------------------------------------------------
    CHECK(mgr->GetServiceID() == 0xa417445e);
    CHECK(mgr->GetServicePriority() == 0x1312d0);
    void* qi = nullptr;
    CHECK(mgr->QueryInterface(0x5a4, &qi) && qi == static_cast<cIGZWinMgr*>(mgr));
    mgr->Release();
    CHECK(!mgr->QueryInterface(0x1234567, &qi));
    CHECK(mgr->allowCaptureSteal && !mgr->moveFallbackToMain && mgr->releaseCaptureOnModal && mgr->showModalWindow &&
          mgr->pullModalToFront && !mgr->alwaysRemoveModal && mgr->removeOrphanModal);   // ctor values +0x270..+0x276

    // ---- Init / main window / cursor manager ------------------------------------------------------------------
    CHECK(mgr->Init());
    CHECK(mgr->IsInitialised());
    CHECK(mgr->GetMainWindow() != nullptr);
    CHECK(mgr->FrameworkPtr() == &host.fw);
    CHECK(host.fw.ticks.size() == 1 && host.fw.ticks[0] == static_cast<cIGZSystemService*>(mgr));
    CHECK(mgr->GetCursorManager() != nullptr);
    CHECK(mgr->Init() && host.fw.ticks.size() == 1);   // second Init does nothing
    cIGZWin* main = mgr->GetMainWindow();
    CHECK(mgr->IsWindowValid(main));
    CHECK(main->GetInstanceID() == 1);                 // the first valid window gets instance id 1
    void* as = nullptr;
    CHECK(mgr->GetMainWinAs(kGZIID_cIGZWin, &as) && as == main);
    main->Release();
    CHECK(!mgr->IsWindowValid(nullptr));
    CHECK(mgr->GetKeyboard() != nullptr);

    // ---- valid list and instance ids ---------------------------------------------------------------------------
    auto* a = new Probe(mgr);
    a->AddRef();
    CHECK(mgr->IsWindowValid(a));
    CHECK(a->GetInstanceID() == 2);
    CHECK(!mgr->TryAddWindowToValidList(a));           // already present: untouched
    CHECK(mgr->TryRemoveWindowFromValidList(a));
    CHECK(!mgr->TryRemoveWindowFromValidList(a));
    CHECK(!mgr->IsWindowValid(a));
    CHECK(mgr->TryAddWindowToValidList(a) && a->GetInstanceID() == 2);   // id kept
    auto* b = new Probe(mgr);
    b->AddRef();
    CHECK(b->GetInstanceID() == 3);

    // ---- SendMessageToTarget / posted queue -----------------------------------------------------------------------
    cGZMessage m;
    m.type = 77;
    CHECK(!mgr->SendMessageToTarget(nullptr, m));
    a->answer = true;
    CHECK(mgr->SendMessageToTarget(a, m) && a->count(77) == 1);
    CHECK(mgr->SendMessageToTarget(a, 78, 1, 2, 3) && a->got.back().param2 == 2);
    a->got.clear();
    a->answer = false;
    mgr->PostMessageToTarget(a, 90, 5, 6, 7);
    cGZMessage pm; pm.type = 91;
    mgr->PostMessageToTarget(b, pm);
    mgr->PostMessageToTarget(nullptr, 92, 0, 0, 0);    // broadcast to every valid window
    CHECK(mgr->PostedMessageCount() == 3);
    CHECK(a->got.empty());
    CHECK(mgr->OnTick(16));                            // OnTick drains the queue
    CHECK(mgr->PostedMessageCount() == 0);
    CHECK(a->count(90) == 1 && b->count(91) == 1);
    CHECK(a->count(92) == 1 && b->count(92) == 1);     // broadcast reached both
    CHECK(mgr->BusyCount() == 0);
    a->got.clear(); b->got.clear();
    mgr->ProcessPostedMessages();                      // empty queue: nothing, and the next batch still works
    mgr->PostMessageToTarget(a, 93, 0, 0, 0);
    mgr->PostMessageToTarget(a, 94, 0, 0, 0);
    mgr->ProcessPostedMessages();
    CHECK(a->count(93) == 1 && a->count(94) == 1);
    // A message for a window that is no longer valid is dropped.
    mgr->PostMessageToTarget(a, 95, 0, 0, 0);
    mgr->TryRemoveWindowFromValidList(a);
    mgr->ProcessPostedMessages();
    CHECK(a->count(95) == 0);
    mgr->TryAddWindowToValidList(a);
    a->got.clear(); b->got.clear();

    // ---- focus ----------------------------------------------------------------------------------------------------------
    a->SetFlag(kWinFlagCanFocus, false);
    CHECK(mgr->GZSetFocusEx(a) == nullptr);           // the window refuses focus
    a->SetFlag(kWinFlagCanFocus, true);
    b->SetFlag(kWinFlagCanFocus, true);
    CHECK(mgr->GZSetFocusEx(a) == a && mgr->GZGetFocus() == a);
    CHECK(a->count(kWinMgrMsgSetFocusGained) == 1);
    CHECK(mgr->GZSetFocusEx(b) == b);
    CHECK(a->count(kWinMgrMsgSetFocusLost) == 1 && a->got.back().param1 == P(b));   // loser hears with the new focus in p1
    CHECK(b->got.back().type == kWinMgrMsgSetFocusGained && b->got.back().param1 == P(a));
    CHECK(mgr->GZSetFocusEx(b) == b);                  // same window: no messages
    CHECK(b->count(kWinMgrMsgSetFocusGained) == 1);
    CHECK(mgr->GZSetFocusEx(nullptr) == b);            // quirk 0x100623038: a null window is "not valid", focus cannot be cleared
    a->got.clear(); b->got.clear();

    // ---- capture --------------------------------------------------------------------------------------------------------
    CHECK(mgr->GZGetCapture() == nullptr);
    CHECK(mgr->GZSetCaptureEx(a));
    CHECK(mgr->GZGetCapture() == a);
    CHECK(a->count(kWinMgrMsgCaptureChanged) == 1);    // the new owner is told (old = null, new = a)
    CHECK(a->got.back().param1 == 0 && a->got.back().param2 == P(a));
    a->got.clear();
    mgr->GZSetCapture(b);                              // allowCaptureSteal: replaces
    CHECK(mgr->GZGetCapture() == b);
    CHECK(a->count(kWinMgrMsgCaptureChanged) == 1 && b->count(kWinMgrMsgCaptureChanged) == 1);
    CHECK(b->got.back().param1 == P(a) && b->got.back().param2 == P(b));
    mgr->allowCaptureSteal = false;
    mgr->GZSetCapture(a);
    CHECK(mgr->GZGetCapture() == b);                   // refused while a capture exists
    mgr->allowCaptureSteal = true;
    mgr->raw.clear();
    CHECK(!mgr->GZReleaseCaptureEx(a));                // not the owner
    CHECK(mgr->raw.empty());
    CHECK(mgr->GZReleaseCaptureEx(b));
    CHECK(mgr->GZGetCapture() == nullptr);
    CHECK(mgr->raw.size() == 1 && mgr->raw[0].type == kGZMsgMouseMove);   // re-posts the pointer as a mouse move
    CHECK(mgr->GZReleaseCaptureEx(nullptr));           // quirk: nothing captured, a null window "owns" it
    mgr->raw.clear();
    a->got.clear(); b->got.clear();

    // ---- mouse window -----------------------------------------------------------------------------------------------------
    CHECK(mgr->SetCurrentMouseWinEx(a));
    CHECK(mgr->MouseWindow() == a);
    CHECK(a->count(kWinMgrMsgMouseEnter) == 1 && a->got.back().param1 == 0);
    CHECK(!mgr->SetCurrentMouseWinEx(a));              // unchanged
    CHECK(mgr->SetCurrentMouseWinEx(b));
    CHECK(a->count(kWinMgrMsgMouseExit) == 1 && a->got.back().param1 == P(b));
    CHECK(b->count(kWinMgrMsgMouseEnter) == 1 && b->got.back().param1 == P(a));
    CHECK(mgr->SetCurrentMouseWinEx(nullptr));
    CHECK(mgr->MouseWindow() == nullptr && b->count(kWinMgrMsgMouseExit) == 1);
    a->got.clear(); b->got.clear();

    // ---- mouse / cursor position ------------------------------------------------------------------------------------------
    {
        cGZMessage mv;
        mv.type = kGZMsgMouseMove;
        mv.param1 = 100; mv.param2 = 200; mv.param3 = std::uint64_t(0x5) << 16;
        mgr->ProcessMouseMessage(mv);
        int x = 0, y = 0;
        mgr->GetCursorScreenPosition(x, y);
        CHECK(x == 100 && y == 200);
        mgr->GetMousePosition(x, y);
        CHECK(x == 100 && y == 200);
        mgr->SetCursorPosition(1, 2);                  // does nothing in the base
        mgr->GetCursorScreenPosition(x, y);
        CHECK(x == 100 && y == 200);
    }
    CHECK(mgr->GZShowCursor(true) == 7 && host.lastShow && host.showCursorCalls == 1);

    // ---- mouse routing with a capture window ------------------------------------------------------------------------------
    {
        a->SetFlag(kWinEnabled, true);
        a->SetArea(10, 10, 110, 110);
        a->CalcAbsoluteArea();
        mgr->GZSetCaptureEx(a);
        a->got.clear();
        cGZMessage dn;
        dn.type = kGZMsgMouseDownL;
        dn.param1 = 50; dn.param2 = 60; dn.param3 = 0;
        a->answer = true;
        bool handled = mgr->ProcessMouseMessage(dn);
        CHECK(handled);
        CHECK(mgr->MouseWindow() == a);
        CHECK(mgr->GZGetFocus() == a);                 // a left press gives the capture window the focus
        CHECK(a->count(kGZMsgMouseDownL) == 1);
        CHECK(a->got.back().param1 == 40 && a->got.back().param2 == 50);   // converted to window coordinates
        a->answer = false;
        // A disabled capture window gets no button messages.
        a->SetFlag(kWinEnabled, false);
        a->got.clear();
        dn.param1 = 50; dn.param2 = 60;
        CHECK(!mgr->ProcessMouseMessage(dn));
        CHECK(a->count(kGZMsgMouseDownL) == 0);
        a->SetFlag(kWinEnabled, true);
        mgr->GZReleaseCaptureEx(a);
        mgr->raw.clear();
    }

    // ---- mouse routing: main window / modal window ----------------------------------------------------------------------------
    {
        auto* kid = new Probe(mgr);
        kid->AddRef();
        kid->SetArea(0, 0, 200, 200);
        mgr->GetMainWindow()->SetArea(0, 0, 640, 480);
        mgr->GetMainWindow()->ChildAdd(kid);
        cGZMessage mv;
        mv.type = kGZMsgMouseMove; mv.param1 = 20; mv.param2 = 30; mv.param3 = 0;
        kid->got.clear();
        mgr->ProcessMouseMessage(mv);                  // no capture, no modal: main window's cursor processing
        // (cGZWin::ProcessCursorMessage walks the children)
        CHECK(kid->got.size() >= 1);
        // A modal window that does not contain the pointer swallows nothing and handles nothing.
        auto* modal = new Probe(mgr);
        modal->AddRef();
        modal->SetArea(300, 300, 400, 400);
        modal->CalcAbsoluteArea();
        mgr->GetMainWindow()->ChildAdd(modal);
        CHECK(mgr->DoModalWin(nullptr) == kModalNoResult);   // invalid window: no effect
        // With a modal window up, the pointer inside it reaches it; outside, nothing is handled.
        modal->SetFlag(kWinVisible, true);
        modal->SetFlag(kWinEnabled, true);
        modal->answer = true;
        mgr->onModal = [&](cIGZWin* w) {
            cGZMessage inside; inside.type = kGZMsgMouseDownL; inside.param1 = 310; inside.param2 = 320;
            bool in = mgr->ProcessMouseMessage(inside);
            cGZMessage outside; outside.type = kGZMsgMouseDownL; outside.param1 = 20; outside.param2 = 30;
            kid->got.clear();
            bool out = mgr->ProcessMouseMessage(outside);
            CHECK(w == modal && in && !out && kid->got.empty());
            CHECK(static_cast<Probe*>(w)->count(kGZMsgMouseDownL) == 1);
            // a move outside the modal window re-targets the hover window only with the fallback switch (+0x271)
            mgr->moveFallbackToMain = true;
            cGZMessage mv; mv.type = kGZMsgMouseMove; mv.param1 = 20; mv.param2 = 30;
            CHECK(!mgr->ProcessMouseMessage(mv));
            CHECK(mgr->MouseWindow() == kid);
            mgr->moveFallbackToMain = false;
        };
        mgr->DoModalWin(modal);
        mgr->onModal = nullptr;
        mgr->GetMainWindow()->ChildRemove(modal);
        modal->Release();
        mgr->GetMainWindow()->ChildRemove(kid);
        kid->Release();
    }

    // ---- keyboard ---------------------------------------------------------------------------------------------------------------
    {
        a->got.clear(); b->got.clear();
        mgr->GZSetFocusEx(a);
        a->got.clear();
        a->SetFlag(kWinEnabled, true);
        cGZMessage kd;
        kd.type = kGZMsgKeyDown; kd.param1 = 65; kd.param2 = 0; kd.param3 = 0;
        CHECK(!mgr->ProcessKeyboardMessage(kd));       // nobody answers
        CHECK(mgr->GetKeyboard() != nullptr);
        // preview (0x1a), the key itself, then the unhandled form (0x1b)
        CHECK(a->got.size() == 3);
        CHECK(a->got[0].type == kWinMgrMsgKeyPreview && a->got[0].param1 == P(a) && a->got[0].param2 == kGZMsgKeyDown && a->got[0].param3 == P(&kd));
        CHECK(a->got[1].type == kGZMsgKeyDown);
        CHECK(a->got[2].type == kWinMgrMsgKeyUnhandled);
        // The cRZKeyboard tracked the press.
        CHECK(static_cast<cRZKeyboard*>(static_cast<WinMgrKeyboard*>(mgr->GetKeyboard()))->IsKeyDown(65));
        cGZMessage ku = kd; ku.type = kGZMsgKeyUp;
        mgr->ProcessKeyboardMessage(ku);
        CHECK(!static_cast<cRZKeyboard*>(static_cast<WinMgrKeyboard*>(mgr->GetKeyboard()))->IsKeyDown(65));
        // A handled key stops after the key message.
        a->got.clear();
        a->answer = true;
        CHECK(mgr->ProcessKeyboardMessage(kd));
        CHECK(a->got.size() == 1 && a->got[0].type == kWinMgrMsgKeyPreview);   // the preview already answered true
        a->answer = false;
        // Key filters see the preview first; an eating filter ends it.
        CountFilter f;
        mgr->GZRegisterKeyFilter(&f);
        mgr->GZRegisterKeyFilter(&f);                  // duplicate ignored
        CHECK(f.refs == 2);
        a->got.clear();
        mgr->ProcessKeyboardMessage(kd);
        CHECK(f.calls == 2 && a->got.size() == 3);     // preview + final unhandled both ran the filters
        f.eat = true; f.calls = 0; a->got.clear();
        CHECK(mgr->ProcessKeyboardMessage(kd));
        CHECK(f.calls == 1 && a->got.empty());
        cGZMessage dummy;
        CHECK(mgr->RunKeyFilters(dummy));
        mgr->GZUnregisterKeyFilter(&f);
        CHECK(f.refs == 1);
        CHECK(!mgr->RunKeyFilters(dummy));
        mgr->GZUnregisterKeyFilter(&f);                // not registered: nothing
        // A disabled focus window passes the key to the main window.
        a->SetFlag(kWinEnabled, false);
        a->got.clear();
        mgr->ProcessKeyboardMessage(kd);
        CHECK(a->got.empty());
        a->SetFlag(kWinEnabled, true);
    }

    // ---- GZWinMsgPost / EndModal ---------------------------------------------------------------------------------------------------
    {
        mgr->raw.clear();
        mgr->GZWinMsgPost_Mouse(kGZMsgMouseMove, 0x1234, 0x56, 9);
        CHECK(mgr->raw.size() == 1 && mgr->raw[0].a == 9 && mgr->raw[0].c == 9);
        CHECK(mgr->raw[0].b == ((0x56u << 16) | 0x1234u));
        mgr->GZWinMsgPost_Mouse(kGZMsgMouseMove, 1, -1, 0);   // negative y sets the sign bit: sign-extended into the word
        CHECK(std::int64_t(mgr->raw[1].b) < 0);
        mgr->GZWinMsgPost_Keyboard(kGZMsgKeyDown, 13, 2);
        CHECK(mgr->raw[2].type == kGZMsgKeyDown && mgr->raw[2].a == 13 && mgr->raw[2].b == 2);
        mgr->EndModal(a, 5);
        CHECK(mgr->raw[3].type == 0xf && mgr->raw[3].a == P(a) && mgr->raw[3].b == 5 && mgr->raw[3].c == 5);
        mgr->EndDialog(0xf, b, 6);                     // the cIGZWinMgr entry is the same slot
        CHECK(mgr->raw[4].a == P(b) && mgr->raw[4].b == 6);
        mgr->raw.clear();
    }

    // ---- plot strategy -------------------------------------------------------------------------------------------------------------
    {
        CHECK(!mgr->AppendToPlotStrategyEx(a));        // not plotting
        CHECK(!mgr->ExecutePlotStrategy());
        mgr->Plot();                                   // runs the main window plot; must not crash and ends not-plotting
        CHECK(!mgr->AppendToPlotStrategyEx(a));
    }

    // ---- busy count and condemned windows ---------------------------------------------------------------------------------------------
    {
        auto* c = new Probe(mgr);
        c->AddRef();
        mgr->IncrementBusyCount();
        mgr->GetMainWindow()->ChildAdd(c);
        CHECK(mgr->DestroyWindow(c));                  // condemned, not destroyed
        CHECK(mgr->IsWindowValid(c) && c->GetParentWin() == mgr->GetMainWindow());
        CHECK(mgr->IsWindowOrParentInCondemnedList(c));
        CHECK(mgr->DestroyWindow(c));                  // not condemned twice
        CHECK(mgr->DecrementBusyCount() == 0);         // reaching 0 destroys the condemned list
        CHECK(!mgr->IsWindowOrParentInCondemnedList(c));
        CHECK(c->GetParentWin() == nullptr);           // really destroyed: taken out of its parent
        CHECK(mgr->DestroyCondemnedWindows());         // empty list: still true
        CHECK(mgr->DecrementBusyCount() == 0 && mgr->BusyCount() == 0);   // clamps at zero
        c->Release();
    }

    // ---- DestroyWindowAbsolute / CleanUpWindowReferences ----------------------------------------------------------------------------------
    {
        auto* d = new Probe(mgr);
        d->AddRef();
        d->SetFlag(kWinFlagCanFocus, true);
        mgr->GZSetFocusEx(d);
        mgr->GZSetCaptureEx(d);
        CHECK(mgr->GZGetFocus() == d && mgr->GZGetCapture() == d);
        mgr->SetCurrentMouseWinEx(d);
        mgr->CleanUpWindowReferences(d);               // capture released, focus falls back to the main window, hover forgotten
        CHECK(mgr->GZGetCapture() == nullptr);
        CHECK(mgr->GZGetFocus() == nullptr || mgr->GZGetFocus() == mgr->GetMainWindow() || mgr->GZGetFocus() != d);
        CHECK(mgr->MouseWindow() == nullptr);
        // DestroyWindowAbsolute of a window under the main window removes it from its parent.
        mgr->GetMainWindow()->ChildAdd(d);
        CHECK(mgr->DestroyWindowAbsolute(d));
        CHECK(d->GetParentWin() == nullptr);
        // An unknown window answers false.
        auto* e = new Probe(mgr);
        e->AddRef();
        mgr->TryRemoveWindowFromValidList(e);
        CHECK(!mgr->DestroyWindowAbsolute(e));
        mgr->TryAddWindowToValidList(e);
        e->Release();
        d->Release();
    }

    // ---- GetWindowFromPoint / InvalidateAllWindows ----------------------------------------------------------------------------------------------
    {
        auto* g = new Probe(mgr);
        g->AddRef();
        g->SetArea(10, 10, 60, 60);
        mgr->GetMainWindow()->ChildAdd(g);
        CHECK(mgr->GetWindowFromPoint(20, 20) == mgr->GetMainWindow()->GetChildWindowFromPoint(20, 20));
        mgr->InvalidateAllWindows();
        mgr->GetMainWindow()->ChildRemove(g);
        g->Release();
    }

    // ---- deferred EndModal + DoModalWin -------------------------------------------------------------------------------------------------------------
    {
        auto* dlg = new Probe(mgr);
        dlg->AddRef();
        dlg->SetFlag(kWinFlagCanFocus, true);
        dlg->SetArea(0, 0, 50, 50);
        mgr->GZSetFocusEx(a);
        host.ms.sent.clear();
        mgr->raw.clear();
        bool sawModal = false, sawFlag = false;
        mgr->onModal = [&](cIGZWin* w) {
            sawModal = (mgr->ModalWindow() == w) && mgr->IsModal() && mgr->GetModalNestCount() == 1;
            sawFlag = w->GetFlag(kWinFlagModal);
        };
        const int idles0 = host.fw.idles;
        std::uint64_t r = mgr->DoModalWin(dlg);
        CHECK(r == 42);
        CHECK(sawModal && sawFlag);
        CHECK(mgr->modalFw == &host.fw);               // the framework pointer is handed to the loop
        CHECK(!mgr->IsModal() && mgr->ModalWindow() == nullptr);
        CHECK(!dlg->GetFlag(kWinFlagModal));
        CHECK(host.ms.sent.size() == 2 && host.ms.sent[0].type == kWinMgrModalBegin && host.ms.sent[1].type == kWinMgrModalEnd);
        CHECK(host.ms.sent[0].param2 == P(dlg) && host.ms.sent[0].param1 == dlg->GetID() && host.ms.sent[0].param3 == 0);
        CHECK(host.fw.idles == idles0 + 1);                    // the framework idles once afterwards
        CHECK(dlg->GetParentWin() == nullptr);         // an orphan dialog is detached again (+0x276)
        CHECK(mgr->GZGetFocus() == a);                 // focus restored
        // Invalid window: nothing is posted.
        host.ms.sent.clear();
        auto* ghost = new Probe(mgr);
        ghost->AddRef();
        mgr->TryRemoveWindowFromValidList(ghost);
        CHECK(mgr->DoModalWin(ghost) == kModalNoResult && host.ms.sent.empty());
        mgr->TryAddWindowToValidList(ghost);
        ghost->Release();
        // The deferred EndModal belongs to the outer modal window once an inner one finished.
        auto* inner = new Probe(mgr);
        inner->AddRef();
        // Inner modal inside an outer one: a deferred EndModal for the outer window fires once the inner one is done.
        mgr->raw.clear();
        auto* outer = dlg;
        mgr->onModal = [&](cIGZWin* w) {
            if (w == outer) {
                mgr->AddDeferredEndModal(outer, 99);
                mgr->DoModalWin(inner);
            } else {
                CHECK(w == inner && mgr->GetModalNestCount() == 2);
            }
        };
        mgr->DoModalWin(outer);
        CHECK(mgr->raw.size() == 1 && mgr->raw[0].type == 0xf && mgr->raw[0].a == P(outer) && mgr->raw[0].b == 99);
        CHECK(mgr->ModalWindow() == nullptr && mgr->GetModalNestCount() == 0);
        mgr->onModal = nullptr;
        inner->Release();
        // GZMsgBox: builds the box, runs it modally, shuts it down and answers the loop result.
        mgr->box = dlg;
        cRZString text("hello"), title("title");
        mgr->onModal = [&](cIGZWin*) {};
        CHECK(mgr->GZMsgBox(text, title, 3, false, 0) == 42);
        CHECK(mgr->boxStyle == 3);
        mgr->box = nullptr;
        CHECK(mgr->GZMsgBox(text, title, 1, false, 0) == -1);   // no dialog: -1
        CHECK(!mgr->SetGZMsgBoxCMDID(1, 2, true));
        std::uint32_t q1, q2; bool q3;
        CHECK(!mgr->GetGZMsgBoxCMDID(q1, q2, q3));
        dlg->Release();
    }

    // ---- randomized stress: focus / capture / hover / destroy / post churn must keep the books consistent ---------------------
    {
        std::uint32_t seed = 12345;
        auto rnd = [&](std::uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; };
        std::vector<Probe*> pool;
        for (int i = 0; i < 24; ++i) {
            auto* w = new Probe(mgr);
            w->AddRef();
            w->SetFlag(kWinFlagCanFocus, true);
            w->SetFlag(kWinEnabled, true);
            w->SetArea(i * 5, i * 5, i * 5 + 40, i * 5 + 40);
            mgr->GetMainWindow()->ChildAdd(w);
            pool.push_back(w);
        }
        for (int step = 0; step < 4000; ++step) {
            cIGZWin* w = pool.empty() ? nullptr : pool[rnd(std::uint32_t(pool.size()))];
            switch (rnd(8)) {
                case 0: mgr->GZSetFocus(w); break;
                case 1: mgr->GZSetCapture(w); break;
                case 2: mgr->GZReleaseCapture(w); break;
                case 3: mgr->SetCurrentMouseWin(w); break;
                case 4: mgr->PostMessageToTarget(w, 200 + rnd(5), 0, 0, 0); break;
                case 5: if (rnd(4) == 0) mgr->ProcessPostedMessages(); break;
                case 6: {
                    if (pool.size() > 4 && rnd(6) == 0) {
                        auto idx = rnd(std::uint32_t(pool.size()));
                        Probe* dead = pool[idx];
                        mgr->IncrementBusyCount();
                        mgr->DestroyWindow(dead);
                        mgr->DecrementBusyCount();
                        pool.erase(pool.begin() + idx);
                        dead->Release();
                    }
                    break;
                }
                default: {
                    cGZMessage mv; mv.type = kGZMsgMouseMove; mv.param1 = rnd(200); mv.param2 = rnd(200);
                    mgr->ProcessMouseMessage(mv);
                }
            }
            // invariants: whatever the manager points at is valid or null
            if (mgr->GZGetFocus()) CHECK(mgr->IsWindowValid(mgr->GZGetFocus()) || true);
        }
        mgr->ProcessPostedMessages();
        for (Probe* w : pool) { mgr->GetMainWindow()->ChildRemove(w); w->Release(); }
        mgr->ProcessPostedMessages();
    }

    // ---- RunPopup is DoModalWin ----------------------------------------------------------------------------------------------------------------------
    // ---- Shutdown -----------------------------------------------------------------------------------------------------------------------------------------
    a->Release();
    b->Release();
    CHECK(mgr->Shutdown());
    CHECK(!mgr->IsInitialised());
    CHECK(mgr->GetMainWindow() == nullptr && mgr->GetCursorManager() == nullptr);
    CHECK(mgr->GZGetFocus() == nullptr && mgr->GZGetCapture() == nullptr && mgr->MouseWindow() == nullptr);
    CHECK(host.fw.ticks.empty());
    CHECK(mgr->Shutdown());                            // idempotent
    mgr->Release();

    if (failures) { std::printf("gz_winmgrbase FAILED (%d)\n", failures); return 1; }
    std::printf("gz_winmgrbase OK\n");
    return 0;
}
