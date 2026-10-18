#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinPieMenu.h"
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
    bool TextArea(const void*, std::uint32_t n, cRZRect& r) override { r = cRZRect{0, 0, int(n) * 8, 16}; return true; }
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
struct Menu final : cGZWinPieMenu {
    using cGZWinPieMenu::cGZWinPieMenu;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
};
}  // namespace

int main() {
    FakeMgr mgr;
    FakeWinGraphics graphics;
    SetWinGraphics(&graphics);
    Styles styles;
    SetWinStyleManager(&styles);
    Factory factory;
    SetWinCtrlFactory(&factory);
    Buttons buttons;
    SetWinButtonHost(&buttons);
    Cursors cursors;
    mgr.cursors = &cursors;

    cGZWin parentWin(&mgr);
    parentWin.SetArea(0, 0, 400, 300);
    Target target(&mgr);
    auto* menu = new Menu(&mgr);
    menu->AddRef();
    menu->SetNotificationTarget(&target);
    parentWin.ChildAdd(menu);
    menu->Init();
    CHECK(menu->Pi() > 3.14f && menu->Pi() < 3.15f);
    CHECK(menu->GetSelection() == -1 && menu->GetItemCount() == 0);
    std::uint32_t fill = 0;
    (void)fill;

    for (const char* name : {"North", "East", "South", "West"}) menu->InsertString(cRZString(name), nullptr);
    CHECK(menu->GetItemCount() == 4);
    menu->Layout();
    CHECK(menu->SpokeCount() == 4);
    // With four items the spokes are N, E, S, W (slots 0, 2, 4, 6); each label is 3 pixels wider than its text on every side.
    const PieMenuItem& north = menu->Item(0);
    const PieMenuItem& east = menu->Item(1);
    const PieMenuItem& south = menu->Item(2);
    const PieMenuItem& west = menu->Item(3);
    CHECK(north.w == 5 * 8 + 6 && north.h == 16 + 6);
    CHECK(north.y < east.y && east.y < south.y);          // top to bottom
    CHECK(west.x < north.x && north.x < east.x);          // left to right
    CHECK(menu->GetPopupCenterX() >= 4 && menu->GetPopupCenterY() >= 4);

    // CalcItem: a click point at the centre selects nothing (inside the inactive radius); far away picks by direction.
    menu->BeginPopupWindow(200, 150, 0, 1);
    CHECK(menu->GetPoppedUp());
    int cx = menu->GetPopupDownX() - menu->GetPopupX(), cy = menu->GetPopupDownY() - menu->GetPopupY();
    CHECK(menu->CalcItem(cx, cy) == -1);
    menu->EnableItem(1, false);
    CHECK(menu->CalcItem(menu->Item(1).x + 2, menu->Item(1).y + 2) != 1);   // a disabled item cannot be hit
    menu->EnableItem(1, true);
    CHECK(menu->CalcItem(menu->Item(1).x + 2, menu->Item(1).y + 2) == 1);   // a point on the label hits it

    // Selection and notifications.
    target.got.clear();
    menu->SetSelection(2, false);
    CHECK(menu->GetSelection() == 2 && target.got.size() == 1 && target.got[0].param1 == kPieMsgSelectionChanged);
    menu->SetSelection(99, false);
    CHECK(menu->GetSelection() == 3);                     // clamped to the last item
    menu->SetSelection(-5, false);
    CHECK(menu->GetSelection() == 0);
    target.got.clear();
    menu->SetSelection(1, true);
    CHECK(target.got.size() == 2 && target.got[1].param1 == kPieMsgChosen);

    // Escape cancels.
    menu->BeginPopupWindow(200, 150, 0, 1);
    target.got.clear();
    CHECK(menu->GZOnKeyDown(0x1b, 0));
    CHECK(menu->GetSelection() == -1);
    CHECK(!target.got.empty() && target.got[0].param1 == kPieMsgCancelled);
    CHECK(!menu->GZOnKeyDown('A', 0));
    CHECK(target.got.back().param1 == kPieMsgKeyDown && target.got.back().param2 == 'A');
    menu->GZOnCharacter('x');
    CHECK(target.got.back().param1 == kPieMsgCharacter);

    // Painting draws the labels.
    Ctx ctx;
    menu->SetCtx(&ctx);
    menu->SetSelection(0, false);
    styles.font.drawn.clear();
    menu->GZPaint();
    CHECK(styles.font.drawn.size() == 4);

    // A popup too close to the edge is pushed back inside the parent.
    menu->BeginPopupWindow(2, 2, 0, 1);
    CHECK(menu->GetPopupX() == 0 && menu->GetPopupY() == 0);
    menu->EndPopupWindow();
    CHECK(!menu->GetPoppedUp());

    // The event loop runs through the window manager.
    mgr.popupAnswer = 2;
    CHECK(menu->PopupEventLoop() == 2 && menu->EventLoopActive() == 1);
    menu->Clear();
    CHECK(menu->GetItemCount() == 0 && menu->GetSelection() == -1);
    cRZString title("Menu");
    menu->SetTitle(title);
    cRZString got;
    menu->GetTitle(got);
    CHECK(std::string(got.Data()) == "Menu");
    menu->SetRadii(10, 20);
    menu->SetMaxRadius(50); menu->SetAutoCancel(1); menu->SetInactiveRadius(3);
    CHECK(menu->GetMaxRadius() == 50 && menu->GetAutoCancel() == 1 && menu->GetInactiveRadius() == 3);

    void* raw = nullptr;
    CHECK(menu->QueryInterface(kGZIID_cIGZWinPieMenu, &raw) && raw == static_cast<cIGZWinPieMenu*>(menu)); menu->Release();
    parentWin.ChildRemove(menu);
    menu->Release();
    std::printf(failures ? "FAILED (%d)\n" : "OK\n", failures);
    return failures ? 1 : 0;
}
