#include <cstdio>

#include "sc4/ViewInputControl.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace sc4;

namespace {
struct Cursor final : cIGZCursor {
    int refs = 1;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
};
struct CursorMgr final : cIGZCursorManager {
    Cursor cursors[4];
    cIGZCursor* LoadCursor(std::uint32_t id) override { return id < 4 ? &cursors[id] : nullptr; }
    void SetCursor(cIGZCursor*) override {}
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
};
struct Mgr final : cIGZWinMgr {
    CursorMgr cursors; cIGZWin *capture = nullptr, *setCapture = nullptr, *released = nullptr; int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    cIGZWin* GetMainWindow() override { return nullptr; }
    void AppendToPlotStrategy(cIGZWin*) override {}
    void Plot() override {}
    void PostMessageToTarget(cIGZWin*, const cGZMessage&) override {}
    void AddWindowToValidList(cIGZWin*) override {}
    void RemoveWindowFromValidList(cIGZWin*) override {}
    void CleanUpWindowReferences(cIGZWin*) override {}
    bool DestroyWindow(cIGZWin*) override { return true; }
    bool IsWindowValid(cIGZWin*) override { return true; }
    cIGZWin* GetWindowFromPoint(int, int) override { return nullptr; }
    void SetCurrentMouseWin(cIGZWin*) override {}
    cIGZWin* GZGetCapture() override { return capture; }
    void GZSetCapture(cIGZWin* w) override { setCapture = capture = w; }
    void GZReleaseCapture(cIGZWin* w) override { released = w; capture = nullptr; }
    cIGZCursorManager* GetCursorManager() override { return &cursors; }
    cIGZWin* GZGetFocus() override { return nullptr; }
    void GZSetFocus(cIGZWin*) override {}
    cIGZKeyboard* GetKeyboard() override { return nullptr; }
};
struct View final : IViewInputView {
    int refs = 1, ended = -1; cSC4ViewInputControl* top = nullptr;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void EndInput(int r) override { ended = r; }
    cSC4ViewInputControl* TopControl() override { return top; }
};
struct Host final : IViewInputHost {
    View view; Mgr mgr; int soundChannel = 0; std::uint32_t sound = 0;
    IViewInputView* View3D() override { return &view; }
    cIGZWinMgr* WindowManager() override { return &mgr; }
    void PlaySound(int c, std::uint32_t id) override { soundChannel = c; sound = id; }
};
// a window double that only records SetCursor
}  // namespace

// cIGZWin has ~150 pure virtuals; the control only calls SetCursor on it, so test through a minimal subclass of cGZWin.
#include "gz/Win.h"
namespace {
struct CursorWin final : cGZWin {
    using cGZWin::cGZWin;
    cIGZCursor* seen = nullptr; int calls = 0;
    void SetCursor(cIGZCursor* c, bool update) override { seen = c; ++calls; cGZWin::SetCursor(c, false); (void)update; }
};
}

int main() {
    Host host;
    SetViewInputHost(&host);
    CursorWin win(&host.mgr);
    auto* c = new cSC4ViewInputControl();
    c->AddRef();
    CHECK(!c->IsSelfScrollingView() && c->ShouldStack() && c->IsKeyToBePassed(1, 2));
    CHECK(!c->OnCharacter('a') && !c->OnKeyDown(1, 2) && !c->OnMouseDownL(1, 2, 3) && !c->OnMouseWheel(1, 2, 3, 4) && !c->OnMouseExit());
    c->SetID(42); CHECK(c->GetID() == 42);
    c->SetWindow(&win);
    c->SetCursor(std::uint32_t(2));                       // not initialized yet: stored, window not told
    CHECK(c->GetCursor() == &host.mgr.cursors.cursors[2] && win.calls == 0 && host.mgr.cursors.cursors[2].refs == 2);
    CHECK(c->Init() && win.calls == 1 && win.seen == &host.mgr.cursors.cursors[2] && host.view.refs == 2 && host.mgr.refs == 1);
    CHECK(c->Init() && win.calls == 1);                    // second Init is a no-op
    c->SetCursor(std::uint32_t(3));
    CHECK(win.calls == 2 && c->GetCursor() == &host.mgr.cursors.cursors[3] && host.mgr.cursors.cursors[2].refs == 1);
    c->SetCursor(std::uint32_t(99));                      // unknown cursor id: unchanged
    CHECK(c->GetCursor() == &host.mgr.cursors.cursors[3]);

    CHECK(c->SetCapture() && host.mgr.setCapture == &win && c->AmCapturing());
    CHECK(c->ReleaseCapture() && host.mgr.released == &win && !c->AmCapturing());
    host.view.top = c;
    CHECK(c->IsOnTop());
    c->EndInput();
    CHECK(host.view.ended == 0);
    c->PlayResultSound(0);
    CHECK(host.sound == 0);
    c->PlayResultSound(77);
    CHECK(host.soundChannel == 1 && host.sound == 77);

    void* iface = nullptr;
    CHECK(c->QueryInterface(kGZIID_cISC4ViewInputControl, &iface) && iface == c && !c->QueryInterface(5, &iface));
    CHECK(c->Shutdown() && host.mgr.refs == 0 && host.view.refs == 1 && c->GetCursor() == nullptr);
    c->Release(); c->Release();                           // QueryInterface reference + the original one
    std::printf(failures ? "FAILED\n" : "sc4_viewinputcontrol ok\n");
    return failures ? 1 : 0;
}
