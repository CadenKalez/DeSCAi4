#include <cstdio>

#include <SDL.h>

#include "fake_winmgr2.h"
#include "sc4/WinRegionScroll.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Image final : IWinBuffer {
    int refs = 1; cRZRect bounds{0, 0, 20, 10}; std::uint32_t transparency = 0; bool haveTransparency = false;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    const cRZRect* GetBounds() const override { return &bounds; }
    std::uint32_t MakeNativeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return (r << 16) | (g << 8) | b; }
    void NativeToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override {}
    void* GetDrawContext() override { return nullptr; }
    bool SetFadeAlpha(std::uint8_t) override { return false; }
    void SetTransparency(std::uint32_t c) override { transparency = c; haveTransparency = true; }
};
struct Host final : IRegionScrollHost {
    FakeMgr2* mgr; Image* image = nullptr; bool edge = false, inWindow = true; cGZWin* last = nullptr; int loads = 0;
    bool EdgeScrollEnabled() override { return edge; }
    bool CursorInGameWindow() override { return inWindow; }
    IWinBuffer* LoadScrollOriginImage() override { ++loads; if (image) image->AddRef(); return image; }
    cIGZWin* CreateImageWindow(IWinBuffer*) override {
        last = new cGZWin(mgr); last->AddRef(); last->SetID(kRegionScrollArrowWindowId); return last; }
};
struct Cursors final : cIGZCursorManager, cIGZCursor {
    std::vector<std::uint32_t> loaded;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZCursor* LoadCursor(std::uint32_t id) override { loaded.push_back(id); return this; }
    void SetCursor(cIGZCursor*) override {}
};
}

int main() {
    FakeMgr2 m;
    Cursors cursors; m.cursors = &cursors;
    Image img; Host host; host.mgr = &m; host.image = &img;
    SetRegionScrollHost(&host);
    auto* w = new cSC4WinRegionScroll(&m);
    w->AddRef();
    w->SetArea(0, 0, 400, 300);
    w->Init();
    CHECK(w->CursorId() == kRegionScrollCenterCursor);
    CHECK(!w->GetFlag(0x10000));

    // right button down at (100,100): origin marker created, centred, image keyed magenta, mouse captured
    w->GZOnMouseDownR(100, 100, 0);
    CHECK(m.capture == w);
    CHECK(host.loads == 1 && img.haveTransparency && img.transparency == 0xff00ff);
    CHECK(host.last && w->GetChildWindowFromID(kRegionScrollArrowWindowId) == host.last);
    CHECK(host.last->GetL() == 90 && host.last->GetT() == 95 && host.last->GetR() == 110 && host.last->GetB() == 105);
    CHECK(!host.last->GetFlag(2) && host.last->GetFlag(0x200000));
    CHECK(!w->GetFlag(0x200000));
    // a second press replaces the marker, image is only loaded once
    w->GZOnMouseDownR(120, 80, 0);
    CHECK(host.loads == 1);
    CHECK(m.destroyed.size() == 1);

    // move: dead zone -> no scroll and the centre cursor
    CHECK(!w->GZOnMouseMove(125, 85, 0));
    CHECK(w->MouseVectorX() == 0 && w->MouseVectorY() == 0 && w->CursorId() == kRegionScrollCenterCursor);
    // move right by 20: speed = max(1, (20-10)/3) = 3.33, unit direction (1,0); arrow cursor = right-centre
    w->GZOnMouseMove(140, 80, 0);
    CHECK(w->MouseVectorX() > 3.3f && w->MouseVectorX() < 3.34f && w->MouseVectorY() == 0);
    CHECK(w->CursorId() == 0x82a676a8 || w->CursorId() == 0xc2a676a4);   // row 1 column 2
    CHECK(w->CursorId() == 0xc2a676a4);
    // far up-left is clamped to 32 -> speed (32-10)/3
    w->GZOnMouseMove(120 - 100, 80 - 100, 0);
    CHECK(w->MouseVectorX() < 0 && w->MouseVectorY() < 0);
    CHECK(w->CursorId() == 0x02a67691);

    // paint: first call only starts the timer
    m.posted.clear();
    g_ticks = 1000;
    CHECK(w->GZPaint());
    CHECK(m.posted.empty());
    // with a mouse vector the second paint sends start + delta (elapsed may be 0 ms: delta 0 but still sent)
    w->GZOnMouseMove(160, 80, 0);
    g_ticks = 1500;   // 0.5 s
    w->GZPaint();
    CHECK(m.posted.size() == 2 && m.posted[0].type == kRegionScrollMsgStart && m.posted[1].type == kRegionScrollMsgDelta);
    CHECK(w->VelocityX() == w->MouseVectorX());
    CHECK(static_cast<std::int64_t>(m.posted[1].param1) == static_cast<int>(w->MouseVectorX() * 0.5f * 256.0f));
    CHECK(static_cast<std::int64_t>(m.posted[1].param2) == 0);
    // button up: marker removed, vector cleared, capture released
    w->GZOnMouseUpR(0, 0, 0);
    CHECK(m.capture == nullptr && w->MouseVectorX() == 0 && w->GetFlag(0x200000));
    CHECK(w->GetChildWindowFromID(kRegionScrollArrowWindowId) == nullptr);
    // next paint: velocity coasts -> still sends (with the old velocity), stop message comes once it reaches zero
    m.posted.clear();
    g_ticks = 1600;   // 0.1 s: velocity falls by 0.1 * 5 * v = half of v
    float v0 = w->VelocityX();
    w->GZPaint();
    CHECK(m.posted.size() == 1 && m.posted[0].type == kRegionScrollMsgDelta);   // sent with the pre-decay velocity
    CHECK(w->VelocityX() > 0.49f * v0 && w->VelocityX() < 0.51f * v0);
    m.posted.clear();
    g_ticks = 2600;   // 1 s: the sign flips so it stops dead, still sending the last frame
    w->GZPaint();
    CHECK(m.posted.size() == 1 && w->VelocityX() == 0.0f);
    m.posted.clear();
    g_ticks = 2700;
    w->GZPaint();
    CHECK(m.posted.size() == 1 && m.posted[0].type == kRegionScrollMsgStop);
    m.posted.clear();
    w->GZPaint();
    CHECK(m.posted.empty());

    // edge scrolling: pointer on the left border pushes -300 on x, the bottom border +300 on y
    host.edge = true; m.cursorX = 0; m.cursorY = 299;
    g_ticks = 2800;
    w->GZPaint();
    CHECK(w->VelocityX() == -300.0f && w->VelocityY() == 300.0f);
    m.cursorX = 399; m.cursorY = 150;
    w->GZPaint();
    CHECK(w->VelocityX() == 300.0f && w->VelocityY() == 0.0f);
    host.inWindow = false; m.cursorX = 0;
    w->GZPaint();   // not in the game window: no edge scroll, coasting

    w->Shutdown();
    CHECK(img.refs == 1);
    w->Release();
    SetRegionScrollHost(nullptr);
    if (!failures) std::printf("winregionscroll OK\n");
    return failures;
}
