#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinCombo.h"
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


struct Buttons final : IWinButtonHost {
    IWinButtonFont* Font(std::uint32_t) override { return nullptr; }
    cIGZBuffer* Image(std::uint32_t) override { return nullptr; }
    std::uint32_t DefaultSoundId(int) override { return 0; }
    void PlaySound(std::uint32_t) override {}
    void ShowTip(const BtnTipRequest&) override {}
    void HideTip() override {}
    bool HasWindowColorManager() override { return true; }
};
struct Host final : IWinTextEditHost {
    bool ClipboardGetText(cIGZString&) override { return false; }
    void ClipboardSetText(const cIGZString&) override {}
    std::uint32_t ModifierState() override { return 0; }
    bool CapsLockOn() override { return false; }
    bool ShiftDown() override { return false; }
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
    Host host;
    SetWinTextEditHost(&host);
    Buttons buttons;
    SetWinButtonHost(&buttons);

    Target target(&mgr);
    auto* combo = new cGZWinCombo(&mgr, false);
    combo->AddRef();
    combo->SetNotificationTarget(&target);
    combo->SetArea(0, 0, 100, 20);

    cRZString one("one"), two("two"), three("three");
    CHECK(combo->InsertString(one, -2) && combo->InsertString(three, -2) && combo->InsertString(two, 1));
    CHECK(combo->StringCount() == 3);
    CHECK(!combo->InsertString(one, -1));              // a negative index other than -2 never finds its place
    cRZString out;
    CHECK(combo->GetStringFromIndex(1, out) && std::string(out.Data()) == "two");
    CHECK(!combo->GetStringFromIndex(3, out));
    int index = -1;
    CHECK(combo->GetIndexFromString(index, three) && index == 2);
    CHECK(!combo->GetIndexFromString(index, cRZString("zzz")));
    CHECK(combo->InsertString(cRZString("zero"), 0) && combo->GetStringFromIndex(0, out) && std::string(out.Data()) == "zero");

    CHECK(combo->Init());
    cIGZWinTextEdit* edit = combo->GetTextEdit();
    CHECK(edit != nullptr && edit->AsIGZWin()->GetID() == kComboTextEditId);
    CHECK(combo->GetChildCount() == 1);
    CHECK(!combo->GetEditable());

    // The arrow takes 16 pixels, the button gutter 1: the edit fills the rest.
    CHECK(edit->AsIGZWin()->GetW() == 100 - 16 - 1 && edit->AsIGZWin()->GetH() == 20);
    cRZRect info;
    combo->GetMinMaxInfo(info);
    CHECK(info.left == 16 + 1 + 1 && info.top == 8);

    // Selection updates the text and notifies.
    target.got.clear();
    CHECK(combo->SetSelection(2, true));
    CHECK(combo->GetSelection() == 2 && std::string(edit->GetText()) == "two");
    CHECK(target.got.size() == 1 && target.got[0].type == 3 && target.got[0].param1 == kComboChanged);
    CHECK(!combo->SetSelection(9, true) && !combo->SetSelection(-1, true));
    combo->GZOnKeyDown(0x26, 0);                       // Up
    CHECK(combo->GetSelection() == 1);
    combo->GZOnKeyDown(0x28, 0);                       // Down
    CHECK(combo->GetSelection() == 2);
    combo->GZOnKeyDown(0x41, 0);                       // other keys go to the target
    CHECK(target.got.back().type == kGZMsgKeyDown && target.got.back().param1 == 0x41);

    // Drop-down: a non-editable combo opens on a click or Alt+Down and takes the popup's answer.
    mgr.popupAnswer = 3;
    combo->GZOnMouseDownL(5, 5, 0);
    CHECK(mgr.popups == 1 && combo->GetSelection() == 3 && !combo->Dropped());
    mgr.popupAnswer = 0;
    combo->GZOnKeyDown(0x28, 4);
    CHECK(mgr.popups == 2 && combo->GetSelection() == 0);
    cGZMessage click; click.type = kTextEditMsgMouseDownL;
    combo->DoMessage(click);
    CHECK(mgr.popups == 3);
    combo->SetEditable(true);
    combo->DoMessage(click);
    CHECK(mgr.popups == 3);                             // editable boxes ignore clicks on the text
    cGZMessage key; key.type = kTextEditMsgKey;
    target.got.clear();
    combo->DoMessage(key);
    CHECK(target.got.size() == 1 && target.got[0].param1 == kComboChanged);

    // Removing strings.
    CHECK(combo->RemoveString(cRZString("one")) && combo->StringCount() == 3);
    CHECK(!combo->RemoveString(cRZString("one")));
    CHECK(combo->RemoveString(0) && !combo->RemoveString(7) && combo->StringCount() == 2);
    combo->RemoveAllStrings();
    CHECK(combo->StringCount() == 0 && combo->GetSelection() == -1);

    // Colours and flags.
    combo->SetFontColor(cIGZWinCombo::kColorText, 0x123456);
    CHECK(combo->GetFontColor(cIGZWinCombo::kColorText) == 0x123456);
    std::uint32_t color = 0;
    CHECK(combo->GetFontColor(cIGZWinCombo::kColorText, color) && color == 0x123456);
    combo->ClearFontColor(cIGZWinCombo::kColorText);
    CHECK(!combo->GetFontColor(cIGZWinCombo::kColorText, color) && !combo->SetFontColor(static_cast<cIGZWinCombo::tFontColorIdx>(3), 1));
    combo->SetHighlightFillAlpha(2.0f);
    float alpha = 0; combo->GetHighlightFillAlpha(alpha);
    CHECK(alpha == 1.0f);
    combo->SetFlag(1); CHECK(combo->IsFlagSet(1) && combo->GetFlags() == 1);
    combo->ClearFlag(1); CHECK(combo->GetFlags() == 0);
    void* raw = nullptr;
    CHECK(combo->QueryInterface(kGZIID_cIGZWinCombo, &raw) && raw == static_cast<cIGZWinCombo*>(combo)); combo->Release();

    combo->Release();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
