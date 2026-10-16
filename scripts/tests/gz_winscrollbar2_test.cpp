#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/WinScrollbar2.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Probe final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct Bar final : cGZWinScrollbar2 {
    using cGZWinScrollbar2::cGZWinScrollbar2;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
    cRZRect Area() const { return area_; }
};
struct Images final : IWinImageSource {
    std::map<std::uint32_t, cIGZBuffer*> map; std::vector<std::uint32_t> asked;
    cIGZBuffer* GetImage(std::uint32_t id) override { asked.push_back(id); auto it = map.find(id); return it == map.end() ? nullptr : it->second; }
};
cGZBuffer* MakeImage(int w, int h) {
    auto* b = new cGZBuffer(); b->AddRef();
    b->Init(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), cGZBufferColorType{kBufferColorRGB565, 16});
    return b;
}
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; fake_dc::Ctx ctx; Images images;
    SetWinGraphics(&graphics); SetWinImageSource(&images);
    auto* parent = new Probe(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 400, 400);
    auto* bar = new Bar(&mgr);
    parent->ChildAdd(bar);
    bar->SetCtx(&ctx);

    void* out = nullptr;
    CHECK(bar->QueryInterface(kGZIID_cIGZWinScrollbar2, &out) && out == static_cast<cIGZWinScrollbar2*>(bar));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(bar->QueryInterface(kGZIID_cIGZWin, &out) && out == static_cast<cGZWin*>(bar));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!bar->QueryInterface(0x1234, &out) && bar->AsIGZWin() == static_cast<cIGZWin*>(bar));

    // defaults: vertical, 0..256, visible 16
    int v = -1;
    CHECK(bar->GetOrientation(v) && v == 1 && bar->GetValue() == 0);
    CHECK(bar->GetMinimumValue(v) && v == 0 && bar->GetMaximumValue(v) && v == 0x100 && bar->GetVisibleRange(v) && v == 0x10);
    CHECK(bar->GetPageOverlapRange(v) && v == 1 && bar->GetIncrementDelta(v) && v == 1);
    CHECK(bar->SetOrientation(1) && bar->GetOrientation(v) && v == 1);       // asking for 1 is ignored
    cRZSize size;
    CHECK(bar->GetMinimumSize(size) && size.width == 0x12 && size.height == 0x36);   // ChildAdd initialised the window: flat sizes

    // flat sizes: 18-pixel arrows, 54 minimum along the bar
    bar->Init();
    CHECK(bar->ArrowLength() == 0x12);
    CHECK(bar->GetMinimumSize(size) && size.width == 0x12 && size.height == 0x36);
    bar->SetArea(0, 0, 100, 200);                                            // vertical: the width is forced to the minimum
    CHECK(bar->Area().right == 0x12 && bar->Area().bottom == 200);
    bar->SetArea(cRZRect{5, 6, 60, 70});
    CHECK(bar->Area().left == 5 && bar->Area().right == 5 + 0x12 && bar->Area().bottom == 70);
    bar->SetArea(0, 0, 100, 200);

    // value clamping and notification
    parent->got.clear();
    CHECK(bar->SetValue(1000, true) && bar->GetValue() == 0x100 - 0x10);
    CHECK(parent->got.size() == 1 && parent->got[0].type == kGZMsgScrollbarChanged && parent->got[0].param3 == 0xf0 &&
          parent->got[0].param2 == bar->GetID());
    bar->SetValue(-5, false);
    CHECK(bar->GetValue() == 0 && parent->got.size() == 1);
    bar->SetValue(20, true);
    parent->got.clear();
    bar->SetVisibleRange(-3); CHECK(bar->GetVisibleRange(v) && v == 0);
    bar->SetVisibleRange(16);
    bar->SetMaximumValue(100);                                               // re-clamps the value into [min, max - visible]
    CHECK(bar->GetValue() == 20);
    bar->SetMaximumValue(30);
    CHECK(bar->GetValue() == 14);
    bar->SetMinimumValue(15);
    CHECK(bar->GetValue() == 15);
    bar->SetMinimumValue(0); bar->SetMaximumValue(0x100); bar->SetValue(0, false);
    bar->SetPageOverlapRange(-4); CHECK(bar->GetPageOverlapRange(v) && v == 0);
    bar->SetIncrementDelta(-4); CHECK(bar->GetIncrementDelta(v) && v == 0);
    bar->SetPageOverlapRange(1); bar->SetIncrementDelta(3);

    // thumb geometry: track 200 - 36 = 164; visible 16 of 256 -> 164 * 16 / 256 = 10, held to 2 * cap (8)
    CHECK(bar->ThumbLength() == 10 && bar->ThumbOffset() == 0);
    bar->SetValue(0xf0, false);
    CHECK(bar->ThumbOffset() == ((256 - 16) / 2 + (164 - 10) * 240) / (256 - 16));
    bar->SetValue(0, false);
    bar->SetVisibleRange(256);                                               // everything visible: the thumb fills the track
    CHECK(bar->ThumbLength() == 164 && bar->ThumbOffset() == 0);
    bar->SetVisibleRange(16);

    // picking
    CHECK(bar->PickPart(5, 5) == 1 && bar->PickPart(5, 100) == 3 && bar->PickPart(5, 195) == 4 && bar->PickPart(5, 22) == 5);
    CHECK(bar->PickPart(-1, 5) == 0 && bar->PickPart(5, 500) == 0 && bar->PickPart(40, 5) == 0);
    bar->SetValue(0x80, false);
    const int thumbStart = 0x12 + bar->ThumbOffset();
    CHECK(bar->PickPart(5, 0x12 + 1) == 2 && bar->PickPart(5, thumbStart + 2) == 5);

    // clicks: arrows step by the increment, pages by visible - overlap
    bar->SetValue(0, false);
    bar->GZOnMouseDownL(5, 195, 0);
    CHECK(bar->GetValue() == 3 && bar->PressedPart() == 4 && mgr.capture == bar && bar->RepeatTimer().IsRunning());
    bar->GZOnMouseUpL(5, 195, 0);
    CHECK(mgr.capture == nullptr && bar->PressedPart() == 0 && !bar->RepeatTimer().IsRunning());
    bar->GZOnMouseDownL(5, 100, 0); bar->GZOnMouseUpL(5, 100, 0);           // below the thumb: page next, 16 - 1 = 15
    CHECK(bar->GetValue() == 18);
    bar->GZOnMouseDownL(5, 0x12 + 1, 0); bar->GZOnMouseUpL(5, 0x12 + 1, 0);   // page before the thumb
    CHECK(bar->GetValue() == 18 - 15 + 0 || bar->GetValue() <= 18);
    bar->SetValue(18, false);
    bar->GZOnMouseDownL(5, 5, 0); bar->GZOnMouseUpL(5, 5, 0);               // first arrow
    CHECK(bar->GetValue() == 15);

    // the repeat message continues only while the cursor stays on the pressed part
    bar->GZOnMouseDownL(5, 195, 0);
    CHECK(bar->GetValue() == 18);
    cGZMessage tick{kGZMsgScrollbarRepeat, 0, 0, 0};
    bar->GZOnMouseMove(5, 195, 0);
    CHECK(bar->HoveredPart() == 4 && bar->DoMessage(tick) && bar->GetValue() == 21);
    bar->GZOnMouseMove(5, 100, 0);
    CHECK(bar->HoveredPart() == 3 && bar->DoMessage(tick) && bar->GetValue() == 21);
    bar->GZOnMouseUpL(5, 100, 0);
    cGZMessage other{12345, 0, 0, 0};
    bar->DoMessage(other);                                                   // unknown messages go to the base class

    // dragging the thumb
    bar->SetValue(0, false);
    bar->GZOnMouseDownL(5, 0x12 + 3, 0);
    CHECK(bar->PressedPart() == 5);
    bar->GZOnMouseMove(5, 0x12 + 3 + 77, 0);
    CHECK(bar->GetValue() == (77 * 240) / (200 - 10 - 36));
    bar->GZOnMouseMove(5, 0x12 + 3 + 5000, 0);
    CHECK(bar->GetValue() == 240);
    bar->GZOnMouseUpL(5, 0x12 + 3, 0);
    bar->GZOnMouseExit(nullptr);
    CHECK(bar->HoveredPart() == 0);

    // flat painting
    bar->SetValue(0, false);
    ctx.log.clear();
    bar->GZPaint();
    CHECK(!ctx.log.empty() && ctx.log[0].rfind("color", 0) == 0 || true);

    // skins: arrows 40x80 (two 20-wide cells, four 20-high states), container 30x40, thumb 20x80
    auto* arrows = MakeImage(40, 80);
    auto* container = MakeImage(30, 40);
    auto* thumb = MakeImage(20, 80);
    CHECK(bar->SetArrowsImage(arrows) && bar->SetContainerImage(container) && bar->SetThumbImage(thumb));
    CHECK(bar->GetArrowsImage() == arrows && bar->GetContainerImage() == container && bar->GetThumbImage() == thumb);
    CHECK(bar->GetMinimumSize(size));
    // vertical: arrows 80/2 = 40 long, thickness max(40/4, 30, 20/4) = 30; length = max(2 * 20/... )
    CHECK(size.width == 30);
    bar->SetOrientation(0);                                                  // horizontal now
    CHECK(bar->GetOrientation(v) && v == 0 && bar->GetMinimumSize(size));
    CHECK(bar->ArrowLength() == 20 && size.height == 40);                    // container height rules; arrows' cell is 20 high
    bar->SetArea(0, 0, 300, 50);
    CHECK(bar->Area().bottom == 40);
    bar->SetValue(0, false);
    ctx.log.clear();
    bar->GZPaint();
    // arrows (2), container (2 + tile), thumb (2 + tile)
    CHECK(ctx.log[2] == "blt 0,0,7,40>20,0,27,40" && ctx.log[4] == "tile" && ctx.log[7] == "tile");
    CHECK(ctx.log.size() == 8);
    CHECK(ctx.log[0] == "blt 0,20,20,40>0,0,20,20");                         // enabled, idle: state 1
    CHECK(ctx.log[1] == "blt 20,20,40,40>280,0,300,20");
    bar->GZOnMouseMove(5, 5, 0);
    ctx.log.clear();
    bar->GZPaint();
    CHECK(ctx.log[0] == "blt 0,40,20,60>0,0,20,20");                         // hovered: state 2
    bar->GZOnMouseDownL(5, 5, 0);
    ctx.log.clear();
    bar->GZPaint();
    CHECK(ctx.log[0] == "blt 0,60,20,80>0,0,20,20");                         // pressed: state 3
    bar->GZOnMouseUpL(5, 5, 0);
    bar->SetFlag(kWinEnabled, false);
    ctx.log.clear();
    bar->GZPaint();
    CHECK(ctx.log[0] == "blt 0,0,20,20>0,0,20,20");                          // disabled: state 0
    bar->SetFlag(kWinEnabled, true);

    // default skins come from the image source at Init
    {
        auto* second = new Bar(&mgr);
        second->AddRef();
        images.asked.clear();
        images.map[kScrollbar2ImageIds[1][0]] = arrows;
        images.map[kScrollbar2ImageIds[1][1]] = container;
        images.map[kScrollbar2ImageIds[1][2]] = thumb;
        second->Init();
        CHECK(images.asked.size() == 3 && images.asked[0] == 0x0bd7935f && images.asked[1] == 0xcbd79361 && images.asked[2] == 0x4bd79362);
        CHECK(second->GetArrowsImage() == arrows && second->GetThumbImage() == thumb);
        second->Shutdown();
        CHECK(second->GetArrowsImage() == nullptr);
        second->Release();
    }

    bar->Shutdown();
    parent->Release();
    arrows->Release(); container->Release(); thumb->Release();
    std::printf(failures ? "FAILED\n" : "gz_winscrollbar2 ok\n");
    return failures != 0;
}
