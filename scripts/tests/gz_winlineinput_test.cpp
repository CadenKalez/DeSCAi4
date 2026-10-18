#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinLineInput.h"
#include "gz/WinBtn.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinStyle.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct Ctx final : cIGZDrawContext {
    std::vector<std::string> log;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; }
    std::uint32_t Release() override { return 1; }
    void PushTransform() override {} void ClipReset() override {} void Clip(const cRZRect&) override {}
    void ClipAbsolute(const cRZRect&) override {} void Offset(const cRZPoint&) override {}
    void OffsetAbsolute(const cRZPoint&) override {} void PopTransform() override {} void PushState() override { log.push_back("push"); }
    void PopState() override { log.push_back("pop"); } cRZRect GetCanvasArea() override { return {}; } cRZRect GetClipRect() override { return {}; }
    cRZPoint GetOrigin() override { return {}; } int GetTransformStackDepth() override { return 0; }
    bool IsTargetAttached() override { return true; } void SetTarget(IDrawTargetBuffer*) override {}
    cIGZDrawEngine* GetEngine() override { return nullptr; } void ResetToDefaultState() override {}
    void SetColor(std::uint32_t rgb) override { log.push_back("color " + std::to_string(rgb)); }
    void SetColor(std::uint32_t, std::uint32_t) override {} void SetAlphaBlend(int) override {}
    void SetBltModulate(bool) override {} void SetAntialiasing(bool) override {} void SetLineThickness(float) override {}
    void ClearGradient() override {} void SetGradientTransform(const float (*)[4], float, float) override {}
    void ClearBlt() override {} void SetBlt(cIGZUnknown*, float, float, float, float) override {}
    void ClearBltTiling() override {} void SetBltTiling(const cRZRect&) override {} void Clear() override {}
    void Point(int, int) override {}
    void Line(int a, int b, int c, int d) override { log.push_back("line " + std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d)); }
    void Fill(const cRZRect& r) override { log.push_back("fill " + std::to_string(r.left) + "," + std::to_string(r.top) + "," + std::to_string(r.right) + "," + std::to_string(r.bottom)); }
    void Ellipse(int, int, int, int) override {} void FillEllipse(int, int, int, int) override {}
    void Blt(IDrawTargetBuffer*, const cRZRect& s, const cRZRect& d) override {
        log.push_back("blt " + std::to_string(s.left) + "," + std::to_string(s.top) + "," + std::to_string(s.right) + "," + std::to_string(s.bottom) +
                      ">" + std::to_string(d.left) + "," + std::to_string(d.top) + "," + std::to_string(d.right) + "," + std::to_string(d.bottom)); }
    void BltTiled(IDrawTargetBuffer*, const cRZRect&, const cRZRect&, int, int) override { log.push_back("tile"); }
    void Flush() override {} bool IsSuspended() override { return false; } void Suspend() override {} void Resume() override {}
    void Draw(eDrawingOp, const float*, int) override {} void DrawNoTransform(eDrawingOp, const float*, int) override {}
    bool Lock(cGZBufferSurfaceDesc&, bool) override { return true; } void Unlock() override {}
};


// Every glyph is 8 pixels wide, the line is 16 pixels high.
struct FakeFont final : cIGZFont {
    int refs = 0;
    std::vector<std::string> drawn;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Init() override { return true; } bool Shutdown() override { return true; }
    void GetName(cIGZString&) const override {} std::uint32_t GetStyle() const override { return 0; }
    bool GetStyle(int) const override { return false; } bool SetStyle(int, bool) override { return true; }
    std::uint32_t GetType() const override { return 0; } std::uint32_t GetSize() const override { return 16; }
    bool SetSize(std::uint32_t) override { return true; } std::uint32_t GetFontColor() const override { return 0; }
    std::uint32_t GetFontBackgroundColor() const override { return 0; } void SetFontColor(std::uint32_t, std::uint32_t) override {}
    int GetAntialiasing() override { return 0; } void SetAntialiasing(int) override {}
    float GetAntialiasingGammaCorrection() override { return 1; } void SetAntialiasingGammaCorrection(float) override {}
    float GetAntialiasingSharpness() override { return 1; } void SetAntialiasingSharpness(float) override {}
    void GetFilePath(cIGZString&) const override {} void SetFilePath(const cIGZString&) override {}
    bool GetCodePage(int&) const override { return true; } bool SetCodePage(int) override { return true; }
    bool IsCodePageAvailable(int) override { return true; } bool IsCharEncodingAvailable(int) override { return true; }
    bool IsCharAvailable(void*, std::uint32_t) override { return true; } bool PreloadChars(void*, std::uint32_t) override { return true; }
    void GetScaling(float& x, float& y) const override { x = y = 1; } void SetScaling(float, float) override {}
    void GetAdvanceScaling(float& x, float& y) const override { x = y = 1; } void SetAdvanceScaling(float, float) override {}
    void GetSpacingAdjustment(int& x, int& y) const override { x = y = 0; } void SetSpacingAdjustment(int, int) override {}
    int GetLineHeight() override { return 16; }
    int CharWidth(const void*, std::uint32_t n) override { return int(n) * 8; }
    int CharHeight(const void*, std::uint32_t) override { return 16; }
    int GetSizeAboveBaseline() override { return 12; } int GetSizeBelowBaseline() override { return 4; }
    int GetAdvanceWidth(const void*, std::uint32_t n) override { return int(n) * 8; }
    std::uint32_t CalculateWordsToFitInWidth(const void*, std::uint32_t, std::uint32_t, std::uint32_t*, WordWrapMode) override { return 0; }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t length, std::uint32_t width, std::uint32_t*) override {
        const std::uint32_t fit = width / 8; return fit < length ? fit : length; }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return 1; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t, std::uint32_t) override { return 0; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, bool, bool) override { return true; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, int, int, int) override { return true; }
    bool TextArea(const void*, std::uint32_t, cRZRect&) override { return true; }
    int GetStringWidth(const void*, std::uint32_t n) override { return int(n) * 8; }
    int GetStringVisibleWidth(const void*, std::uint32_t n, std::uint32_t& v) override { v = n; return int(n) * 8; }
    bool FormatForEllipsis(cIGZString&, const cIGZString&, const cRZRect&, bool) override { return true; }
    bool DrawTextLine(void*, int x, int y, const void* text, std::uint32_t length, std::uint32_t color, std::uint32_t,
                      const cRZRect*, cRZPoint*) override {
        drawn.push_back(std::to_string(x) + "," + std::to_string(y) + ":" + std::string(static_cast<const char*>(text), length) + ":" + std::to_string(color));
        return true; }
    bool DrawTextParagraph(void*, const cRZRect&, const void*, std::uint32_t, int, std::uint32_t, std::uint32_t, const cRZRect*, cRZPoint*) override { return true; }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; } void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { return true; } bool UnlockSurfaceForFontUse(void*) override { return true; }
};

struct Styles final : IWinStyleManager {
    FakeFont font;
    cIGZUnknown* GetFontStyle(std::uint32_t) override { return &font; }
};
struct Factory final : IWinCtrlFactory {
    bool CreateCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t, int, int, int, int) override { return nullptr; }
    std::uint32_t DefaultColor(int id) override { return 0x1000u + std::uint32_t(id & 0xff); }
    std::uint32_t DoubleClickMilliseconds() override { return 700; }
};
struct Cursors final : cIGZCursorManager {
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return 1; } std::uint32_t Release() override { return 1; }
    cIGZCursor* LoadCursor(std::uint32_t) override { return nullptr; }
    void SetCursor(cIGZCursor*) override {}
};
struct Target final : cGZWin {
    explicit Target(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct Input final : cGZWinLineInput {
    using cGZWinLineInput::cGZWinLineInput;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr;
    FakeWinGraphics graphics;
    SetWinGraphics(&graphics);
    Cursors cursors;
    mgr.cursors = &cursors;
    Styles styles;
    Factory factory;
    SetWinStyleManager(&styles);
    SetWinCtrlFactory(&factory);

    Target target(&mgr);
    auto* input = new Input(&mgr, true);
    input->AddRef();
    input->SetNotificationTarget(&target);
    input->Init();
    CHECK(input->IsEditable() && input->IsFilled());
    cRZRect minimum;
    input->GetMinMaxInfo(minimum);
    CHECK(minimum.left == 4 + 2 && minimum.top == 0 + 2 + 16 + 0 && minimum.right == 0x7fffffff);   // gutters 2/0/2/0, outline inset 2
    input->SetArea(0, 0, 100, 4);
    CHECK(input->GetH() == 18 && input->GetW() == 100);    // clamped to the minimum height

    // typing
    for (char c : std::string("hello")) CHECK(input->GZOnCharacter(c));
    cRZString text;
    CHECK(input->GetText(text) == 5 && text.Data() == std::string("hello"));
    std::uint32_t cursor = 0;
    input->GetCursorLoc(cursor);
    CHECK(cursor == 5);
    CHECK(!input->GZOnCharacter('\n'));   // control characters are refused
    CHECK(target.got.size() == 5 && target.got[0].type == kGZMsgCommand && target.got[0].param1 == kLineInputTextChanged);

    // character limit
    input->SetCharacterLimit(5);
    CHECK(!input->GZOnCharacter('x'));
    input->SetCharacterLimit(0xffffffffu);

    // navigation
    input->GZOnKeyDown(0x25, 0);   // left
    input->GetCursorLoc(cursor);
    CHECK(cursor == 4);
    input->GZOnKeyDown(0x24, 0);   // home
    input->GetCursorLoc(cursor);
    CHECK(cursor == 0);
    CHECK(input->GZOnKeyDown(0x24, 0));   // already home: nothing happens
    input->GZOnKeyDown(0x27, 0);   // right
    input->GetCursorLoc(cursor);
    CHECK(cursor == 1);
    input->GZOnKeyDown(0x23, 0);   // end
    input->GetCursorLoc(cursor);
    CHECK(cursor == 5);

    // shift-left extends the selection from the anchor
    input->GZOnKeyDown(0x25, 1);
    input->GZOnKeyDown(0x25, 1);
    std::uint32_t start = 0, count = 0;
    input->GetSelection(start, count);
    CHECK(start == 5 && static_cast<int>(count) == -2);   // anchor 5, end 3 (the original keeps the signed difference)

    // backspace removes a selection
    input->SetSelection(1, 3);
    input->GetSelection(start, count);
    CHECK(start == 1 && count == 3);
    input->GZOnKeyDown(8, 0);
    CHECK(input->GetText(text) == 2 && text.Data() == std::string("ho"));
    input->GetCursorLoc(cursor);
    CHECK(cursor == 1);

    // backspace / delete without a selection
    // (the original passes cursor - charLength as the 1-based index, so at cursor 1 nothing is erased)
    input->GZOnKeyDown(8, 0);
    CHECK(input->GetText(text) == 2 && text.Data() == std::string("ho"));
    input->GetCursorLoc(cursor);
    CHECK(cursor == 0);
    input->GZOnKeyDown(0x2e, 0);
    CHECK(input->GetText(text) == 1 && text.Data() == std::string("o"));
    input->GZOnKeyDown(0x2e, 0);
    CHECK(input->GetText(text) == 0);

    // return / tab / escape notifications
    target.got.clear();
    input->SetText("abc");
    CHECK(!target.got.empty() && target.got.back().param1 == kLineInputTextChanged);
    target.got.clear();
    input->GZOnKeyDown(0xd, 0);
    CHECK(target.got.size() == 1 && target.got[0].param1 == kLineInputReturn);
    input->GZOnKeyDown(9, 0);
    input->GZOnKeyDown(0x1b, 0);
    CHECK(target.got.size() == 3 && target.got[1].type == kGZMsgKeyDown && target.got[2].param1 == 0x1b);

    // clicking maps x to the nearest character boundary
    input->SetText("abcdef");
    input->SetArea(0, 0, 200, 18);
    CHECK(input->FindTextIndexFromCursorLocation(2 + 8 * 2 + 1) == 2);
    CHECK(input->FindTextIndexFromCursorLocation(2 + 8 * 2 + 6) == 3);
    CHECK(input->FindTextIndexFromCursorLocation(500) == 6);
    CHECK(input->FindTextIndexFromCursorLocation(0) == input->ViewStart());

    // a read-only line takes no characters
    input->SetEditable(false);
    CHECK(!input->GZOnCharacter('q'));
    input->SetEditable(true);

    // colour accessors
    CHECK(input->SetFontColor(0x123456) && input->SetFontColor(cIGZWinLineInput::kColorSelected, 7));
    std::uint32_t color = 0;
    CHECK(input->GetFontColor(cIGZWinLineInput::kColorText, color) && color == 0x123456);
    CHECK(input->ClearFontColor(cIGZWinLineInput::kColorText) && !input->GetFontColor(cIGZWinLineInput::kColorText, color));
    CHECK(!input->SetFontColor(static_cast<cIGZWinLineInput::tFontColorIdx>(3), 1));
    input->SetHighlightFillAlpha(7.0f);
    float alpha = 0;
    input->GetHighlightFillAlpha(alpha);
    CHECK(alpha == 1.0f);
    CHECK(input->SetSoundID(1, 9) && !input->SetSoundID(3, 9));

    // painting: unfocused draws the text, focused with a selection fills the highlight
    Ctx ctx;
    input->SetCtx(&ctx);
    styles.font.drawn.clear();
    CHECK(input->GZPaint());
    CHECK(!styles.font.drawn.empty() && styles.font.drawn[0].find(":abcdef:") != std::string::npos);
    mgr.focus = input;
    input->SetSelection(1, 2);
    ctx.log.clear();
    styles.font.drawn.clear();
    input->GZPaint();
    bool filled = false;
    for (const auto& line : ctx.log) filled = filled || line.rfind("fill ", 0) == 0;
    CHECK(filled && styles.font.drawn.size() == 2);
    mgr.focus = nullptr;

    input->Release();
    SetWinStyleManager(nullptr);
    SetWinCtrlFactory(nullptr);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures != 0;
}
