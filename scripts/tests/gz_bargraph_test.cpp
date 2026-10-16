#include <cmath>
#include <cstdio>

#include "fake_graph.h"
#include "fake_winmgr.h"
#include "gz/BarGraph.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;
using graphfakes::Font;
using graphfakes::Host;

namespace {
struct Bars final : cGZBarGraph {
    using cGZBarGraph::cGZBarGraph;
    void Layout(float lo, float hi, cRZRect area) {
        min_[0] = min_[1] = lo; max_[0] = max_[1] = hi; graphArea_ = area; drawArea_ = cRZRect{0, 0, 200, 120};
        CalculateAxisZeroPosition(kAxisHorizontal); CalculateAxisZeroPosition(kAxisVertical);
    }
    int zero(int a) const { return zeroPosition_[a]; }
    void SetFonts(cIGZFont* tick) { tickFont_[0] = tickFont_[1] = tick; }
    void ShowTickText() { tickText_[0] = tickText_[1] = true; }
    void NoTickText() { tickText_[0] = tickText_[1] = false; }
    void Ticks(bool major, bool minor) { majorTicks_[0] = majorTicks_[1] = major; minorTicks_[0] = minorTicks_[1] = minor; }
    void Grid(bool on) { majorGrid_[0] = majorGrid_[1] = on; }
    void Color(std::uint32_t c) { tickColor_ = c; }
    bool Major(int a) const { return majorTicks_[a]; }
    bool Text(int a) const { return tickText_[a]; }
    bool GridOf(int a) const { return majorGrid_[a]; }
};
}  // namespace

int main() {
    FakeMgr mgr; Host host; SetGraphHost(&host);
    FakeWinGraphics graphics; SetWinGraphics(&graphics);
    auto* canvas = new cGZBuffer(); canvas->AddRef();
    canvas->Init(200, 120, cGZBufferColorType{kBufferColorRGB565, 16});
    canvas->Fill(0, nullptr);

    auto* bars = new Bars(&mgr);
    bars->AddRef();
    bars->SetArea(0, 0, 200, 120);
    CHECK(!bars->Major(0) && bars->Major(1) && bars->Text(0) && bars->Text(1) && !bars->GridOf(0) && bars->GridOf(1));
    CHECK(bars->WidthFraction() == 0.75f && bars->BarWidth() == 1.0f && bars->BarSpacing() == 1.0f);
    void* out = nullptr;
    CHECK(bars->QueryInterface(kGZIID_cIGZBarGraph, &out) && out == static_cast<cIGZBarGraph*>(bars));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(bars->QueryInterface(kGZIID_cIGZGraph, &out) && out == static_cast<cIGZGraph*>(bars));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(bars->QueryInterface(kGZIID_cIGZWin, &out) && out == static_cast<cIGZWin*>(bars));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!bars->QueryInterface(0x1234, &out));
    CHECK(bars->AsIGZGraph() == static_cast<cIGZGraph*>(bars) && bars->AsIGZWin() == static_cast<cIGZWin*>(bars));

    // series bookkeeping
    bars->SetSeriesCount(3);
    CHECK(bars->SeriesCount() == 3 && bars->SeriesInfo(0).fill && bars->SeriesInfo(0).fillColor == 0xffffffffu && bars->SeriesInfo(2).outline);
    cRZString names("alpha");
    bars->SetSeriesName(names, 0xffffffffu);
    CHECK(std::string(bars->SeriesInfo(1).name.ToChar()) == "alpha");
    cRZString one("one");
    bars->SetSeriesName(one, 1);
    CHECK(std::string(bars->SeriesInfo(1).name.ToChar()) == "one" && std::string(bars->SeriesInfo(0).name.ToChar()) == "alpha");
    bars->SetSeriesName(one, 9);                                         // out of range: ignored
    bars->SetBarFillColor(0x1111, 0); bars->SetBarOutlineColor(0x2222, 0xffffffffu);
    bars->SetBarColorUse(kUsageOff, kUsageOn, 2);
    CHECK(bars->SeriesInfo(0).fillColor == 0x1111 && bars->SeriesInfo(1).outlineColor == 0x2222 && !bars->SeriesInfo(2).outline && bars->SeriesInfo(2).fill);
    bars->InsertSeries(0xffffffffu);
    CHECK(bars->SeriesCount() == 4);
    bars->InsertSeries(1);
    CHECK(bars->SeriesCount() == 5 && bars->SeriesInfo(0).fillColor == 0x1111);
    bars->InsertSeries(9);                                               // beyond the end: ignored
    CHECK(bars->SeriesCount() == 5);
    bars->RemoveSeries(1); bars->RemoveSeries(0xffffffffu);
    CHECK(bars->SeriesCount() == 3);
    BarGraphSeriesInfo info; info.fillColor = 0x3333; info.name.AssignFromCString("custom");
    bars->SetSeriesInfo(info, 1);
    CHECK(bars->SeriesInfo(1).fillColor == 0x3333 && std::string(bars->SeriesInfo(1).name.ToChar()) == "custom");
    bars->InsertSeriesInfo(info, 0xffffffffu);
    bars->RemoveSeriesInfo(0xffffffffu);
    bars->SetBarWidthPercentage(5.0f); CHECK(bars->WidthFraction() == 1.0f);
    bars->SetBarWidthPercentage(0.01f); CHECK(bars->WidthFraction() == 0.1f);
    bars->SetBarWidthPercentage(0.75f);
    auto* pattern = new cGZBuffer(); pattern->AddRef(); pattern->Init(2, 2, cGZBufferColorType{kBufferColorRGB565, 16}); pattern->Fill(0x0abc, nullptr);
    bars->SetBarPattern(pattern, 2);
    CHECK(pattern->AddRef() == 3); pattern->Release();                   // one reference held by the series
    bars->SetBarPattern(nullptr, 2);
    CHECK(pattern->AddRef() == 2); pattern->Release();

    // categories
    cRZString c0("Q1"), c1("Q2");
    bars->InsertCategoryName(c0, 0xffffffffu); bars->InsertCategoryName(c1, 0xffffffffu);
    CHECK(bars->CategoryNames().size() == 2 && std::string(bars->CategoryNames()[1].ToChar()) == "Q2");
    bars->InsertCategoryName(c0, 1);                                     // overwrites
    CHECK(std::string(bars->CategoryNames()[1].ToChar()) == "Q1");
    bars->InsertCategoryName(c1, 1);
    bars->RemoveCategoryName(0);                                         // only clears the text
    CHECK(bars->CategoryNames().size() == 2 && bars->CategoryNames()[0].Strlen() == 0);

    // data: 3 series x 2 categories
    std::vector<float> a{4.0f, 8.0f}, b{2.0f, 6.0f}, c{-2.0f, 3.0f};
    bars->SetData(a, 0); bars->SetData(b, 1); bars->SetData(c, 2);
    CHECK(bars->Series(1)[1] == 6.0f);
    bars->SetData(9.0f, 0, 0); CHECK(bars->Series(0)[0] == 9.0f);
    bars->SetData(4.0f, 0, 0);
    bars->InsertData(1.5f, 0, 0); CHECK(bars->Series(0).size() == 3 && bars->Series(0)[0] == 1.5f);
    bars->RemoveData(0, 0); CHECK(bars->Series(0).size() == 2 && bars->Series(0)[0] == 4.0f);
    std::vector<float> extra{7.0f};
    bars->AppendData(extra, 1); bars->RemoveData(2, 1);
    CHECK(bars->Series(1).size() == 2);
    bars->SetData(1.0f, 0xffffffffu, 2); bars->RemoveData(2, 2);
    CHECK(bars->Series(2).size() == 2);

    // clustered geometry: 100 wide plot area, 3 series x 2 categories
    bars->Layout(0.0f, 10.0f, cRZRect{10, 10, 110, 110});
    bars->SetBarGraphType(kBarClustered);
    bars->CalculateBarWidthAndSpacing();
    CHECK(std::fabs(bars->BarWidth() - 12.5f) < 1e-4f && std::fabs(bars->BarSpacing() - 25.0f / 3.0f) < 1e-3f);
    CHECK(bars->CalculateMaximumBarWidth() == 25);                       // 75 / 3 series
    const int left00 = bars->GetLeftOfBar(0, 0), left21 = bars->GetLeftOfBar(1, 2);
    CHECK(left00 == 10 + 8 && left21 == static_cast<int>(2 * 8.3333333f + 10 + 1 * 12.5f * 3 + 12.5f * 2));
    CHECK(bars->GetRightOfBar(0, 0) == static_cast<int>(18.0f + 12.5f) && bars->GetCenterOfBar(0, 0) == static_cast<int>(18.0f + 6.25f));
    cRZRect rect;
    CHECK(bars->GetRectOfBar(0, 0, rect));                               // value 4: pixels 10 per unit
    CHECK(rect.left == 18 && rect.right == 30 && rect.top == 110 - 40 && rect.bottom == 110);
    CHECK(bars->GetRectOfBar(0, 2, rect) && rect.top == 110 && rect.bottom == 110 + 20);   // negative value hangs below zero
    CHECK(!bars->GetRectOfBar(5, 0, rect) && rect.right == 0 && !bars->GetRectOfBar(0, 7, rect));
    CHECK(bars->GetRectOfBarsInCategory(0, rect) && rect.left == 18 && rect.top == 70 - 0 - 0 + 0 - 0 + 0 - 0 + 0 && rect.bottom == 130);
    CHECK(!bars->GetRectOfBarsInCategory(9, rect));
    CHECK(bars->GetHighestAxisValue(kAxisVertical) == 8.0f && bars->GetLowestAxisValue(kAxisHorizontal) == -2.0f);

    // hit test
    cRZPoint inside{22, 100}, outside{5, 5};
    std::uint32_t result = 0;
    CHECK(bars->DoHitTest(inside, result) && result == kBarHitTestResult);
    const std::int32_t* hit = static_cast<const std::int32_t*>(bars->GetHitTestResultData());
    CHECK(hit[1] == 0 && hit[2] == 0);
    CHECK(!bars->DoHitTest(outside, result));

    // clustered drawing (a range with room below zero so the negative bar is visible)
    bars->Layout(-5.0f, 10.0f, cRZRect{10, 10, 110, 110});
    bars->SetBarGraphType(kBarClustered);
    bars->CalculateBarWidthAndSpacing();
    bars->SetBarFillColor(0x1111, 0); bars->SetBarFillColor(0x2222, 1); bars->SetBarFillColor(0x3333, 2);
    bars->SetBarColorUse(kUsageOff, kUsageOn, 0xffffffffu);
    canvas->Fill(0, nullptr);
    bars->DrawAllBars(canvas);
    const int zero = bars->zero(1);
    CHECK(zero == 10 + 66);                                              // 10 units at 6.67 pixels per unit below the top
    const int top0 = bars->ValueToPixel(4.0f, kAxisVertical, true);
    CHECK(canvas->GetPixel(24, static_cast<std::uint32_t>(zero - 2)) == 0x1111 && canvas->GetPixel(24, static_cast<std::uint32_t>(top0 + 1)) == 0x1111);
    CHECK(canvas->GetPixel(24, static_cast<std::uint32_t>(top0 - 2)) == 0);                // above the 4-unit bar
    CHECK(canvas->GetPixel(36, static_cast<std::uint32_t>(zero - 2)) == 0x2222);            // series 1
    CHECK(canvas->GetPixel(49, static_cast<std::uint32_t>(zero + 3)) == 0x3333);            // the negative bar hangs below zero
    CHECK(canvas->GetPixel(49, static_cast<std::uint32_t>(zero - 3)) == 0);
    // outline + pattern drawing path
    bars->SetBarColorUse(kUsageOn, kUsageOff, 1);
    bars->SetBarOutlineColor(0x00f0, 1);
    canvas->Fill(0, nullptr);
    bars->DrawAllBars(canvas);
    CHECK(canvas->GetPixel(30, static_cast<std::uint32_t>(zero - 2)) == 0x00f0 && canvas->GetPixel(36, static_cast<std::uint32_t>(zero - 2)) == 0);
    bars->SetBarPattern(pattern, 0);
    canvas->Fill(0, nullptr);
    bars->DrawAllBars(canvas);
    CHECK(canvas->GetPixel(24, static_cast<std::uint32_t>(zero - 2)) == 0x0abc);
    bars->SetBarPattern(nullptr, 0);

    // stacked
    bars->Layout(0.0f, 10.0f, cRZRect{10, 10, 110, 110});
    bars->SetBarGraphType(kBarStacked);
    bars->CalculateBarWidthAndSpacing();
    CHECK(std::fabs(bars->BarWidth() - 37.5f) < 1e-4f && std::fabs(bars->BarSpacing() - 25.0f / 3.0f) < 1e-3f);
    CHECK(bars->GetHighestAxisValue(kAxisVertical) == 17.0f && bars->GetLowestAxisValue(kAxisVertical) == -2.0f);
    canvas->Fill(0, nullptr);
    bars->SetBarColorUse(kUsageOff, kUsageOn, 0xffffffffu);
    bars->DrawAllBars(canvas);
    CHECK(canvas->GetPixel(20, 105) == 0x1111 && canvas->GetPixel(20, 75) == 0x1111);       // series 0: 4 units = y 70..110
    CHECK(canvas->GetPixel(20, 60) == 0x2222 && canvas->GetPixel(20, 40) == 0);              // series 1 stacked on top: y 50..70
    CHECK(canvas->GetPixel(20, 112) == 0x3333);                                              // the negative series under the axis
    CHECK(canvas->GetPixel(70, 100) == 0x1111 && canvas->GetPixel(70, 20) == 0x2222);        // second category: 8 + 6 + 3
    // stacked rectangles accumulate along the category axis of one series (sic)
    CHECK(bars->GetRectOfBar(1, 0, rect) && rect.top == 110 - 40 - 80 && rect.bottom == 110 - 40);

    // category ticks and names (vertical bars: along the bottom edge)
    bars->Ticks(true, true); bars->Grid(true); bars->Color(0xf800);
    canvas->Fill(0, nullptr);
    bars->DrawTicks(canvas, 1);
    const int tick1 = static_cast<int>(bars->BarSpacing() * -0.5f + static_cast<float>(bars->GetLeftOfBar(1, 0)));
    CHECK(canvas->GetPixel(static_cast<std::uint32_t>(tick1), 50) == 0xf800 && canvas->GetPixel(static_cast<std::uint32_t>(tick1), 112) == 0xf800);
    const int mid1 = static_cast<int>(bars->BarWidth() * 0.5f + static_cast<float>(bars->GetLeftOfBar(1, 0)));
    CHECK(canvas->GetPixel(static_cast<std::uint32_t>(mid1), 50) == 0 && canvas->GetPixel(static_cast<std::uint32_t>(mid1), 111) == 0xf800);   // minor ticks have no grid line
    Font font;
    bars->SetFonts(&font); bars->ShowTickText();
    bars->DrawTickText(canvas, 1);
    CHECK(font.calls.size() == 1 && font.calls[0].text == "Q2" && font.calls[0].align == 1);   // the cleared first name is skipped
    cRZRect nameRect;
    bars->GetRectOfBarsInCategory(1, nameRect);
    const int nameWidth = 2 * 6, centre = (nameRect.right - nameRect.left) / 2 + nameRect.left;
    CHECK(font.calls[0].rect.top == 110 + 4 && font.calls[0].rect.bottom == 110 + 4 + 10);
    CHECK(font.calls[0].rect.left == centre - nameWidth / 2 - 1 && font.calls[0].rect.right == centre - nameWidth / 2 + nameWidth + 1);
    // horizontal bars put the category names on the left
    bars->SetGraphOrientation(kOrientationHorizontal);
    bars->SetBarGraphType(kBarClustered);
    bars->CalculateBarWidthAndSpacing();
    font.calls.clear();
    bars->DrawTickText(canvas, 2);
    CHECK(font.calls.size() == 1 && font.calls[0].rect.right == 10 - 4 && font.calls[0].rect.left == 10 - 4 - nameWidth &&
          font.calls[0].rect.bottom - font.calls[0].rect.top == 11);
    canvas->Fill(0, nullptr);
    bars->DrawTicks(canvas, 2);                                          // draws without tripping over the stray-leftover quirk
    bars->SetFonts(nullptr);
    bars->SetGraphOrientation(kOrientationVertical);

    // a fresh graph with a stacked default, the empty cases and the tick helpers
    Bars empty(&mgr);
    CHECK(empty.GetHighestAxisValue(kAxisVertical) == 100.0f && empty.GetLowestAxisValue(kAxisVertical) == 100.0f);
    empty.SetBarGraphType(kBarClustered);
    CHECK(empty.GetHighestAxisValue(kAxisVertical) == -FLT_MAX && empty.GetLowestAxisValue(kAxisVertical) == FLT_MAX);
    empty.CalculateBarWidthAndSpacing();
    empty.DrawAllBars(canvas);                                           // nothing to draw
    CHECK(empty.GetLeftOfBar(0, 0) == static_cast<int>(empty.BarSpacing()));

    pattern->Release();
    bars->Shutdown();
    CHECK(bars->Release() == 0);
    canvas->Release();
    std::printf(failures ? "FAILED\n" : "gz_bargraph ok\n");
    return failures != 0;
}
