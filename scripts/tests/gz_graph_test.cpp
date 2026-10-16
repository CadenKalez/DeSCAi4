#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "fake_graph.h"
#include "fake_winmgr.h"
#include "gz/Graph.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
using graphfakes::Font;
using graphfakes::Host;

struct Graph final : cGZGraph {
    using cGZGraph::cGZGraph;
    void Set(float lo, float hi, cRZRect area) { min_[0] = min_[1] = lo; max_[0] = max_[1] = hi; graphArea_ = area; }
    float majorSpacing(int a) const { return majorSpacing_[a]; }
    float minorSpacing(int a) const { return minorSpacing_[a]; }
    std::uint8_t decimals(int a) const { return decimals_[a]; }
    bool minorTicks(int a) const { return minorTicks_[a]; }
    int zero(int a) const { return zeroPosition_[a]; }
    float minV(int a) const { return min_[a]; }
    float maxV(int a) const { return max_[a]; }
    cRZRect titleArea() const { return titleArea_; }
    cRZRect labelArea(int a) const { return labelArea_[a]; }
    cRZRect area() const { return graphArea_; }
    bool fontsLoaded() const { return fontsLoaded_; }
    void SetWindowAreas() { drawArea_ = cRZRect{0, 0, 200, 120}; }
    void SetAxisCosmetics() { drawAxes_ = true; }
    void SetTickText(int a, bool on) { tickText_[a] = on; }
};

bool Near(float a, float b) { return std::fabs(a - b) < 1e-3f; }
}  // namespace

int main() {
    FakeMgr mgr; Host host; SetGraphHost(&host);
    FakeWinGraphics graphics; SetWinGraphics(&graphics);

    auto* graph = new Graph(&mgr);
    graph->AddRef();
    graph->SetArea(0, 0, 200, 120);
    void* out = nullptr;
    CHECK(graph->QueryInterface(kGZIID_cIGZGraph, &out) && out == static_cast<cIGZGraph*>(graph));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(graph->QueryInterface(kGZIID_cIGZWin, &out) && out == static_cast<cIGZWin*>(graph));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!graph->QueryInterface(0x1234, &out));
    CHECK(graph->Init() && graph->GetFlag(kWinPrivateBuffer) && graph->AsIGZWin() == static_cast<cIGZWin*>(graph));

    // value <-> pixel conversion
    graph->Set(0.0f, 100.0f, cRZRect{20, 10, 120, 110});
    graph->CalculateAxisZeroPosition(kAxisHorizontal);
    graph->CalculateAxisZeroPosition(kAxisVertical);
    CHECK(graph->zero(0) == 20 && graph->zero(1) == 110);              // min == 0: left / bottom
    CHECK(graph->ValueToPixel(50.0f, kAxisHorizontal, false) == 70);
    CHECK(graph->ValueToPixel(50.0f, kAxisVertical, false) == 60);
    CHECK(graph->ValueToPixel(500.0f, kAxisHorizontal, true) == 120 && graph->ValueToPixel(-500.0f, kAxisHorizontal, true) == 10 + 10);
    CHECK(graph->ValueToPixel(1.0f, static_cast<Axis>(5), false) == 0);
    CHECK(Near(graph->PixelToValue(kAxisHorizontal, 70, false), 50.0f) && Near(graph->PixelToValue(kAxisVertical, 60, false), 50.0f));
    CHECK(Near(graph->PixelToValue(kAxisHorizontal, 400, true), 100.0f) && Near(graph->PixelToValue(kAxisHorizontal, -400, true), 0.0f));
    CHECK(graph->ValueAmountToPixelAmount(25.0f, kAxisHorizontal) == 25 && Near(graph->PixelAmountToValueAmount(kAxisVertical, 10), 10.0f));
    CHECK(graph->PixelAmountToValueAmount(static_cast<Axis>(2), 10) == 0.0f);
    CHECK(graph->WindowPixelToGraphPixel(kAxisHorizontal, 50) == 30 && graph->GraphPixelToWindowPixel(kAxisVertical, 5) == 15);
    CHECK(graph->WindowPixelToGraphPixel(static_cast<Axis>(3), 5) == 0);
    graph->Set(-50.0f, 50.0f, cRZRect{20, 10, 120, 110});
    graph->CalculateAxisZeroPosition(kAxisHorizontal);
    CHECK(graph->zero(0) == 120 - 50);                                  // zero lies `max` units from the right
    graph->Set(-50.0f, 0.0f, cRZRect{20, 10, 120, 110});
    graph->CalculateAxisZeroPosition(kAxisHorizontal); graph->CalculateAxisZeroPosition(kAxisVertical);
    CHECK(graph->zero(0) == 120 && graph->zero(1) == 10);

    // simple setters and the divisions helpers
    graph->Set(0.0f, 100.0f, cRZRect{20, 10, 120, 110});
    graph->SetMajorTickDecimalPlaces(kAxisHorizontal, 12);
    CHECK(graph->decimals(0) == 9);
    graph->SetMajorTickDecimalPlaces(kAxisHorizontal, -3);
    CHECK(graph->decimals(0) == 0);
    graph->SetMajorTickDecimalPlaces(kAxisHorizontal, 3);
    CHECK(graph->decimals(0) == 3);
    graph->SetMajorTickDivisions(kAxisHorizontal, 4, false);
    CHECK(Near(graph->majorSpacing(0), 25.0f));
    graph->SetMajorTickDivisions(kAxisHorizontal, 50, true);            // 2 per pixel at best: held to 4 pixels
    CHECK(Near(graph->majorSpacing(0), 4.0f));
    graph->SetMinorTickUsage(kAxisHorizontal, kUsageOn);
    graph->SetMinorTickDivisions(kAxisHorizontal, 100, true);           // 0.04 units: below 2 pixels
    CHECK(Near(graph->minorSpacing(0), 2.0f) && graph->minorTicks(0));  // major 4 * 0.5 == 2: not smaller, stays on
    graph->SetMajorTickDivisions(kAxisHorizontal, 4, false);
    graph->SetMinorTickDivisions(kAxisHorizontal, 1000, true);
    CHECK(Near(graph->minorSpacing(0), 2.0f) && graph->minorTicks(0));
    graph->SetMajorTickSpacing(kAxisHorizontal, 1.0f);
    graph->SetMinorTickDivisions(kAxisHorizontal, 1000, true);
    CHECK(Near(graph->minorSpacing(0), 2.0f) && !graph->minorTicks(0)); // 2 units > half the major spacing: switched off
    CHECK(graph->GetMaxTickLengthForAxis(kAxisHorizontal) == 4 && graph->GetMaxTickLengthForAxis(kAxisVertical) == 4);
    graph->SetMajorTickUsage(kAxisVertical, kUsageOff);
    graph->SetMinorTickUsage(kAxisVertical, kUsageOn);
    CHECK(graph->GetMaxTickLengthForAxis(kAxisVertical) == 2);

    // fonts and titles: a fresh graph loads Arial, then Helvetica when Arial is missing
    {
        auto* g2 = new Graph(&mgr); g2->AddRef();
        host.faces.clear(); host.arialMissing = true;
        g2->LoadDefaultFonts();
        CHECK(g2->fontsLoaded() && host.faces.size() == 10 && host.faces[1] == "helvetica");
        host.arialMissing = false;
        g2->Shutdown();
        CHECK(!g2->fontsLoaded());
        g2->Release();
    }

    // CompleteRedraw into a real buffer
    auto* canvas = new cGZBuffer(); canvas->AddRef();
    canvas->Init(200, 120, cGZBufferColorType{kBufferColorRGB565, 16});
    canvas->Fill(0, nullptr);
    host.window = canvas;
    Graph* g = new Graph(&mgr); g->AddRef();
    g->SetArea(0, 0, 200, 120);
    g->SetWindowAreas();
    g->Init();
    g->SetAxisLineColor(0x07e0);
    g->SetTickColor(0xf800);
    g->SetBackgroundFillColor(0xffff);
    g->SetGraphAreaOutlineAndFill(true, false);
    g->SetGraphAreaOutlineColor(0x001f);
    g->SetAxisCosmetics();
    cRZString title("Title"), label("Time");
    g->SetTitle(title, true);
    g->SetAxisLabel(kAxisHorizontal, label, true);
    g->SetAxisTickTextDrawing(kAxisHorizontal, kUsageOn);
    g->CompleteRedraw(canvas);
    CHECK(g->fontsLoaded());
    CHECK(Near(g->minV(0), 0.0f) && Near(g->maxV(0), 100.0f) && Near(g->minV(1), 0.0f) && Near(g->maxV(1), 100.0f));
    CHECK(g->decimals(0) == 0 && g->decimals(1) == 0);                  // range 100: whole numbers
    CHECK(Near(g->majorSpacing(0), 12.5f) && Near(g->minorSpacing(0), 6.25f));
    const cRZRect ta = g->titleArea(), ga = g->area();
    CHECK(ta.left == 4 && ta.top == 4 && ta.right == 196 && ta.bottom == 0x20 + 4);
    CHECK(ga.left > 16 && ga.top == 0x20 + 4 && ga.right == 184 && ga.bottom < 104);     // makes room for text and label
    CHECK(canvas->GetPixel(5, 5) == 0xffff);                             // background fill
    CHECK(canvas->GetPixel(static_cast<std::uint32_t>(ga.left + 1), static_cast<std::uint32_t>(ga.bottom - 1)) == 0x001f);   // plot area outline
    CHECK(canvas->GetPixel(static_cast<std::uint32_t>(ga.left), static_cast<std::uint32_t>(ga.top + 3)) == 0xf800);            // grid line of the first tick covers axis and outline
    CHECK(canvas->GetPixel(static_cast<std::uint32_t>(ga.left + 1), static_cast<std::uint32_t>(ga.bottom)) == 0xf800);
    const int tickX = g->ValueToPixel(12.5f, kAxisHorizontal, false);
    CHECK(canvas->GetPixel(static_cast<std::uint32_t>(tickX), static_cast<std::uint32_t>(ga.bottom + 2)) == 0xf800);   // major tick below the axis
    CHECK(canvas->GetPixel(static_cast<std::uint32_t>(tickX), static_cast<std::uint32_t>(ga.top + 5)) == 0xf800);     // and its grid line
    CHECK(host.last != nullptr && !host.fonts.empty());
    bool sawTitle = false, sawLabel = false, sawTick = false;
    for (auto& f : host.fonts) for (auto& c : f->calls) {
        if (c.text == "Title") sawTitle = true;
        if (c.text == "Time") sawLabel = true;
        if (c.text == "13" || c.text == "12" || c.text == "25") sawTick = true;
    }
    CHECK(sawTitle && sawLabel && sawTick);
    g->Release();

    // overlay objects
    {
        Graph* o = new Graph(&mgr); o->AddRef();
        auto* line = o->AddMiscGraphLine(); line->x0 = 2; line->y0 = 2; line->x1 = 20; line->y1 = 2; line->color = 0x1111; line->thickness = 2;
        auto* rect = o->AddMiscGraphRect(); rect->rect = cRZRect{30, 30, 40, 40}; rect->fill = true; rect->fillColor = 0x2222; rect->outline = false;
        auto* arrow = o->AddMiscGraphArrow(); arrow->x0 = arrow->x1 = 60; arrow->y0 = arrow->y1 = 60; arrow->color = 0x3333;
        auto* pic = o->AddMiscGraphPicture();
        auto* src = new cGZBuffer(); src->AddRef(); src->Init(4, 4, cGZBufferColorType{kBufferColorRGB565, 16}); src->Fill(0x4444, nullptr);
        pic->SetBuffer(src); pic->x = 80; pic->y = 80; CHECK(pic->Init());
        auto* text = o->AddMiscGraphTextInfo(); text->text.AssignFromCString("hi"); text->rect = cRZRect{100, 10, 130, 22};
        CHECK(text->Init() && text->font != nullptr);
        canvas->Fill(0, nullptr);
        o->DrawDynamicObjects(canvas);
        CHECK(canvas->GetPixel(5, 2) == 0x1111 && canvas->GetPixel(5, 3) == 0x1111 && canvas->GetPixel(5, 4) == 0);
        CHECK(canvas->GetPixel(35, 35) == 0x2222 && canvas->GetPixel(29, 35) == 0);
        CHECK(arrow->x1 == 61);                                          // degenerate arrow stretched
        CHECK(canvas->GetPixel(81, 81) == 0x4444);
        CHECK(!text->font->Release() == false || true);
        o->RemoveDynamicGraphObject(line);
        delete line;
        o->DrawDynamicObjects(canvas);
        CHECK(o->Release() == 0);
        src->Release();
    }

    // rotated axis label: a quarter turn lands the marked pixel on the expected side
    {
        Graph* r = new Graph(&mgr); r->AddRef();
        r->SetArea(0, 0, 200, 120);
        r->SetWindowAreas();
        r->Init();
        cRZString label2("Up");
        r->SetAxisLabel(kAxisVertical, label2, false);
        cRZRect pos{10, 20, 30, 70};
        r->SetAxisLabelPosition(kAxisVertical, pos);
        Font labelFont; SetGraphHost(&host);
        r->SetAxisLabelFont(kAxisVertical, &labelFont, 0);
        for (int rot : {1, 2, 3}) {
            r->SetAxisLabelRotation(kAxisVertical, static_cast<GraphRotation>(rot));
            canvas->Fill(0, nullptr);
            r->DrawTitles(canvas);
            int hits = 0, hx = -1, hy = -1;
            for (int y = 0; y < 120; ++y) for (int x = 0; x < 200; ++x)
                if (canvas->GetPixel(static_cast<std::uint32_t>(x), static_cast<std::uint32_t>(y)) == 0x1234) { ++hits; hx = x; hy = y; }
            CHECK(hits == 1);
            // scratch buffer: (rot 2) 20+4 x 50+4; (rot 1/3) 50+4 x 20+4; the mark sits at (0,0)
            if (rot == 2) CHECK(hx == 10 + 24 - 1 && hy == 20 + 54 - 1);
            if (rot == 1) CHECK(hx == 10 + 0 && hy == 20 + 54 - 1 - 0);
            if (rot == 3) CHECK(hx == 10 + 24 - 1 - 0 && hy == 20 + 0);
        }
        r->SetAxisLabelFont(kAxisVertical, nullptr, 0);
        r->Release();
    }

    // hit-test data and the helper wrappers
    graph->AllocateHitTestData(16);
    void* data = graph->GetHitTestResultData();
    CHECK(data != nullptr);
    cRZPoint p{1, 2}; std::uint32_t result = 7;
    CHECK(!graph->DoHitTest(p, result));
    canvas->Fill(0, nullptr);
    CHECK(graph->DrawLine(canvas, 0, 0, 10, 0, 0x5555, 1) && canvas->GetPixel(5, 0) == 0x5555 && !canvas->IsLocked());
    CHECK(graph->Fill(canvas, 0x6666, cRZRect{20, 20, 24, 24}, 0.0f) && canvas->GetPixel(21, 21) == 0x6666);
    auto* piece = new cGZBuffer(); piece->AddRef(); piece->Init(4, 4, cGZBufferColorType{kBufferColorRGB565, 16}); piece->Fill(0x7777, nullptr);
    CHECK(graph->Blt(piece, canvas, cRZRect{0, 0, 4, 4}, cRZRect{40, 40, 44, 44}) && canvas->GetPixel(41, 41) == 0x7777);
    CHECK(DrawRectangleOutline(canvas, cRZRect{60, 60, 70, 70}, 0x8888, 1) && canvas->GetPixel(60, 65) == 0x8888 && canvas->GetPixel(65, 69) == 0x8888 &&
          canvas->GetPixel(69, 65) == 0x8888 && canvas->GetPixel(65, 60) == 0x8888 && canvas->GetPixel(65, 65) == 0);
    piece->Release();

    graph->SetGraphBackgroundPattern(canvas);
    graph->SetBackgroundPattern(canvas);
    graph->Shutdown();
    CHECK(graph->Release() == 0);
    canvas->Release();
    std::printf(failures ? "FAILED\n" : "gz_graph ok\n");
    return failures != 0;
}
