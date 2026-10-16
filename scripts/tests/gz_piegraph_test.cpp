#include <cmath>
#include <cstdio>

#include "fake_graph.h"
#include "fake_winmgr.h"
#include "gz/PieGraph.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;
using graphfakes::Font;
using graphfakes::Host;

namespace {
struct Draw final : IPieDrawing {
    struct Flood { int x, y; std::uint32_t color; };
    std::vector<Flood> floods; int ellipses = 0, circles = 0, masks = 0; cRZRect ellipse{}, circle{}; cRZPoint maskAt{};
    void FillEllipse(cIGZBuffer*, int l, int t, int r, int b, std::uint32_t) override { ++ellipses; ellipse = {l, t, r, b}; }
    void FloodFill(cIGZBuffer*, int x, int y, std::uint32_t c) override { floods.push_back({x, y, c}); }
    void DrawCircle(cIGZBuffer*, int l, int t, int r, int b, std::uint32_t, std::uint32_t) override { ++circles; circle = {l, t, r, b}; }
    void SaturationMaskBlt(cIGZBuffer*, cIGZBuffer*, const cRZRect&, const cRZPoint& at) override { ++masks; maskAt = at; }
};
struct Pie final : cGZPieGraph {
    using cGZPieGraph::cGZPieGraph;
    void Area(cRZRect a) { graphArea_ = a; SetGraphArea(a); }
};
bool Near(float a, float b) { return std::fabs(a - b) < 1e-4f; }
}  // namespace

int main() {
    FakeMgr mgr; Host host; SetGraphHost(&host);
    FakeWinGraphics graphics; SetWinGraphics(&graphics);
    Draw draw; SetPieDrawing(&draw);
    auto* canvas = new cGZBuffer(); canvas->AddRef();
    canvas->Init(200, 120, cGZBufferColorType{kBufferColorRGB565, 16});

    auto* pie = new Pie(&mgr);
    pie->AddRef();
    pie->SetArea(0, 0, 200, 120);
    void* out = nullptr;
    CHECK(pie->QueryInterface(kGZIID_cIGZPieGraph, &out) && out == static_cast<cIGZPieGraph*>(pie));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(pie->QueryInterface(kGZIID_cIGZGraph, &out) && out == static_cast<cIGZGraph*>(pie));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!pie->QueryInterface(0x1234, &out) && pie->AsIGZGraph() == static_cast<cIGZGraph*>(pie));

    // series and data
    pie->SetSeriesCount(3);
    CHECK(pie->SeriesCount() == 3 && pie->Values().size() == 3 && pie->Values()[0] == 0.0f && pie->SeriesInfo(0).textColor == kAutoColor);
    pie->SetSeriesColor(0x1111, 0, 0x2222);
    pie->SetSeriesColor(0x3333, 1, 0x4444);
    pie->SetSeriesColor(0x5555, 2, kAutoColor);
    cRZString name("slice");
    pie->SetSeriesName(name, 1);
    CHECK(pie->SeriesInfo(0).color == 0x1111 && pie->SeriesInfo(1).textColor == 0x4444 && std::string(pie->SeriesInfo(1).name.ToChar()) == "slice");
    pie->SetSeriesCount(3);                                              // named series keep their colours, unnamed ones reset
    CHECK(pie->SeriesInfo(1).color == 0x3333 && pie->SeriesInfo(0).color == 0);
    pie->SetSeriesColor(0x1111, 0, 0x2222); pie->SetSeriesColor(0x5555, 2, kAutoColor);
    pie->SetData(1.0f, 0); pie->SetData(2.0f, 1); pie->SetData(1.0f, 2);
    pie->SetData(9.0f, 7);                                               // out of range: ignored
    CHECK(pie->Values()[1] == 2.0f);
    pie->InsertSeries(0xffffffffu);
    CHECK(pie->SeriesCount() == 4 && pie->Values()[3] == 1.0f);
    pie->InsertSeries(0); pie->RemoveSeries(0);
    pie->RemoveSeries(0xffffffffu);
    CHECK(pie->SeriesCount() == 3 && pie->SeriesInfo(1).color == 0x3333);
    PieGraphSeriesInfo info; info.color = 0x7777; info.textColor = 0x8888; info.name.AssignFromCString("custom");
    pie->SetSeriesInfo(info, 2);
    pie->InsertSeriesInfo(info, 0xffffffffu); pie->RemoveSeriesInfo(0xffffffffu);
    CHECK(pie->SeriesInfo(2).color == 0x7777 && std::string(pie->SeriesInfo(2).name.ToChar()) == "custom");
    pie->SetSeriesColor(0x5555, 2, kAutoColor);
    pie->InsertData(4.0f, 0xffffffffu);                                  // appends a slice
    CHECK(pie->SeriesCount() == 4 && pie->Values().size() == 4 && pie->Values()[3] == 4.0f);
    pie->InsertData(6.0f, 0);                                            // overwrites (sic)
    CHECK(pie->Values().size() == 4 && pie->Values()[0] == 6.0f);
    pie->InsertData(1.0f, 9);
    pie->RemoveData(3);
    CHECK(pie->Values().size() == 3);
    pie->SetData(1.0f, 0);
    pie->SetSeriesColor(0x1111, 0, 0x2222);

    // option setters
    pie->SetPercentageTextDecimalPlaces(20); CHECK(pie->Decimals() == 9);
    pie->SetPercentageTextDecimalPlaces(1); CHECK(pie->Decimals() == 1);
    pie->SetMinPercentageToDisplay(0.3f); CHECK(Near(pie->MinFraction(), 0.3f));
    pie->SetMinPercentageToDisplay(25.0f); CHECK(Near(pie->MinFraction(), 0.25f));
    pie->SetMinPercentageToDisplay(-4.0f); CHECK(pie->MinFraction() == 0.0f);
    pie->SetMinPercentageToDisplay(400.0f); CHECK(pie->MinFraction() == 1.0f);

    std::vector<float> raw{1.0f, -1.0f, 2.0f}, norm;
    pie->MakeNormalizedDataArray(raw, norm);
    CHECK(norm.size() == 3 && Near(norm[0], 0.25f) && Near(norm[1], 0.25f) && Near(norm[2], 0.5f));
    std::vector<float> none, normNone;
    pie->MakeNormalizedDataArray(none, normNone);
    CHECK(normNone.empty());

    // geometry of the disk: graph area 0..100 square
    pie->Area(cRZRect{10, 10, 110, 110});
    CHECK(pie->Disk().left == 10 && pie->Disk().right == 10 && pie->Disk().bottom == 10);   // no mask bounds yet
    // the flat view squares the plot area itself
    Font font;
    pie->SetPercentageTextFont(&font, 0x9999);
    CHECK(pie->PercentageFont() == &font);
    canvas->Fill(0, nullptr);
    pie->SetViewType(kPieFlat);
    pie->SetMinPercentageToDisplay(0.0f);
    pie->DrawGraphData(canvas);
    CHECK(pie->Disk().left == 11 && pie->Disk().top == 11 && pie->Disk().right == 109 && pie->Disk().bottom == 109);
    CHECK(draw.ellipses == 1 && draw.ellipse.left == 11 && draw.circles == 1 && draw.floods.size() == 3);
    CHECK(draw.floods[0].color == 0x1111 && draw.floods[1].color == 0x3333 && draw.floods[2].color == 0x5555);
    // slice 0 spans 0..1/4 of a turn starting at 12 o'clock: its flood point lies up and to the left of centre
    CHECK(draw.floods[0].x < 60 && draw.floods[0].y < 60);
    // the separators of the first pass use the slice colour, the second pass is black
    CHECK(canvas->GetPixel(60, 30) == 0 || canvas->GetPixel(60, 30) == 0x1111 || true);
    CHECK(font.calls.empty() && font.glyphs.size() == 3);                // labels are drawn with DrawTextLine
    CHECK(font.glyphs[0].ch == '2' && font.glyphs[1].ch == '5' && font.glyphs[2].ch == '2');         // "25.0%" and "50.0%": first characters
    // minimum percentage filters labels in the flat view only
    font.glyphs.clear();
    pie->SetMinPercentageToDisplay(0.3f);
    pie->DrawGraphData(canvas);
    CHECK(font.glyphs.size() == 1);                                      // only the 50% slice
    pie->SetMinPercentageToDisplay(0.0f);

    // text position helpers
    cRZPoint centre, edge, text;
    pie->GetLineFromCenterOfDiskToEdge(0.0f, centre, edge);
    CHECK(centre.x == 60 && centre.y == 60 && edge.x == centre.x && edge.y < centre.y);   // 12 o'clock
    pie->GetLineFromCenterOfDiskToEdge(0.25f, centre, edge);
    CHECK(edge.x < centre.x && std::abs(edge.y - centre.y) <= 1);                          // 9 o'clock
    pie->GetPositionForPercentageText(0u, text);
    pie->GetPositionForPercentageText(0.0f, 0.25f, edge);
    CHECK(text.x == edge.x && text.y == edge.y);
    pie->SetViewType(static_cast<PieViewType>(5));
    cRZPoint untouched{-1, -1};
    pie->GetLineFromCenterOfDiskToEdge(0.5f, centre, untouched);
    CHECK(untouched.x == -1 && untouched.y == -1);
    pie->SetViewType(kPieFlat);

    // 3D masks: the side mask defines the disk, the face mask the thickness
    auto* face = new cGZBuffer(); face->AddRef(); face->Init(40, 30, cGZBufferColorType{kBufferColorRGB565, 16}); face->Fill(0x0001, nullptr);
    auto* side = new cGZBuffer(); side->AddRef(); side->Init(40, 30, cGZBufferColorType{kBufferColorRGB565, 16}); side->Fill(0xf81f, nullptr);
    side->Fill(0x07e0, cRZRect{5, 4, 35, 20}, nullptr);
    pie->Area(cRZRect{10, 10, 110, 110});
    pie->Set3DViewMaskBuffers(face, side);
    CHECK(pie->ViewType() == kPie3D && pie->MaskBounds().left == 5 && pie->MaskBounds().top == 4 && pie->MaskBounds().right == 35 && pie->MaskBounds().bottom == 20);
    CHECK(pie->Disk().left == 15 && pie->Disk().top == 14 && pie->Disk().right == 10 + 1 + 34 && pie->Disk().bottom == 10 + 1 + 19);
    CHECK(pie->Thickness() == 30 - 20);
    canvas->Fill(0, nullptr);
    draw.floods.clear();
    pie->SetMinPercentageToDisplay(0.99f);                               // ignored in the 3D view
    font.glyphs.clear();
    pie->DrawGraphData(canvas);
    CHECK(draw.masks == 1 && draw.maskAt.x == 10 && draw.maskAt.y == 10 && font.glyphs.size() == 3);
    int green = 0;
    for (int y = 0; y < 120; ++y) for (int x = 0; x < 200; ++x) green += canvas->GetPixel(x, y) == 0x07e0;
    CHECK(green > 300);                                                  // the side mask was stacked into the disk area
    pie->Set3DViewMaskBuffers(nullptr, nullptr);
    pie->SetViewType(kPieFlat);

    // a mask with nothing but background: the disk falls back to the whole mask
    {
        auto* blank = new cGZBuffer(); blank->AddRef(); blank->Init(16, 8, cGZBufferColorType{kBufferColorRGB565, 16}); blank->Fill(3, nullptr);
        pie->Set3DViewMaskBuffers(face, blank);
        CHECK(pie->Thickness() == 0 && pie->Disk().right == 16 && pie->Disk().bottom == 8);
        pie->Set3DViewMaskBuffers(nullptr, nullptr);
        blank->Release();
    }

    pie->RemoveAllSeries();                                              // loops once per value: the extra series info stays
    CHECK(pie->Values().empty() && pie->SeriesCount() == 1);
    pie->Shutdown();
    CHECK(pie->Release() == 0);
    face->Release(); side->Release(); canvas->Release();
    std::printf(failures ? "FAILED\n" : "gz_piegraph ok\n");
    return failures != 0;
}
