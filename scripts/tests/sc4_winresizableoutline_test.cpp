#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr2.h"
#include "sc4/WinResizableOutline.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace sc4;

namespace {
struct Buf final : IWinBuffer {
    int locks = 0, unlocks = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    const cRZRect* GetBounds() const override { return nullptr; }
    std::uint32_t MakeNativeColor(std::uint8_t, std::uint8_t, std::uint8_t) override { return 0; }
    void NativeToRGB(std::uint32_t, std::uint8_t&, std::uint8_t&, std::uint8_t&) override {}
    void* GetDrawContext() override { return nullptr; }
    bool SetFadeAlpha(std::uint8_t) override { return false; }
    bool Lock(std::uint32_t f) override { ++locks; return f == 0x8004; }
    void Unlock(std::uint32_t) override { ++unlocks; }
};
struct Font final : IOutlineFont {
    int refs = 1; std::uint32_t style = 7; std::vector<std::string> drawn; std::vector<std::uint32_t> styleAtDraw;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t GetStyle() override { return style; }
    void SetStyle(std::uint32_t s) override { style = s; }
    void DrawText(IWinBuffer*, int x, int y, const char* t, std::uint32_t n, std::uint32_t, std::uint32_t, const cRZRect*, int) override {
        drawn.push_back(std::string(t, n) + "@" + std::to_string(x) + "," + std::to_string(y)); styleAtDraw.push_back(style); }
};
struct Host final : IResizableOutlineHost {
    Font font; std::string face; std::vector<std::string> lines;
    IOutlineFont* CreateFont(const char* f, int, int size, int, int) override { face = f + std::to_string(size); font.AddRef(); return &font; }
    void DrawLine(IWinBuffer*, int a, int b, int c, int d, std::uint32_t col, std::uint32_t) override {
        lines.push_back(std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d) + ":" + std::to_string(col)); }
};
struct Cursors final : cIGZCursorManager, cIGZCursor {
    std::vector<std::uint32_t> loaded;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    cIGZCursor* LoadCursor(std::uint32_t id) override { loaded.push_back(id); return this; }
    void SetCursor(cIGZCursor*) override {}
};
struct Outline final : cSC4WinResizableOutline {
    using cSC4WinResizableOutline::cSC4WinResizableOutline;
    void SetDraw(IWinBuffer* b, cRZRect a) { drawBuffer_ = b; drawArea_ = a; }
    cRZRect Area() const { return area_; }
};
}

int main() {
    FakeMgr2 m; Cursors cur; m.cursors = &cur; Host host; SetResizableOutlineHost(&host);
    auto* o = new Outline(&m); o->AddRef();
    o->SetArea(100, 100, 200, 180);
    o->SetResizingAndMoving(true, true);
    o->Init();
    CHECK(host.face == "Arial MT14" && host.font.refs == 2);
    CHECK(cur.loaded.size() == 1 && cur.loaded[0] == 0x11);

    using RT = cSC4WinResizableOutline;
    RT::ResizeType t; std::uint32_t c;
    CHECK(o->GetResizeTypeAndCursorForPosition(0, 0, t, c) && t == RT::kTopLeft && c == 0x68024b0e);
    CHECK(o->GetResizeTypeAndCursorForPosition(100, 0, t, c) && t == RT::kTopRight && c == 0x88024b02);
    CHECK(o->GetResizeTypeAndCursorForPosition(0, 80, t, c) && t == RT::kBottomLeft && c == 0x88024b02);
    CHECK(o->GetResizeTypeAndCursorForPosition(100, 80, t, c) && t == RT::kBottomRight && c == 0x68024b0e);
    CHECK(o->GetResizeTypeAndCursorForPosition(0, 40, t, c) && t == RT::kLeft && c == 0xc8024b17);
    CHECK(o->GetResizeTypeAndCursorForPosition(101, 40, t, c) && t == RT::kRight && c == 0xc8024b17);
    CHECK(o->GetResizeTypeAndCursorForPosition(50, 3, t, c) && t == RT::kTop && c == 0xe8024b13);
    CHECK(o->GetResizeTypeAndCursorForPosition(50, 82, t, c) && t == RT::kBottom && c == 0xe8024b13);
    CHECK(o->GetResizeTypeAndCursorForPosition(50, 40, t, c) && t == RT::kNone && c == 0x11);
    CHECK(o->GetResizeTypeAndCursorForPosition(106, 40, t, c) && t == RT::kNone);   // 6 past the edge is the body
    o->SetResizingAndMoving(false, false);
    CHECK(!o->GetResizeTypeAndCursorForPosition(50, 40, t, c));
    o->SetResizingAndMoving(true, false);
    CHECK(!o->GetResizeTypeAndCursorForPosition(50, 40, t, c));    // body but moving off
    CHECK(o->GetResizeTypeAndCursorForPosition(0, 0, t, c));
    o->SetResizingAndMoving(true, true);

    // hover updates the cursor once
    cur.loaded.clear();
    o->GZOnMouseMove(0, 40, 0); o->GZOnMouseMove(1, 41, 0);
    CHECK(o->CurrentCursorId() == 0xc8024b17 && cur.loaded.size() == 1);

    // drag the right edge 30 wider
    o->GZOnMouseDownL(100, 40, 0);
    CHECK(o->Dragging() && o->ActiveZone() == RT::kRight && m.capture == o);
    o->GZOnMouseMove(130, 40, 0);
    CHECK(o->Area().right == 230 && o->Area().left == 100 && o->Area().bottom == 180);
    // shrink beyond the minimum 8 pixels on the left edge
    o->GZOnMouseUpL(0, 0, 0);
    CHECK(!o->Dragging() && m.capture == nullptr);
    o->GZOnMouseDownL(0, 40, 0);
    o->GZOnMouseMove(500, 40, 0);   // left would pass the right edge
    CHECK(o->Area().left == 230 - 8 && o->Area().right == 230);
    o->GZOnMouseUpL(0, 0, 0);

    // move the body
    o->SetArea(100, 100, 200, 180);
    o->GZOnMouseDownL(50, 40, 0);
    CHECK(o->Dragging() && o->ActiveZone() == RT::kNone);
    o->GZOnMouseMove(60, 55, 0);   // offsets by (10, 15); remembered position stays
    CHECK(o->Area().left == 110 && o->Area().top == 115 && o->Area().right == 210 && o->Area().bottom == 195);
    o->GZOnMouseMove(60, 55, 0);   // same relative position again moves again (stored position unchanged)
    CHECK(o->Area().left == 120 && o->Area().top == 130);
    // turning moving off mid body-drag: zone 0 tests "resizing", still allowed
    o->SetResizingAndMoving(true, false);
    CHECK(o->Dragging());
    o->SetResizingAndMoving(false, true);
    CHECK(!o->Dragging() && m.capture == nullptr);

    // paint
    Buf buf; o->SetDraw(&buf, cRZRect{0, 0, 100, 80});
    o->SetColors(11, 22, 33);
    o->SetResizingAndMoving(true, true);
    o->GZPaint();
    CHECK(host.lines.size() == 4 && host.lines[0] == "0,0,0,79:11" && host.lines[1] == "0,0,99,0:11" &&
          host.lines[2] == "99,0,99,79:11" && host.lines[3] == "0,79,99,79:11");
    CHECK(buf.locks == 1 && buf.unlocks == 1 && host.font.drawn.empty());
    o->SetResizingAndMoving(false, false);
    host.lines.clear(); o->GZPaint();
    CHECK(host.lines[0] == "0,0,0,79:33");
    o->SetResizingAndMoving(true, true);
    o->GZOnMouseDownL(0, 40, 0);   // dragging
    host.lines.clear(); o->GZPaint();
    CHECK(host.lines[0] == "0,0,0,79:22");
    CHECK(host.font.drawn.size() == 1 && host.font.drawn[0] == "98 x 78@8,8" && host.font.styleAtDraw[0] == 1 && host.font.style == 7);

    o->Shutdown();
    CHECK(host.font.refs == 1);
    o->Release();
    SetResizableOutlineHost(nullptr);
    if (!failures) std::printf("winresizableoutline OK\n");
    return failures;
}
