#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinTextEdit.h"
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
    std::uint32_t CalculateWordsToFitInWidth(const void* text, std::uint32_t length, std::uint32_t width, std::uint32_t*, WordWrapMode) override {
        const char* t = static_cast<const char*>(text);
        std::uint32_t fit = width / 8;
        for (std::uint32_t i = 0; i < length && i < fit; ++i) if (t[i] == '\n') return i + 1;
        if (length <= fit) return length;
        std::uint32_t at = fit;
        while (at > 0 && t[at - 1] != ' ') --at;     // break after the last space that fits
        return at ? at : fit;
    }
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

struct Edit final : cGZWinTextEdit {
    using cGZWinTextEdit::cGZWinTextEdit;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
struct Host final : IWinTextEditHost {
    std::string clip; std::uint32_t mods = 0; bool caps = false, shift = false;
    bool ClipboardGetText(cIGZString& out) override { if (clip.empty()) return false; out.AssignFromChars(clip.data(), std::uint32_t(clip.size())); return true; }
    void ClipboardSetText(const cIGZString& t) override { clip.assign(t.Data(), t.Strlen()); }
    std::uint32_t ModifierState() override { return mods; }
    bool CapsLockOn() override { return caps; }
    bool ShiftDown() override { return shift; }
};
}  // namespace

static std::string Text(cGZWinTextEdit& e) { return std::string(e.GetText(), e.GetTextLength()); }
static void Type(cGZWinTextEdit& e, const char* s) { for (; *s; ++s) e.GZOnCharacter(*s); }

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
    Host host;
    SetWinTextEditHost(&host);

    Target target(&mgr);
    auto* edit = new Edit(&mgr);
    edit->AddRef();
    edit->SetNotificationTarget(&target);
    edit->SetArea(0, 0, 100, 48);
    edit->Init();
    CHECK(edit->GetFlags() == 0x10b);
    CHECK(edit->GetLineHeight() == 16 && edit->VisibleLines() == 3);
    CHECK(edit->GetTextLength() == 0 && edit->GetLineCount() == 0);

    // Layout: 90 px of text area = 11 glyphs per line, wrapping after spaces.
    CHECK(edit->SetText("hello world again", 0, false));
    CHECK(edit->GetLineCount() == 2);
    CHECK(edit->LineStarts()[0] == 0 && edit->LineStarts()[1] == 6);
    CHECK(edit->GetCharacterCountOfLine(0) == 6 && edit->GetCharacterCountOfLine(1) == 11);
    std::uint32_t line = 9, column = 9;
    edit->GetLineAndColumnFromIndex(line, column, 14);
    CHECK(line == 1 && column == 8);
    std::uint32_t index = 0;
    CHECK(edit->GetIndexFromLineAndColumn(1, 8, index) && index == 14);
    CHECK(!edit->GetIndexFromLineAndColumn(1, 50, index) && index == 17);
    int x = 0, y = 0;
    edit->GetPositionFromLineAndColumn(1, 2, x, y);
    CHECK(x == 5 + 16 && y == 16);
    edit->GetLineAndColumnFromPosition(line, column, 5 + 17, 20);
    CHECK(line == 1 && column == 2);     // nearer to the edge after two glyphs

    // Editing: typing, backspace, delete, undo / redo.
    edit->SetText("", 0, false);
    edit->SetFocus();
    Type(*edit, "abc");
    CHECK(Text(*edit) == "abc" && edit->Position().index == 3);
    CHECK(edit->UndoDepth() == 3);
    edit->GZOnKeyDown(8, 0);
    CHECK(Text(*edit) == "ab" && edit->Position().index == 2);
    edit->GZOnKeyDown(0x25, 0);                     // Left
    CHECK(edit->Position().index == 1);
    edit->GZOnKeyDown(0x2e, 0);                     // Delete removes 'b'
    CHECK(Text(*edit) == "a");
    CHECK(edit->Undo() && Text(*edit) == "ab");
    CHECK(edit->Undo() && Text(*edit) == "abc");
    CHECK(edit->Redo() && Text(*edit) == "ab");
    CHECK(edit->RedoDepth() == 1);
    Type(*edit, "z");                               // a new edit clears the redo list
    CHECK(edit->RedoDepth() == 0);

    // Selection, replace, overwrite.
    edit->SetText("hello", 0, false);
    edit->SetSelection(1, 3);
    CHECK(edit->Position().index == 3);
    Type(*edit, "x");
    host.shift = true;   // without shift or caps lock an upper-case letter is typed as lower case
    edit->SetText("hello", 0, false);
    edit->SetSelection(1, 3);
    Type(*edit, "X");
    host.shift = false;
    CHECK(Text(*edit) == "hXlo");
    edit->SetText("abcdef", 0, false);
    edit->SetInsertionIndex(2);
    edit->SetInsertMode(false);
    CHECK(!edit->GetInsertMode());
    host.shift = true;
    Type(*edit, "Z");
    host.shift = false;
    CHECK(Text(*edit) == "abZdef");
    edit->SetInsertMode(true);

    // Navigation keys.
    edit->SetText("one\ntwo\nthree", 0, false);
    CHECK(edit->GetLineCount() == 3);
    edit->SetInsertionIndex(0);
    edit->GZOnKeyDown(0x28, 0);                     // Down
    CHECK(edit->Position().line == 1);
    edit->GZOnKeyDown(0x23, 0);                     // End
    CHECK(edit->Position().column <= 4);
    edit->GZOnKeyDown(0x26, 1);                     // Shift+Up selects
    CHECK(edit->Position().line == 0 && edit->Position().selectionA != edit->Position().selectionB);
    edit->GZOnKeyDown(0x24, 0);                     // Home
    CHECK(edit->Position().column == 0);
    host.mods = 0xc0;
    edit->GZOnKeyDown(0x23, 0);                     // Ctrl+End
    CHECK(edit->Position().index == 13);
    host.mods = 0;

    // Clipboard: the field must allow it (flag 0x1000).
    edit->SetFlag(kTEClipboard);
    edit->SetText("copy me", 0, false);
    edit->SetSelection(0, 4);
    host.mods = 0xc0;
    edit->GZOnKeyDown(0x43, 0);                     // Ctrl+C
    CHECK(host.clip == "copy");
    edit->GZOnKeyDown(0x58, 0);                     // Ctrl+X
    CHECK(Text(*edit) == " me");
    edit->SetInsertionIndex(3);
    host.clip = "!!";
    edit->GZOnKeyDown(0x56, 0);                     // Ctrl+V
    CHECK(Text(*edit) == " me!!");
    edit->GZOnKeyDown(0x5a, 0);                     // Ctrl+Z
    CHECK(Text(*edit) == " me");
    host.mods = 0;

    // Single line: newlines are stripped, Return notifies.
    edit->SetFlags(kTEEditable | kTESingleLine | kTEReturnNotify | kTEUndo);
    CHECK(!edit->IsFlagSet(kTEWrap) && edit->IsFlagSet(kTESingleLine));
    edit->SetText("a\nb", 0, false);
    CHECK(Text(*edit) == "ab");
    target.got.clear();
    edit->GZOnKeyDown(0xd, 0);
    CHECK(target.got.size() == 1 && target.got[0].type == 0x9b1552d5 && target.got[0].param2 == 0xd);
    edit->SetFlags(kTEEditable | kTEPassword | kTEUndo);
    CHECK(edit->IsFlagSet(kTESingleLine) && !edit->IsFlagSet(kTEWrap));

    // Limits and removal helpers.
    edit->SetFlags(0x10b);
    edit->SetMaximumTextToAllow(5);
    edit->SetText("abcdefgh", 0, false);
    CHECK(Text(*edit) == "abcde");
    edit->SetMaximumTextToAllow(0xffffffffu);
    edit->SetText("abcdef", 0, false);
    CHECK(edit->RemoveText(1, 2, nullptr) == 2 && Text(*edit) == "adef");
    cRZString removed;
    CHECK(edit->RemoveChar(0, true, &removed) && removed.Strlen() == 1 && Text(*edit) == "def");
    CHECK(edit->FindText("ef", 2, 0, true) == 1 && edit->FindText("zz", 2, 0, true) == 0xffffffffu);
    CHECK(edit->InsertText("XY", 2, 1) == 2 && Text(*edit) == "dXYef");
    edit->RemoveText(0, 0, 0, 3, nullptr);
    CHECK(Text(*edit) == "ef");

    // Painting does not crash and draws the text.
    Ctx ctx;
    edit->SetCtx(&ctx);
    edit->SetText("paint me", 0, false);
    edit->GZPaint();
    CHECK(!styles.font.drawn.empty());

    // Sounds and caption.
    edit->SetSoundID(1, 77);
    CHECK(!edit->SetSoundID(4, 1));
    cRZString caption("caption");
    edit->SetCaption(caption);
    CHECK(Text(*edit) == "caption" && std::string(edit->GetCaption().Data()) == "caption");

    edit->Release();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
