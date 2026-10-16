#include <cstdio>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/WinText.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;
using fake_dc::Ctx;

namespace {
struct FakeFont final : IWinFont {
    int refs = 0; std::vector<std::string> drawn; std::uint32_t lastAlign = 99;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t DefaultTextColor() override { return 0xff102030; }
    std::uint32_t DefaultShadowColor() override { return 0xff405060; }
    std::int16_t LineHeight(int, int) override { return 10; }
    int CountLines(const char*, std::uint32_t length, int width) override { return std::max(1, int((length * 5 + width - 1) / width)); }
    int WrappedWidth(const char*, std::uint32_t, int) override { return 0; }
    void MeasureText(const char*, std::uint32_t length, cRZRect& out) override { out = {0, 0, int(length) * 5, 10}; }
    int TextWidth(const char*, std::uint32_t length) override { return int(length) * 5; }
    void DrawText(IWinBuffer*, int, int, const char*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
    void DrawTextBox(IWinBuffer*, const cRZRect& box, const char*, std::uint32_t, std::uint32_t align, std::uint32_t c1, std::uint32_t c2, const cRZRect*, int) override {
        lastAlign = align;
        drawn.push_back("box " + std::to_string(box.left) + "," + std::to_string(box.top) + "," + std::to_string(box.right) + "," + std::to_string(box.bottom) + " " + std::to_string(c1) + "/" + std::to_string(c2));
    }
};
struct Source final : IWinFontSource {
    FakeFont font;
    IWinFont* Font(std::uint32_t) override { return &font; }
    bool Available() override { return true; }
};
struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct Text final : cGZWinText {
    using cGZWinText::cGZWinText;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; Ctx ctx; Source source;
    SetWinGraphics(&graphics);
    SetWinFontSource(&source);
    auto* target = new Target(&mgr);
    target->AddRef();
    target->SetArea(0, 0, 300, 300);
    auto* text = new Text(&mgr);
    text->SetArea(10, 10, 110, 40);                          // 100 x 30
    text->Init();
    target->ChildAdd(text);
    text->SetCtx(&ctx);
    CHECK(source.font.refs == 1);
    std::uint32_t native = 0;
    CHECK(text->GetTextColor(cIGZWinText::kColorText, native));       // the font's colours became the text colours
    CHECK(text->GetWinTextFlag(kTextDefaultColors) && text->GetWinTextFlag(kTextWrap) && text->GetWinTextFlag(kTextOpaque));

    text->SetCaption(cRZString("Hello"));
    cRZRect min;
    text->GetMinTextArea(min, false, false);
    CHECK(min.right == 96 && min.bottom == 10);                     // wrapped to the window minus the gutters (100 - 4)
    CHECK(text->TextRect().left == 2 && text->TextRect().right == 98);   // alignment 0: top row, window-sized rectangle
    text->SetAlignment(1);                                           // middle row
    CHECK(text->TextRect().top == (30 - 10) / 2 && text->TextRect().bottom == 20 / 2 + 10);
    text->SetAlignment(2);                                           // bottom
    CHECK(text->TextRect().top == 30 - 10 - 2);
    CHECK(!text->SetAlignment(9));
    text->SetAlignment(5);
    ctx.log.clear(); source.font.drawn.clear();
    text->GZPaint();
    CHECK(ctx.log.size() == 2 && source.font.drawn.size() == 1 && source.font.lastAlign == 1);   // fill + text; 5 / 3 = centre

    // colours: explicit colours leave the "default" state
    text->SetTextColor(cIGZWinText::kColorText, cRZColor{0xabcdef});
    CHECK(!text->GetWinTextFlag(kTextDefaultColors));
    cRZColor color;
    CHECK(text->GetTextColor(cIGZWinText::kColorText, color) && color.rgb == 0xabcdef);
    text->ClearTextColor(cIGZWinText::kColorText);
    text->ClearTextColor(cIGZWinText::kColorShadow);
    CHECK(text->GetWinTextFlag(kTextDefaultColors));

    // outline and fitting
    text->SetOutlineUse(true, 0x00ff00);
    bool on; std::uint32_t outline;
    text->GetOutlineUse(on, outline);
    CHECK(on && outline == 0x00ff00);
    text->SetWinTextFlag(kTextWrap, false);
    text->FitWindowToText(false, false);
    CHECK(text->GetW() == 25 + 4 && text->GetH() == 10 + 4);

    // mouse messages pass through to the parent when asked
    cGZMessage move{kGZMsgMouseMove, 1, 2, 0};
    CHECK(!text->DoMessage(move));
    text->SetWinTextFlag(kTextPassThrough, true);
    CHECK(text->DoMessage(move) && target->got.back().param1 == 11 && target->got.back().param2 == 12);
    text->Shutdown();
    CHECK(source.font.refs == 0);
    target->Release();
    std::printf(failures ? "FAILED\n" : "gz_wintext ok\n");
    return failures != 0;
}
