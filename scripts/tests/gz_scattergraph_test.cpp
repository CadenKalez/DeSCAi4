#include <cfloat>
#include <cmath>
#include <cstdio>

#include "fake_graph.h"
#include "fake_winmgr.h"
#include "gz/ScatterGraph.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;
using graphfakes::Font;
using graphfakes::Host;

namespace {
struct Scatter final : cGZScatterGraph {
    using cGZScatterGraph::cGZScatterGraph;
    void Layout(float lo, float hi, cRZRect area) {
        min_[0] = min_[1] = lo; max_[0] = max_[1] = hi; graphArea_ = area; drawArea_ = cRZRect{0, 0, 200, 120};
        CalculateAxisZeroPosition(kAxisHorizontal); CalculateAxisZeroPosition(kAxisVertical);
    }
    bool Text(int a) const { return tickText_[a]; }
};
}  // namespace

int main() {
    FakeMgr mgr; Host host; SetGraphHost(&host);
    FakeWinGraphics graphics; SetWinGraphics(&graphics);
    auto* canvas = new cGZBuffer(); canvas->AddRef();
    canvas->Init(200, 120, cGZBufferColorType{kBufferColorRGB565, 16});
    canvas->Fill(0, nullptr);

    auto* g = new Scatter(&mgr);
    g->AddRef();
    g->SetArea(0, 0, 200, 120);
    CHECK(g->Text(0) && g->Text(1));
    void* out = nullptr;
    CHECK(g->QueryInterface(kGZIID_cIGZScatterGraph, &out) && out == static_cast<cIGZScatterGraph*>(g));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(g->QueryInterface(kGZIID_cIGZGraph, &out) && out == static_cast<cIGZGraph*>(g));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!g->QueryInterface(0x1234, &out) && g->AsIGZGraph() == static_cast<cIGZGraph*>(g));

    g->SetSeriesCount(2);
    CHECK(g->SeriesCount() == 2 && g->SeriesInfo(0).symbol == 'A' && g->SeriesInfo(0).symbolSize == 12 && !g->SeriesInfo(1).drawLines);
    g->SetSymbolCharacter('o', 0xffffffffu); g->SetSymbolSize(5, 1); g->SetSeriesColor(0x1111, 0); g->SetSeriesColor(0x2222, 1);
    g->SetSeriesLineDrawing(true, 1);
    cRZString name("pts");
    g->SetSeriesName(name, 0);
    CHECK(g->SeriesInfo(1).symbol == 'o' && g->SeriesInfo(1).symbolSize == 5 && g->SeriesInfo(1).drawLines && std::string(g->SeriesInfo(0).name.ToChar()) == "pts");
    g->InsertSeries(0xffffffffu); g->InsertSeries(0);
    CHECK(g->SeriesCount() == 4 && g->SeriesInfo(0).symbol == 'A');
    g->RemoveSeries(0); g->RemoveSeries(0xffffffffu);
    CHECK(g->SeriesCount() == 2 && g->SeriesInfo(0).color == 0x1111);
    ScatterGraphSeriesInfo info; info.symbol = 'z'; info.drawLines = true;
    g->SetSeriesInfo(info, 0); g->InsertSeriesInfo(info, 0xffffffffu); g->RemoveSeriesInfo(0xffffffffu);
    CHECK(g->SeriesInfo(0).symbol == 'z' && g->SeriesInfo(0).drawLines);

    g->SetData(cRZ2DPointFloat32{1, 2}, 0xffffffffu, 0);
    g->InsertData(3.0f, 4.0f, 0, 0xffffffffu);
    g->InsertData(0.0f, 0.0f, 0, 0);
    CHECK(g->Series(0).size() == 3 && g->Series(0)[0].x == 0.0f && g->Series(0)[2].y == 4.0f);
    g->RemoveData(0, 0);
    g->SetData(9.0f, 8.0f, 0, 0);
    CHECK(g->Series(0).size() == 2 && g->Series(0)[0].x == 9.0f && g->Series(0)[0].y == 8.0f);
    g->SetData(1.0f, 1.0f, 0, 0);
    std::vector<cRZ2DPointFloat32> second{{2, 2}, {4, 4}, {6, 1}};
    g->SetData(second, 1);
    g->AppendData({{8, 8}}, 1);
    CHECK(g->Series(1).size() == 4);
    CHECK(g->GetHighestAxisValue(kAxisHorizontal) == 8.0f && g->GetHighestAxisValue(kAxisVertical) == 8.0f &&
          g->GetLowestAxisValue(kAxisHorizontal) == 1.0f && g->GetLowestAxisValue(kAxisVertical) == 1.0f);
    CHECK(g->GetHighestAxisValue(static_cast<Axis>(3)) == -FLT_MAX && g->GetLowestAxisValue(static_cast<Axis>(3)) == FLT_MAX);
    float v = -1;
    g->GetAutomaticAxisMaxValue(kAxisHorizontal, v);
    CHECK(std::fabs(v - 8.0f / 0.9f) < 1e-3f);
    g->GetAutomaticAxisMinValue(kAxisHorizontal, v);
    CHECK(v == 0.0f);                                                    // all values positive
    g->SetSharedRange(false);
    g->GetAutomaticAxisMinValue(kAxisVertical, v); CHECK(v == 0.0f);
    g->GetAutomaticAxisMaxValue(kAxisVertical, v); CHECK(std::fabs(v - 8.0f / 0.9f) < 1e-3f);
    g->SetSharedRange(true);
    g->InsertData(-3.0f, 0.5f, 1, 0xffffffffu);
    g->GetAutomaticAxisMinValue(kAxisHorizontal, v);
    CHECK(std::fabs(v - (-3.0f / 0.9f)) < 1e-3f);

    // the automatic plot area is square
    g->SetArea(0, 0, 200, 120);
    cRZRect area;
    g->GetAutomaticGraphAreaRect(area);
    CHECK(area.right - area.left == area.bottom - area.top);

    // drawing: values 0..10 over a 100 x 100 plot area
    g->Layout(0.0f, 10.0f, cRZRect{10, 10, 110, 110});
    Font symbols;
    g->SetSymbolFont(&symbols, 0x4444);
    CHECK(g->SymbolFont() == &symbols && g->SymbolFontColor() == 0x4444);
    g->SetData(std::vector<cRZ2DPointFloat32>{{0, 0}, {5, 5}, {10, 10}}, 0);
    g->SetData(std::vector<cRZ2DPointFloat32>{{0, 10}, {10, 10}, {10, 0}, {5, 5}}, 1);
    g->SetSeriesLineDrawing(true, 0); g->SetSeriesLineDrawing(false, 1);
    g->SetSeriesColor(0x1111, 0); g->SetSeriesColor(0x80000000u, 1);
    canvas->Fill(0, nullptr);
    g->DrawGraphData(canvas);
    CHECK(symbols.locks == 1 && symbols.unlocks == 1);
    CHECK(symbols.glyphs.size() == 6);                                   // three points per series: the first series' count rules
    CHECK(symbols.glyphs[0].x == 10 - 3 && symbols.glyphs[0].y == 110 - 5 && symbols.glyphs[0].ch == 'z');
    CHECK(symbols.glyphs[0].color == 0x1111 && symbols.glyphs[3].color == 0x1234);   // series 1 keeps the font colour
    CHECK(canvas->GetPixel(35, 85) == 0x1111 || canvas->GetPixel(36, 84) == 0x1111 || canvas->GetPixel(34, 86) == 0x1111);   // line between (10,110) and (60,60)
    CHECK(canvas->GetPixel(60, 60) == 0x1111);
    CHECK(canvas->GetPixel(35, 20) == 0);                                // series 1 draws no lines

    // fonts
    {
        auto* f = new Scatter(&mgr); f->AddRef();
        host.faces.clear(); host.arialMissing = false;
        f->LoadDefaultFonts();
        CHECK(host.faces.size() == 6 && host.faces[0] == "windings" && f->SymbolFont() != nullptr);
        f->Shutdown();
        CHECK(f->SymbolFont() == nullptr);
        f->DrawGraphData(canvas);                                        // no font: nothing to draw
        f->Release();
    }
    g->Shutdown();
    CHECK(g->Release() == 0);
    canvas->Release();
    std::printf(failures ? "FAILED\n" : "gz_scattergraph ok\n");
    return failures != 0;
}
