#include <cstdio>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/WinScrollbar.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Probe final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct Bar final : cGZWinScrollbar {
    using cGZWinScrollbar::cGZWinScrollbar;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
    void SetFill(std::uint32_t rgb) { fillColor_ = rgb; }
    int Pressed() const { return pressed_; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; fake_dc::Ctx ctx;
    SetWinGraphics(&graphics);
    auto* parent = new Probe(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 300, 300);
    auto* bar = new Bar(&mgr);
    parent->ChildAdd(bar);
    bar->SetCtx(&ctx);

    void* out = nullptr;
    CHECK(bar->QueryInterface(kGZIID_cIGZWinScrollbar, &out) && out == static_cast<cIGZWinScrollbar*>(bar));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(bar->QueryInterface(kGZIID_cIGZWinSlider, &out) && out == static_cast<cIGZWinSlider*>(bar));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(bar->QueryInterface(kGZIID_cIGZWin, &out) && out == static_cast<cGZWin*>(bar));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!bar->QueryInterface(0x1234, &out) && bar->AsIGZWinSlider() == static_cast<cIGZWinSlider*>(bar));

    // vertical, 32 wide and 96 tall, thumb 32x32, one button at each end
    bar->SetArea(0, 0, 32, 96);
    CHECK(bar->GetOrientation() == 2 && bar->SetMinMax(0, 100));
    int low, high;
    CHECK(bar->GetMinMax(low, high) && low == 0 && high == 100);
    std::uint32_t lines = 0, pages = 0;
    bar->GetLineAndPageCount(lines, pages);
    CHECK(lines == 32 && pages == 32);                                    // from the construction-time range
    bar->SetLineAndPageCount(10, 5);
    bar->GetLineAndPageCount(lines, pages);
    CHECK(lines == 10 && pages == 5 && bar->GetLineSize() == 10 && bar->GetPageSize() == 101 / 5);
    bar->SetLineAndPageCount(0, 0);
    bar->GetLineAndPageCount(lines, pages);
    CHECK(lines == 100 && pages == 1);
    CHECK(bar->SetLineSize(10) && bar->SetPageSize(25) && !bar->SetLineSize(0) && !bar->SetPageSize(0));
    bar->GetLineAndPageCount(lines, pages);
    CHECK(lines == 10 && pages == 4);

    // metrics: the track runs between the two buttons
    CHECK(bar->GetPrevNextButtonHeight() == 32 && bar->GetPrevNextButtonWidth() == 32);
    CHECK(bar->ThumbRect().top == 32 && bar->ThumbRect().bottom == 64);   // value 0: just below the previous button
    bar->SetValue(100);
    CHECK(bar->ThumbRect().top == 32 && bar->ThumbRect().bottom == 64 + 0 && bar->GetValue() == 100);   // track 32..32: no travel
    bar->SetArea(0, 0, 32, 200);
    CHECK(bar->ThumbRect().top == 200 - 32 - 32);                        // value 100: just above the next button
    bar->SetValue(0);

    // hit test
    tScrollHitTestResult part;
    CHECK(bar->DoCursorPositionHitTest(part, 5, 10) && part == kScrollPreviousLine);
    CHECK(bar->DoCursorPositionHitTest(part, 5, 190) && part == kScrollNextLine);
    CHECK(bar->DoCursorPositionHitTest(part, 5, 50) && part == kScrollThumb);
    bar->SetValue(50);
    const int thumbTop = bar->ThumbRect().top;
    CHECK(bar->DoCursorPositionHitTest(part, 5, 40) && part == kScrollPreviousPage);
    CHECK(bar->DoCursorPositionHitTest(part, 5, thumbTop + 1) && part == kScrollThumb);
    CHECK(bar->DoCursorPositionHitTest(part, 5, thumbTop + 33) && part == kScrollNextPage);
    CHECK(!bar->DoCursorPositionHitTest(part, 500, 10) && part == kScrollNothing);
    bar->SetValue(0);

    // clicks scroll by lines and pages
    parent->got.clear();
    CHECK(bar->GZOnMouseDownL(5, 190, 0) && bar->GetValue() == 10 && bar->Pressed() == kScrollNextLine && mgr.capture == bar);
    CHECK(!parent->got.empty() && parent->got.back().param1 == kGZSliderValueChanged);
    bar->GZOnMouseUpL(5, 190, 0);
    CHECK(mgr.capture == nullptr && bar->Pressed() == kScrollNothing);
    bar->GZOnMouseDownL(5, 10, 0); bar->GZOnMouseUpL(5, 10, 0);
    CHECK(bar->GetValue() == 0);
    bar->GZOnMouseDownL(5, 150, 0); bar->GZOnMouseUpL(5, 150, 0);          // page next: (100 + 1 - 0) / 4 = 25
    CHECK(bar->GetValue() == 25);
    bar->GZOnMouseDownL(5, 150, 0); bar->GZOnMouseUpL(5, 150, 0);
    CHECK(bar->GetValue() == 50);
    bar->MovePagePrevious(); CHECK(bar->GetValue() == 25);
    bar->MoveLineNext(); CHECK(bar->GetValue() == 35);
    bar->MoveLinePrevious(); bar->MoveLinePrevious();
    CHECK(bar->GetValue() == 15);

    // dragging the thumb
    bar->SetValue(0);
    const int thumbTop0 = bar->ThumbRect().top;
    bar->GZOnMouseDownL(5, thumbTop0 + 4, 0);
    CHECK(bar->Pressed() == kScrollThumb && mgr.capture == bar);
    bar->GZOnMouseMove(5, 100, 0);
    CHECK(bar->GetValue() > 0);
    bar->GZOnMouseUpL(5, 100, 0);
    CHECK(bar->Pressed() == kScrollNothing);

    // button repeat: held buttons scroll again once 200 ms have passed, and only while the cursor stays on them
    bar->SetValue(0);
    bar->GZOnMouseDownL(5, 190, 0);
    CHECK(bar->GetValue() == 10);
    bar->GZPaint();                                                       // nothing yet: the timer was just restarted
    CHECK(bar->GetValue() == 10);
    bar->GZOnMouseMove(5, 100, 0);                                        // cursor left the button
    bar->GZPaint();
    CHECK(bar->GetValue() == 10);
    bar->GZOnMouseMove(5, 190, 0);                                        // and came back
    bar->GZOnMouseUpL(5, 190, 0);

    // the line / page model
    bar->SetLineAndPageCount(11, 5);
    bar->SetValue(50);
    CHECK(bar->GetCurrentLine() == 5 && bar->GetValueOfGivenLine(5) == 50 && bar->GetValueOfGivenLine(0) == 0);
    CHECK(bar->GetCurrentPage() == 2 && bar->GetValueOfGivenPage(4) == 100);
    bar->SetLineAndPageCount(1, 1);
    CHECK(bar->GetCurrentLine() == 0 && bar->GetValueOfGivenLine(7) == 0 && bar->GetCurrentPage() == 0 && bar->GetValueOfGivenPage(3) == 0);

    // orientation and limits
    CHECK(bar->SetOrientation(1) && bar->GetOrientation() == 1 && bar->SetOrientation(9) && bar->GetOrientation() == 0 && bar->SetOrientation(2));
    cRZRect limits;
    CHECK(bar->GetMinMaxInfo(limits) && limits.left == 32 && limits.top == 96 && limits.right == 32 && limits.bottom == 0xffff);
    bar->SetOrientation(1);
    CHECK(bar->GetMinMaxInfo(limits) && limits.left == 600 && limits.top == 200 && limits.right == 0xffff && limits.bottom == 200);
    bar->SetOrientation(7);
    CHECK(!bar->GetMinMaxInfo(limits));
    bar->SetOrientation(2);
    bar->SetLineAndPageCount(10, 4);

    // flat painting
    bar->SetFill(0x00123456);
    ctx.log.clear();
    bar->SetValue(0);
    bar->GZPaint();
    CHECK(ctx.log.size() == 7);
    CHECK(ctx.log[0] == "color " + std::to_string(0x00123456) && ctx.log[1] == "fill 0,0,32,32");
    CHECK(ctx.log[2] == "color " + std::to_string(~0x00123456u) && ctx.log[3] == "fill 0,32,32," + std::to_string(200 - 32));
    CHECK(ctx.log[5] == "fill 0," + std::to_string(200 - 32) + ",32,200");
    CHECK(ctx.log[6] == "fill 0,32,32,64");                                // the thumb

    // image painting
    auto* image = new cGZBuffer(); image->AddRef();
    image->Init(12 * 20, 20, cGZBufferColorType{kBufferColorRGB565, 16});
    CHECK(bar->SetImage(image, nullptr));
    cRZRect cell; cIGZBuffer* got = nullptr; int flags = 7;
    CHECK(bar->GetImage(cell, got, flags) && got == image && flags == 0);
    CHECK(cell.left == 6 * 20 && cell.right == 7 * 20 && cell.top == 0 && cell.bottom == 20 && bar->GetW() == 20);   // vertical bars use the second half
    bar->SetValue(0);
    ctx.log.clear();
    bar->GZPaint();
    CHECK(ctx.log.size() == 4);
    CHECK(ctx.log[0] == "blt 120,0,140,20>0,0,20,20");                    // previous button
    CHECK(ctx.log[1] == "tile");
    CHECK(ctx.log[2] == "blt 200,0,220,20>0,180,20,200");                 // next button (cell 4)
    CHECK(ctx.log[3] == "blt 160,0,180,20>0,20,20,40");                   // thumb (cell 2) right under the previous button
    bar->GZOnMouseDownL(5, 190, 0);
    ctx.log.clear();
    bar->GZPaint();
    CHECK(ctx.log[2] == "blt 220,0,240,20>0,180,20,200");                 // pressed next button (cell 5)
    bar->GZOnMouseUpL(5, 190, 0);
    CHECK(bar->SetImage(nullptr, nullptr));                               // no image: flat again
    cRZRect area{0, 0, 120, 10};
    CHECK(bar->ImageSetup(area, image, 0));
    bar->SetOrientation(1);
    CHECK(bar->SetImage(image, &area) && bar->GetH() == 10);

    image->Release();
    bar->Shutdown();
    parent->Release();
    std::printf(failures ? "FAILED\n" : "gz_winscrollbar ok\n");
    return failures != 0;
}
