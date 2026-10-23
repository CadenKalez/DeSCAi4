#include <cstdio>

#include "fake_winmgr.h"
#include "sc4/WinSmallViews.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
using Mgr = FakeMgr;
int Ends(const FakeMgr& m) { return (int)m.dialogs.size(); }
std::uint32_t Code(const FakeMgr& m) { return m.dialogs.empty() ? 0 : m.dialogs.back().code; }
struct Renderer final : IRegion3DRenderer {
    int refs = 1, draws = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    void DrawRegion3D() override { ++draws; }
};
}

int main() {
    {   // paint-count delay
        Mgr m;
        auto* d = new cSC4WinGameDelay(3, cSC4WinGameDelay::kDelayPaints, &m);
        d->AddRef();
        d->Init();                      // consumes 1 (3 -> 2)
        CHECK(Ends(m) == 0);
        d->GZPaint(); CHECK(Ends(m) == 0);   // 1
        d->GZPaint(); CHECK(Ends(m) == 1 && Code(m) == 0xf);   // 0 -> ends
        d->Release();
        Mgr m1;
        auto* one = new cSC4WinGameDelay(1, cSC4WinGameDelay::kDelayPaints, &m1);
        one->AddRef();
        one->Init(); CHECK(Ends(m1) == 1);   // ends inside Init
        one->Release();
    }
    {   // timer delay of 0 ms ends at once on first paint
        Mgr m;
        auto* d = new cSC4WinGameDelay(0, cSC4WinGameDelay::kDelayMilliseconds, &m);
        d->AddRef();
        d->GZPaint(); CHECK(Ends(m) == 1);
        d->Release();
        auto* far = new cSC4WinGameDelay(1000000, cSC4WinGameDelay::kDelayMilliseconds, &m);
        far->AddRef(); far->Init(); far->GZPaint(); CHECK(Ends(m) == 1);
        far->Release();
    }
    {   // gen transparent
        FakeMgr m;
        auto* w = new cSC4WinGenTransparent(&m); w->AddRef();
        w->SetArea(0, 0, 100, 100);
        auto* c = new cGZWin(&m); c->AddRef();
        c->SetArea(10, 10, 30, 30);
        w->ChildAdd(c);
        CHECK(w->IsPointInWindowWindowCoordinates(15, 15));
        CHECK(!w->IsPointInWindowWindowCoordinates(50, 50));
        CHECK(!w->IsPointInWindowWindowCoordinates(-1, 15));
        CHECK(!w->IsPointInWindowWindowCoordinates(150, 15));
        c->HideWindow();
        CHECK(!w->IsPointInWindowWindowCoordinates(15, 15));
        c->ShowWindow(); c->SetFlag(kWinIgnoreCursor, true);
        CHECK(!w->IsPointInWindowWindowCoordinates(15, 15));
        c->Release(); w->Release();
    }
    {   // region 3d view
        FakeMgr m;
        Renderer r;
        SetWinRegion3DRendererService(&r);
        auto* v = new cSC4WinRegion3DView(&m); v->AddRef();
        v->Init();
        CHECK(r.refs == 2);
        v->Shutdown(); CHECK(r.refs == 1);
        v->Init(); v->Release(); CHECK(r.refs == 1);
        SetWinRegion3DRendererService(nullptr);
    }
    if (!failures) std::printf("winsmallviews OK\n");
    return failures;
}
