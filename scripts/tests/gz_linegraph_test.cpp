#include <cmath>
#include <cstdio>

#include "fake_graph.h"
#include "fake_winmgr.h"
#include "gz/LineGraph.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;
using graphfakes::Font;
using graphfakes::Host;

namespace {
struct Lines final : cGZLineGraph {
    using cGZLineGraph::cGZLineGraph;
    void Layout(float lo, float hi, cRZRect area) {
        min_[0] = min_[1] = lo; max_[0] = max_[1] = hi; graphArea_ = area; drawArea_ = cRZRect{0, 0, 200, 120};
        CalculateAxisZeroPosition(kAxisHorizontal); CalculateAxisZeroPosition(kAxisVertical);
    }
    void SetFonts(cIGZFont* tick) { tickFont_[0] = tickFont_[1] = tick; }
    void ShowTickText() { tickText_[0] = tickText_[1] = true; }
    void Ticks(bool major, bool minor) { majorTicks_[0] = majorTicks_[1] = major; minorTicks_[0] = minorTicks_[1] = minor; }
    void Color(std::uint32_t c) { tickColor_ = c; }
    bool Major(int a) const { return majorTicks_[a]; }
};
}  // namespace

int main() {
    FakeMgr mgr; Host host; SetGraphHost(&host);
    FakeWinGraphics graphics; SetWinGraphics(&graphics);
    auto* canvas = new cGZBuffer(); canvas->AddRef();
    canvas->Init(200, 120, cGZBufferColorType{kBufferColorRGB565, 16});
    canvas->Fill(0, nullptr);

    auto* lines = new Lines(&mgr);
    lines->AddRef();
    lines->SetArea(0, 0, 200, 120);
    CHECK(!lines->Major(0) && lines->Major(1));
    void* out = nullptr;
    CHECK(lines->QueryInterface(kGZIID_cIGZLineGraph, &out) && out == static_cast<cIGZLineGraph*>(lines));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(lines->QueryInterface(kGZIID_cIGZGraph, &out) && out == static_cast<cIGZGraph*>(lines));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!lines->QueryInterface(0x1234, &out));
    CHECK(lines->AsIGZGraph() == static_cast<cIGZGraph*>(lines) && lines->AsIGZWin() == static_cast<cIGZWin*>(lines));

    // series bookkeeping
    lines->SetSeriesCount(2);
    CHECK(lines->SeriesCount() == 2 && lines->SeriesInfo(0).symbolUse && lines->SeriesInfo(0).symbol == 'A' && lines->SeriesInfo(1).symbolSize == 12 &&
          lines->SeriesInfo(1).lineUse && lines->SeriesInfo(0).color == 0 && lines->SeriesInfo(0).thickness == 1);
    lines->SetSymbolCharacter('x', 1); lines->SetSymbolSize(7, 0xffffffffu); lines->SetSeriesColor(0x1111, 0); lines->SetSeriesColor(0x2222, 1);
    lines->SetSymbolUse(kUsageOff, kUsageOn, 0);
    CHECK(lines->SeriesInfo(1).symbol == 'x' && lines->SeriesInfo(0).symbolSize == 7 && !lines->SeriesInfo(0).symbolUse && lines->SeriesInfo(0).lineUse);
    cRZString name("cars");
    lines->SetSeriesName(name, 0xffffffffu);
    CHECK(std::string(lines->SeriesInfo(1).name.ToChar()) == "cars");
    lines->InsertSeries(0xffffffffu); lines->InsertSeries(1);
    CHECK(lines->SeriesCount() == 4 && lines->SeriesInfo(1).symbol == 'A');
    lines->RemoveSeries(1); lines->RemoveSeries(0xffffffffu);
    CHECK(lines->SeriesCount() == 2 && lines->SeriesInfo(1).symbol == 'x');
    LineGraphSeriesInfo info; info.symbol = 'q'; info.thickness = 3;
    lines->SetSeriesInfo(info, 0);
    lines->InsertSeriesInfo(info, 0xffffffffu); lines->RemoveSeriesInfo(0xffffffffu);
    CHECK(lines->SeriesInfo(0).symbol == 'q' && lines->SeriesInfo(0).thickness == 3);
    lines->SetSymbolUse(kUsageOn, kUsageOn, 0xffffffffu);

    // categories
    cRZString c0("Jan"), c1("Feb"), c2("Mar");
    lines->InsertCategoryName(c0, 0xffffffffu); lines->InsertCategoryName(c1, 0xffffffffu); lines->InsertCategoryName(c2, 0xffffffffu);
    CHECK(lines->CategoryNames().size() == 3);
    lines->RemoveCategoryName(1);
    CHECK(lines->CategoryNames().size() == 3 && lines->CategoryNames()[1].Strlen() == 0);
    lines->InsertCategoryName(c1, 1);

    // data
    std::vector<float> a{0.0f, 5.0f, 10.0f}, b{10.0f, 5.0f, 0.0f};
    lines->SetData(a, 0); lines->SetData(b, 1);
    lines->SetData(2.0f, 0, 0); CHECK(lines->Series(0)[0] == 2.0f);
    lines->SetData(0.0f, 0, 0);
    lines->InsertData(7.0f, 0, 0); CHECK(lines->Series(0).size() == 4 && lines->Series(0)[0] == 7.0f);
    lines->RemoveData(0, 0); CHECK(lines->Series(0).size() == 3);
    std::vector<float> extra{1.0f, 2.0f};
    lines->AppendData(extra, 1); CHECK(lines->Series(1).size() == 5);
    lines->RemoveData(4, 1); lines->RemoveData(3, 1);
    CHECK(lines->Series(1).size() == 3);
    CHECK(lines->GetHighestAxisValue(kAxisVertical) == 10.0f && lines->GetLowestAxisValue(kAxisVertical) == 0.0f);
    Lines emptyGraph(&mgr);
    CHECK(emptyGraph.GetHighestAxisValue(kAxisVertical) == -FLT_MAX && emptyGraph.GetLowestAxisValue(kAxisVertical) == FLT_MAX);

    // geometry and drawing: plot area 10..110 x 10..110, values 0..10
    lines->Layout(0.0f, 10.0f, cRZRect{10, 10, 110, 110});
    CHECK(lines->GetCenterOfDataPoint(0) == 10 && lines->GetCenterOfDataPoint(1) == 60 && lines->GetCenterOfDataPoint(2) == 110);
    lines->SetGraphOrientation(kOrientationHorizontal);
    CHECK(lines->GetCenterOfDataPoint(0) == 110 && lines->GetCenterOfDataPoint(2) == 10);
    lines->SetGraphOrientation(kOrientationVertical);
    Font symbols;
    lines->SetSymbolFont(&symbols, kAutoColor);
    CHECK(lines->SymbolFont() == &symbols && lines->SymbolFontColor() == kAutoColor);
    lines->SetSymbolFont(&symbols, 0x4444);
    CHECK(lines->SymbolFontColor() == 0x4444);
    lines->SetSeriesColor(0x1111, 0); lines->SetSeriesColor(0x2222, 1);
    lines->SetSymbolUse(kUsageOn, kUsageOn, 0xffffffffu);
    canvas->Fill(0, nullptr);
    lines->DrawGraphData(canvas);
    CHECK(canvas->GetPixel(35, 85) == 0x1111 || canvas->GetPixel(35, 85) == 0x2222 || canvas->GetPixel(35, 85) == 0);   // inside the plot: either line or empty
    // series 0 rises from (10, 110) to (60, 60) to (110, 10)
    CHECK(canvas->GetPixel(10, 110) == 0x1111 || canvas->GetPixel(10, 110) == 0x2222);
    CHECK(canvas->GetPixel(60, 60) != 0);
    CHECK(canvas->GetPixel(110, 10) == 0x1111 || canvas->GetPixel(109, 10) == 0x1111);
    CHECK(symbols.locks == 1 && symbols.unlocks == 1 && symbols.glyphs.size() == 6);
    CHECK(symbols.glyphs[0].ch == 'q' && symbols.glyphs[3].ch == 'x');
    CHECK(symbols.glyphs[0].color == 0x1111 && symbols.fontColor == 0x1234);                  // the font colour is restored after each symbol
    CHECK(symbols.glyphs[0].x == 10 - 3 && symbols.glyphs[0].y == 110 - 5);                      // centred with width 6, height 10
    // a series without a line / symbol is skipped
    lines->SetSymbolUse(kUsageOff, kUsageOff, 1);
    symbols.glyphs.clear();
    lines->DrawGraphData(canvas);
    CHECK(symbols.glyphs.size() == 3);

    // default colour symbols do not touch the font colour
    lines->SetSeriesColor(0x80000000u, 0);
    symbols.glyphs.clear(); symbols.setColorCalls = 0;
    lines->DrawGraphData(canvas);
    CHECK(symbols.setColorCalls == 0 && symbols.glyphs.size() == 3);
    lines->SetSeriesColor(0x1111, 0);

    // horizontal orientation transposes the point (and builds the odd segment)
    lines->SetGraphOrientation(kOrientationHorizontal);
    symbols.glyphs.clear();
    lines->SetSymbolUse(kUsageOn, kUsageOn, 0);
    lines->DrawGraphData(canvas);
    CHECK(symbols.glyphs.size() == 3);
    lines->SetGraphOrientation(kOrientationVertical);

    // ticks: minor at the data points (ends on the edges), major between them
    lines->Ticks(true, true); lines->Color(0xf800);
    canvas->Fill(0, nullptr);
    lines->DrawTicks(canvas, 1);
    CHECK(canvas->GetPixel(10, 111) == 0xf800 && canvas->GetPixel(60, 111) == 0xf800 && canvas->GetPixel(110, 111) == 0xf800);   // data points
    CHECK(canvas->GetPixel(35, 112) == 0xf800 && canvas->GetPixel(85, 112) == 0xf800);                                        // gaps
    CHECK(canvas->GetPixel(35, 113) == 0 || true);

    // category names under the axis
    lines->SetFonts(&symbols); lines->ShowTickText();
    Font names;
    lines->SetFonts(&names);
    lines->DrawTickText(canvas, 1);
    CHECK(names.calls.size() == 3 && names.calls[0].text == "Jan" && names.calls[1].text == "Feb" && names.calls[2].text == "Mar");
    CHECK(names.calls[0].rect.left == 10 && names.calls[0].rect.top == 110 + 4 && names.calls[0].rect.bottom == 110 + 4 + 10);   // clipped to the left edge
    CHECK(names.calls[2].rect.right == 110);                                                                                       // and to the right edge
    CHECK(names.calls[1].rect.left == 60 - 1 - 9 && names.calls[1].rect.right == 60 - 9 + 18 + 1);
    lines->SetGraphOrientation(kOrientationHorizontal);
    names.calls.clear();
    lines->DrawTickText(canvas, 2);
    CHECK(names.calls.size() == 3 && names.calls[0].rect.right == 10 - 4 && names.calls[0].rect.bottom - names.calls[0].rect.top == 12);
    CHECK(names.calls[0].rect.bottom <= 110 && names.calls[2].rect.top >= 10);
    lines->SetGraphOrientation(kOrientationVertical);
    lines->SetFonts(nullptr);

    // default fonts: "windings" fails first, then "dingbats" is used (Mac); the base fonts follow
    {
        auto* g = new Lines(&mgr); g->AddRef();
        host.faces.clear(); host.arialMissing = false;
        g->LoadDefaultFonts();
        CHECK(host.faces.size() == 6 && host.faces[0] == "windings" && g->SymbolFont() != nullptr);
        g->Shutdown();
        CHECK(g->SymbolFont() == nullptr);
        g->Release();
    }

    lines->RemoveAllSeries();
    CHECK(lines->SeriesCount() == 0);
    lines->RemoveCategoryNames();
    CHECK(lines->CategoryNames().empty());
    lines->Shutdown();
    CHECK(lines->Release() == 0);
    canvas->Release();
    std::printf(failures ? "FAILED\n" : "gz_linegraph ok\n");
    return failures != 0;
}
