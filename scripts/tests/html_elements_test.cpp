#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "html/HTMLElements.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace html;

namespace {

// Monospace font: 8 pixels per character, 10 pixel lines, ascent 8.
struct FakeFont : IHtmlFont {
    int releases = 0;
    std::uint32_t style = 0;
    std::vector<std::string> drawn;
    void Release() override { ++releases; }
    void SetStyle(std::uint32_t s, bool enable) override { if (s == 0xffffffffu) style = 0; else if (enable) style |= s; }
    int LineHeight() override { return 10; }
    int GlyphHeight(int, int) override { return 9; }
    int Ascent() override { return 8; }
    int CharsThatFit(const char* text, int length, int maxWidth, int, int) override {
        int fit = maxWidth / 8;
        if (fit >= length) return length;
        // break after the last blank within the first `fit` characters (+ the blank itself)
        int last = -1;
        for (int i = 0; i < fit + 1 && i < length; ++i) if (text[i] == ' ') last = i;
        return last >= 0 && last < length ? last + 1 : 0;
    }
    int FirstWordLength(const char* text, int length, int, int* wordWidth) override {
        int n = 0;
        while (n < length && text[n] != ' ') ++n;
        if (n < length) ++n;
        *wordWidth = n * 8;
        return n;
    }
    int StringWidth(const char*, int length) override { return length * 8; }
    void DrawText(IHtmlSurface*, int x, int y, const char* text, int length, Color, std::uint32_t, const cRZRect*) override {
        drawn.push_back(std::to_string(x) + "," + std::to_string(y) + ":" + std::string(text, static_cast<std::size_t>(length)));
    }
};

struct FakeFonts : IHtmlFontSystem {
    FakeFont font;
    std::vector<std::string> requests;
    IHtmlFont* CreateFont(const std::string& face, std::uint32_t kind, std::uint32_t, std::uint32_t, bool) override {
        requests.push_back(face + "/" + std::to_string(kind));
        return face == "missing" ? nullptr : &font;
    }
};

struct FakeDocument : IHtmlDocumentServices {
    FakeFonts fonts;
    IHtmlFontSystem* FontSystem() override { return &fonts; }
    std::string GetStringForEscape(const cRZString& name) override { return name.str() == "amp" ? "&" : "?"; }
    void GetDefaultVariableWidthFontInfo(std::string& face, std::uint32_t& style) override { face = "arial"; style = 0; }
    void GetDefaultFixedWidthFontInfo(std::string& face, std::uint32_t& style) override { face = "courier new"; style = 0; }
};

struct FakeGraphics : IHtmlGraphics {
    Color MakeColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { return 0xff000000u | (r << 16) | (g << 8) | b; }
};

struct FakeSurface : IHtmlSurface {
    int lines = 0, fills = 0;
    bool Begin(std::uint32_t) override { return true; }
    void End(std::uint32_t) override {}
    void FillRect(Color, const cRZRect&, const cRZRect*) override { ++fills; }
    void DrawLine(int, int, int, int, Color, int) override { ++lines; }
    void Blit(IHtmlBitmap*, const cRZRect&, const cRZRect&, const cRZRect*) override {}
    void BlitStretched(IHtmlBitmap*, const cRZRect&, const cRZRect&, const cRZRect*) override {}
};

cHTMLDrawContext MakeContext(FakeGraphics& gfx, FakeSurface& surface, int width) {
    cHTMLDrawContext c;
    c.graphics = &gfx;
    c.surface = &surface;
    c.screenRect = cRZRect{0, 0, width, 400};
    c.documentRect = cRZRect{0, 0, width, 400};
    c.InitForFreshPainting();
    return c;
}

}  // namespace

int main() {
    // ---- shared helpers
    Color color = 0xff112233;
    cHTMLShared::SetColor(cRZString("Fuchsia"), color);
    CHECK(color == 0xffff00ff);
    cHTMLShared::SetColor(cRZString("#1a2B3c"), color);
    CHECK(color == 0xff1a2b3c);
    cHTMLShared::SetColor(cRZString("12345"), color);          // five digits: left alone
    CHECK(color == 0xff1a2b3c);
    cHTMLShared::SetColor(cRZString("nonsense"), color);
    CHECK(color == 0xff1a2b3c);
    tLength length;
    cHTMLShared::SetLength(cRZString("75%"), length);
    CHECK(length.unit == tLength::kPercent && length.value == 75);
    cHTMLShared::SetLength(cRZString("120"), length);
    CHECK(length.unit == tLength::kPixels && length.value == 120);
    CHECK(cHTMLShared::StringLeftEquals(cRZString("#xAB"), 2, "#x") && !cHTMLShared::StringLeftEquals(cRZString("#"), 2, "#x"));
    CHECK(cHTMLShared::StringLeftEquals(cRZString("abc"), 'a') && !cHTMLShared::StringLeftEquals(cRZString(""), 'a'));

    // ---- whitespace handling
    std::string t = "  hello \t\n  world  ";
    cHTMLTextSegment::FixTextSegmentWhitespace(t, false);
    CHECK(t == " hello world ");
    t = "a\r\nb\rc\nd";
    cHTMLTextSegment::FixTextSegmentWhitespace(t, true);
    CHECK(t == "a\nb\nc\nd");

    // ---- text wrapping through the draw context
    FakeDocument doc;
    FakeGraphics gfx;
    FakeSurface surface;
    {
        cHTMLDrawContext ctx = MakeContext(gfx, surface, 80);       // 10 characters per line
        tFontAttributes attrs;
        attrs.kind = 1;
        cHTMLTextSegment text(&doc, cRZString("aa bb &amp; cc dd ee"), attrs, 0);
        CHECK(text.text() == "aa bb & cc dd ee");
        CHECK(doc.fonts.requests.back() == "arial/0");
        text.Layout(ctx);
        ctx.RenderLineElements(true);
        CHECK(text.laidOut());
        CHECK(text.renderAreas().size() >= 2);          // wrapped onto several lines
        int covered = 0;
        for (auto& area : text.renderAreas()) covered += area.length;
        CHECK(covered == static_cast<int>(text.text().size()));
        CHECK(text.renderAreas()[0].area.top == 0 && text.renderAreas()[1].area.top == 10);
        cHTMLElement* hit = nullptr;
        CHECK(text.Pick(hit, 2, 2) == 1 && hit == nullptr);     // not selectable
        CHECK(text.Pick(hit, 500, 2) == 0);
        text.Draw(ctx);
        CHECK(doc.fonts.font.drawn.size() == text.renderAreas().size() && doc.fonts.font.drawn[0].rfind("0,0:", 0) == 0);
    }
    {   // centred alignment shifts the line by half of the slack
        cHTMLDrawContext ctx = MakeContext(gfx, surface, 100);
        tFontAttributes attrs;
        attrs.kind = 2;
        doc.fonts.font.drawn.clear();
        cHTMLTextSegment text(&doc, cRZString("abcd"), attrs, 1);
        CHECK(doc.fonts.requests.back() == "courier new/0");
        text.Layout(ctx);
        ctx.RenderLineElements(true);
        CHECK(text.renderAreas().size() == 1 && text.renderAreas()[0].area.left == (100 - 32) / 2);
    }
    {   // font lists: the first available face wins
        tFontAttributes attrs;
        attrs.faceNames = "missing,times";
        cHTMLTextSegment text(&doc, cRZString("x"), attrs, 0);
        CHECK(doc.fonts.requests.size() >= 2 && doc.fonts.requests[doc.fonts.requests.size() - 2] == "missing/0" &&
              doc.fonts.requests.back() == "times/0");
    }

    // ---- block layout, margins, horizontal rule
    {
        cHTMLDrawContext ctx = MakeContext(gfx, surface, 200);
        cHTMLBODY body(0, 0, 0);
        body.graphics = &gfx;
        auto* quote = new cHTMLBlockQuote();
        auto* hr = new cHTMLHR();
        hr->size = 4;
        hr->width = 50;
        quote->Insert(hr);
        body.Insert(quote);
        body.Layout(ctx);
        body.PostLayout();
        CHECK(hr->laidOut() && hr->drawRect().left == 20 + 5 && hr->drawRect().top == 12 + 5);
        CHECK(hr->drawRect().right - hr->drawRect().left == ((200 - 10 - 20) * 50) / 100);
        CHECK(ctx.drawY == 12 + 4 + 10 + 12);          // quote spacing + rule
        CHECK(ctx.leftMargin == 0);                      // popped again
        cHTMLElement* hit = nullptr;
        CHECK(body.Pick(hit, 30, 18) == 1);
        hr->Draw(ctx);
        CHECK(surface.lines == 4);                       // top + left/bottom/right for thickness > 1
    }

    // ---- images: float, inline sizes
    {
        struct Bmp : IHtmlBitmap {
            int refs = 0;
            void AddRef() override { ++refs; }
            void Release() override { --refs; }
            int Width() override { return 30; }
            int Height() override { return 20; }
            cRZRect Bounds() override { return cRZRect{0, 0, 30, 20}; }
        } bmp;
        cHTMLDrawContext ctx = MakeContext(gfx, surface, 100);
        cHTMLIMG img;
        img.align = 0;                      // floats left
        CHECK(img.SetBitmap(&bmp) && img.width == 30 && img.height == 20 && bmp.refs == 1);
        CHECK(!img.SetBitmap(&bmp));
        img.Layout(ctx);
        CHECK(img.laidOut() && ctx.leftMargin == 30 && ctx.drawX == 30);
        ctx.IncrementCurrentDrawY(25);      // past the image: the margin is restored
        CHECK(ctx.leftMargin == 0);
        int ascent = 0, height = 0;
        cHTMLIMG inlineImg;
        inlineImg.align = 3;
        inlineImg.width = 10;
        inlineImg.height = 20;
        inlineImg.GetHeight(ascent, height);
        CHECK(ascent == 10 && height == 20);
        int used = 0;
        CHECK(inlineImg.CalculateWidthThatFitsInLine(5, used, false) && used == 0);
        CHECK(inlineImg.CalculateWidthThatFitsInLine(10, used, false) && used == 10);
    }

    // ---- tables: columns share the width by content
    {
        cHTMLDrawContext ctx = MakeContext(gfx, surface, 200);
        cHTMLTABLE table;
        for (int r = 0; r < 2; ++r) {
            auto* row = new cHTMLTR();
            table.Insert(row);
            table.AddRow(row);
            for (int c = 0; c < 2; ++c) {
                auto* cell = new cHTMLTD();
                row->Insert(cell);
                row->cells.push_back(cell);
                tFontAttributes attrs;
                attrs.kind = 1;
                cell->Insert(new cHTMLTextSegment(&doc, cRZString(c == 0 ? "abcd" : "abcdefghij"), attrs, 0));
            }
        }
        table.Layout(ctx);
        CHECK(table.laidOut() && table.columns.size() == 3);
        CHECK(table.columns[0].maxWidth == 32 && table.columns[1].maxWidth == 80);
        // the free 200 pixels are shared in proportion to the contents' growth room: 32/112 and 80/112
        CHECK(table.columns[0].x == 0 && table.columns[1].x == 58 && table.columns[2].x == 200);
        CHECK(table.rowTops.size() == 3 && table.rowTops[1] - table.rowTops[0] == table.rowHeights[0] && table.rowHeights[0] >= 10);
        CHECK(table.drawRect().bottom == table.rowTops[2] && ctx.drawY == table.rowTops[2]);
    }

    std::printf(failures ? "FAILED\n" : "html_elements: all passed\n");
    return failures ? 1 : 0;
}
