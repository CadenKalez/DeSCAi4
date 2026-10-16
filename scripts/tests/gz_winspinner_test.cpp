#include <algorithm>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/WinSpinner.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct FakeFont final : IWinFont {
    int refs = 0; std::vector<std::string> drawn;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t DefaultTextColor() override { return 0; }
    std::uint32_t DefaultShadowColor() override { return 0; }
    std::int16_t LineHeight(int, int) override { return 10; }
    int CountLines(const char*, std::uint32_t, int) override { return 1; }
    int WrappedWidth(const char*, std::uint32_t, int) override { return 0; }
    void MeasureText(const char*, std::uint32_t, cRZRect&) override {}
    int TextWidth(const char*, std::uint32_t) override { return 0; }
    void DrawText(IWinBuffer*, int, int, const char*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
    void DrawTextBox(IWinBuffer*, const cRZRect& box, const char* text, std::uint32_t, std::uint32_t align, std::uint32_t c1, std::uint32_t c2, const cRZRect*, int) override {
        drawn.push_back(std::string(text) + " " + std::to_string(box.left) + "," + std::to_string(box.top) + "," + std::to_string(box.right) + "," +
                        std::to_string(box.bottom) + " a" + std::to_string(align) + " " + std::to_string(c1) + "/" + std::to_string(c2));
    }
};
struct Fonts final : IWinFontSource {
    FakeFont font; std::vector<std::uint32_t> asked;
    IWinFont* Font(std::uint32_t id) override { asked.push_back(id); return &font; }
    bool Available() override { return true; }
};
struct Images final : IWinImageSource {
    cIGZBuffer* skin = nullptr; std::vector<std::uint32_t> asked;
    cIGZBuffer* GetImage(std::uint32_t id) override { asked.push_back(id); return skin; }
};
struct Formatter final : IWinNumberFormatter {
    bool Available() override { return true; }
    void FormatA(int v, cRZString& out) override { out.Sprintf("A%d", v); }
    void FormatB(int v, cRZString& out) override { out.Sprintf("B%d", v); }
};
struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct Spin final : cGZWinSpinner {
    using cGZWinSpinner::cGZWinSpinner;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
    void Fit() { drawArea_ = cRZRect{0, 0, GetW(), GetH()}; }
    int BtnW() const { return buttonW_; }
    int BtnH() const { return buttonH_; }
    std::uint32_t Pressed() const { return pressed_; }
    std::uint32_t Hovered() const { return hovered_; }
    std::uint32_t Delay() const { return repeatDelay_; }
    void SetElapsed(std::uint64_t ms) { timer_.SetElapsedTimeU64(ms); }
    void SetFill(std::uint32_t rgb) { fillColor_ = rgb; }
    const char* Text() const { return caption_.Data(); }
};
cGZBuffer* MakeImage(int w, int h) {
    auto* b = new cGZBuffer(); b->AddRef();
    b->Init(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), cGZBufferColorType{kBufferColorRGB565, 16});
    return b;
}
bool Has(const std::vector<std::string>& log, const std::string& s) { return std::find(log.begin(), log.end(), s) != log.end(); }
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; fake_dc::Ctx ctx; Fonts fonts; Images images; Formatter formatter;
    SetWinGraphics(&graphics); SetWinFontSource(&fonts); SetWinImageSource(&images); SetWinNumberFormatter(&formatter);
    auto* parent = new Target(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 300, 300);

    // ---- type 0, flat buttons
    auto* s = new Spin(&mgr);
    s->SetID(0x77);
    s->SetNotificationTarget(parent);
    parent->ChildAdd(s);
    s->SetCtx(&ctx);
    s->SetFlag(kWinEnabled, true);
    void* out = nullptr;
    CHECK(s->QueryInterface(kGZIID_cIGZWinSpinner, &out) && out == static_cast<cIGZWinSpinner*>(s));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(s->QueryInterface(kGZIID_cIGZWin, &out) && out == static_cast<cGZWin*>(s));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!s->QueryInterface(0x1234, &out) && s->AsIGZWin() == static_cast<cGZWin*>(s));
    CHECK(images.asked.size() == 1 && images.asked[0] == kSpinnerDefaultImageId && fonts.asked.empty());   // type 0 wants no font
    CHECK(std::string(s->Text()) == "0");

    s->SetArea(0, 0, 20, 30);
    s->Fit();
    CHECK(s->GetW() == 20 && s->GetH() == 30 && s->BtnW() == 20 && s->BtnH() == 15);
    s->SetArea(0, 0, 3, 3);
    CHECK(s->GetW() == 8 && s->GetH() == 8 && s->BtnW() == 8 && s->BtnH() == 4);       // never below the minimum button size
    s->SetArea(0, 0, 20, 30);
    s->Fit();

    CHECK(s->SetMinMax(0, 10) && s->SetStepSize(3));
    int lo = -1, hi = -1, step = 0;
    CHECK(s->GetMinMax(lo, hi) && lo == 0 && hi == 10 && s->GetStepSize(step) && step == 3);
    s->SetValue(5);
    CHECK(s->GetValue() == 5 && std::string(s->Text()) == "5");
    CHECK(parent->got.size() == 1 && parent->got[0].type == kGZMsgSpinnerChanged && parent->got[0].param1 == 0x77 && parent->got[0].param2 == 5);
    s->SetValue(50);
    CHECK(s->GetValue() == 10 && parent->got.size() == 2 && parent->got[1].param2 == 10);
    s->SetValue(99);
    CHECK(parent->got.size() == 2);                                                    // clamped to the same value: no message

    // keys
    CHECK(s->GZOnKeyDown(0x24, 0) && s->GetValue() == 0);
    CHECK(s->GZOnKeyDown(0x26, 0) && s->GetValue() == 3);
    CHECK(s->GZOnKeyDown(0x28, 0) && s->GetValue() == 0);
    CHECK(s->GZOnKeyDown(0x28, 0) && s->GetValue() == 0);
    CHECK(s->GZOnKeyDown(0x23, 0) && s->GetValue() == 10);
    const size_t before = parent->got.size();
    CHECK(!s->GZOnKeyDown(0x41, 2) && parent->got.size() == before + 1 && parent->got.back().type == 5 &&
          parent->got.back().param1 == 0x41 && parent->got.back().param2 == 2);
    CHECK(!s->GZOnKeyUp(0x41, 2) && parent->got.back().type == 6);

    // mouse: upper half raises, lower half lowers, release drops the capture
    s->SetValue(5);
    CHECK(s->GZOnMouseDownL(5, 5, 0) && s->Pressed() == 1 && s->GetValue() == 8 && s->Delay() == 400);
    CHECK(s->GZOnMouseUpL(5, 5, 0) && s->Pressed() == 0);
    CHECK(s->GZOnMouseDownL(5, 20, 0) && s->Pressed() == 2 && s->GetValue() == 5);
    s->GZOnMouseUpL(5, 20, 0);
    CHECK(s->GZOnMouseDownL(100, 5, 0) && s->Pressed() == 0 && s->GetValue() == 5);    // outside the window
    CHECK(s->GZOnMouseMove(5, 20, 0) && s->Hovered() == 2 && mgr.capture == s);
    CHECK(s->GZOnMouseMove(5, 5, 0) && s->Hovered() == 1 && mgr.capture == s);
    CHECK(s->GZOnMouseMove(100, 5, 0) && s->Hovered() == 0 && mgr.capture == nullptr);

    // painting + auto repeat
    ctx.log.clear();
    s->GZOnMouseMove(5, 5, 0);
    s->GZOnMouseDownL(5, 5, 0);                                                         // value 5 -> 8
    CHECK(s->GetValue() == 8);
    s->SetElapsed(10);
    s->GZPaint();
    CHECK(s->GetValue() == 8 && s->Delay() == 400);                                    // not yet
    s->SetElapsed(1000);
    s->GZPaint();
    CHECK(s->GetValue() == 10 && s->Delay() == 350);                                   // 8 + 3 clamps to 10, delay shrinks
    s->GZOnMouseMove(5, 20, 0);                                                         // pointer moved to the other button: delay resets
    CHECK(s->Delay() == 400);
    s->SetElapsed(1000);
    s->GZPaint();
    CHECK(s->GetValue() == 10);                                                         // pressed 1 but hovering 2: no repeat
    s->GZOnMouseUpL(5, 20, 0);
    CHECK(s->GZOnCaptureChanged(nullptr, nullptr) && s->Pressed() == 0);
    s->GZOnMouseDownL(5, 20, 0);
    CHECK(s->GZOnCaptureChanged(nullptr, s) && s->Pressed() == 2);                      // capture came to us: keep pressing
    CHECK(s->GZOnCaptureChanged(nullptr, parent) && s->Pressed() == 0);
    s->GZOnMouseUpL(5, 20, 0);

    // ---- spinner type, flags, colours
    CHECK(!s->SetSpinnerType(static_cast<cIGZWinSpinner::tSpinnerType>(3)));
    CHECK(s->SetSpinnerType(cIGZWinSpinner::kButtonsRight));
    cIGZWinSpinner::tSpinnerType type;
    CHECK(s->GetSpinnerType(type) && type == cIGZWinSpinner::kButtonsRight);
    CHECK(s->SetFont(&fonts.font) && fonts.font.refs == 1);
    s->SetFont(&fonts.font);
    CHECK(fonts.font.refs == 1);
    s->SetArea(0, 0, 40, 30);
    s->Fit();
    CHECK(s->BtnH() == 15 && s->BtnW() == 15);
    s->SetArea(0, 0, 4, 4);
    CHECK(s->GetW() == 16 && s->GetH() == 12 && s->BtnH() == 6 && s->BtnW() == 8);
    s->SetArea(0, 0, 40, 60);
    CHECK(s->BtnH() == 30 && s->BtnW() == 16);                                          // limited to the largest flat button
    s->SetArea(0, 0, 40, 30);
    s->Fit();

    std::uint32_t alignment = 9;
    CHECK(s->GetTextAlignment(alignment) && alignment == 2 && s->SetTextAlignment(1) && !s->SetTextAlignment(3));
    CHECK(s->GetTextAlignment(alignment) && alignment == 1);
    std::uint8_t gx = 0, gy = 0;
    CHECK(s->GetGutters(gx, gy) && gx == 4 && gy == 1);
    cRZColor color;
    CHECK(!s->GetFontColor(cIGZWinSpinner::kColorNormal, color));
    std::uint32_t native = 0;
    CHECK(!s->GetFontColor(cIGZWinSpinner::kColorNormal, native));
    CHECK(s->SetFontColor(cIGZWinSpinner::kColorNormal, cRZColor{0x112233}) && s->GetFontColor(cIGZWinSpinner::kColorNormal, color) && color.rgb == 0x112233);
    CHECK(s->GetFontColor(cIGZWinSpinner::kColorNormal, native) && native == 0xff112233u);
    CHECK(s->SetFontColor(cIGZWinSpinner::kColorHover, 0xff445566u) && s->GetFontColor(cIGZWinSpinner::kColorHover, color) && color.rgb == 0xff445566u);

    s->GZOnMouseMove(100, 100, 0);
    fonts.font.drawn.clear();
    s->SetValue(7);
    s->GZPaint();
    CHECK(fonts.font.drawn.size() == 1);
    CHECK(fonts.font.drawn[0] == "7 4,10,17,20 a1 " + std::to_string(0xff112233u) + "/134217728");   // box left of the right-hand buttons
    s->GZOnMouseMove(35, 5, 0);                                                         // over the buttons: hover colour
    fonts.font.drawn.clear();
    s->GZPaint();
    CHECK(fonts.font.drawn[0].find(std::to_string(0xff445566u) + "/134217728") != std::string::npos);
    s->SetFlag(kWinEnabled, false);
    s->ClearFontColor(cIGZWinSpinner::kColorDisabled);
    fonts.font.drawn.clear();
    s->GZPaint();
    CHECK(fonts.font.drawn.size() == 1);                                                // disabled colour was stored converted, so it is "set"
    s->SetFlag(kWinEnabled, true);

    // hit testing for the buttons on the right
    std::uint32_t p = 9;
    CHECK(s->GZOnMouseMove(10, 5, 0) && s->Hovered() == 0);
    CHECK(s->GZOnMouseMove(30, 5, 0) && s->Hovered() == 1);
    CHECK(s->GZOnMouseMove(30, 25, 0) && s->Hovered() == 2);
    (void)p;
    s->GZOnMouseMove(100, 100, 0);

    // outline + opaque background
    ctx.log.clear();
    s->SetSpinnerFlag(kSpinnerFlagOutline, true);
    s->SetSpinnerFlag(kSpinnerFlagOpaque, true);
    CHECK(s->GetSpinnerFlag(kSpinnerFlagOutline) && s->GetSpinnerFlag(kSpinnerFlagOpaque) && s->GetSpinnerFlag(kSpinnerFlagShowValue));
    s->SetOutlineColors(0xff010203u, 0xff040506u, 0xff070809u, 0xff0a0b0cu);
    std::uint32_t o0, o1, o2, o3;
    CHECK(s->GetOutlineColors(o0, o1, o2, o3) && o0 == 0xff010203u && o3 == 0xff0a0b0cu);
    CHECK(s->SetOutlineColor(cIGZWinSpinner::kOutlineTop, cRZColor{0x777777}) && s->GetOutlineColor(cIGZWinSpinner::kOutlineTop, color) && color.rgb == 0x777777);
    s->SetFill(0x123456);
    s->GZPaint();
    CHECK(Has(ctx.log, "line 0,0,0,29") && Has(ctx.log, "line 0,0,20,0") && Has(ctx.log, "line 20,0,20,29") && Has(ctx.log, "line 0,29,20,29"));
    CHECK(Has(ctx.log, "fill 1,1,20,29") && Has(ctx.log, "color " + std::to_string(0x123456)));

    // formatted captions
    s->SetValue(3);
    s->SetSpinnerFlag(kSpinnerFlagFormatA, true);
    CHECK(std::string(s->Text()) == "A3");
    s->SetSpinnerFlag(kSpinnerFlagFormatB, true);
    CHECK(!s->GetSpinnerFlag(kSpinnerFlagFormatA) && std::string(s->Text()) == "B3");
    s->SetSpinnerFlag(kSpinnerFlagFormatA, true);
    CHECK(!s->GetSpinnerFlag(kSpinnerFlagFormatB) && std::string(s->Text()) == "A3");
    s->SetSpinnerFlag(kSpinnerFlagFormatA, false);
    s->SetValue(4);
    CHECK(std::string(s->Text()) == "4");
    s->SetSpinnerFlag(kSpinnerFlagShowValue, false);
    s->SetValue(6);
    CHECK(std::string(s->Text()) == "4");                                               // caption no longer follows the value

    // font style change after init asks the font source
    fonts.asked.clear();
    CHECK(s->SetFontStyle(0x1234) && fonts.asked.size() == 1 && fonts.asked[0] == 0x1234);
    std::uint32_t style = 0;
    CHECK(s->GetFontStyle(style) && style == 0x1234);

    // ---- image-skinned spinner (4 columns x 2 rows of 16x16 cells)
    cGZBuffer* skin = MakeImage(64, 32);
    images.skin = skin;
    auto* k = new Spin(&mgr);
    parent->ChildAdd(k);
    k->SetCtx(&ctx);
    k->SetFlag(kWinEnabled, true);
    cIGZBuffer* image = nullptr;
    CHECK(k->GetButtonImage(image) && image == skin);
    CHECK(k->BtnW() == 16 && k->BtnH() == 16);
    k->SetArea(0, 0, 20, 40);
    k->Fit();
    CHECK(k->GetW() == 20 && k->GetH() == 40);
    k->SetArea(0, 0, 4, 4);
    CHECK(k->GetW() == 16 && k->GetH() == 32);                                          // never smaller than one button pair
    k->SetArea(0, 0, 20, 40);
    k->Fit();
    ctx.log.clear();
    k->GZPaint();
    CHECK(ctx.log.size() == 2 && ctx.log[0] == "blt 16,0,32,16>0,2,16,18" && ctx.log[1] == "blt 16,16,32,32>0,22,16,38");
    k->GZOnMouseMove(5, 5, 0);
    ctx.log.clear();
    k->GZPaint();
    CHECK(ctx.log[0] == "blt 48,0,64,16>0,2,16,18" && ctx.log[1] == "blt 16,16,32,32>0,22,16,38");   // hovered: the third cell
    k->SetMinMax(0, 100);
    k->GZOnMouseDownL(5, 5, 0);
    ctx.log.clear();
    k->GZPaint();
    CHECK(ctx.log[0] == "blt 32,0,48,16>0,2,16,18" && ctx.log[1] == "blt 16,16,32,32>0,22,16,38");   // pressed: the second cell
    k->GZOnMouseUpL(5, 5, 0);
    k->SetFlag(kWinEnabled, false);
    ctx.log.clear();
    k->GZPaint();
    CHECK(ctx.log[0] == "blt 0,0,16,16>0,2,16,18" && ctx.log[1] == "blt 0,16,16,32>0,22,16,38");

    // explicit image area (the left half of a 64x32 sheet, 8x16 cells)
    CHECK(!k->GetSpinnerFlag(kSpinnerFlagImageArea));
    const cRZRect area{0, 0, 32, 32};
    CHECK(k->SetButtonImageArea(area) && !k->GetSpinnerFlag(kSpinnerFlagImageArea) && k->BtnW() == 16);   // ignored until flagged
    k->SetSpinnerFlag(kSpinnerFlagImageArea, true);
    CHECK(k->BtnW() == 8 && k->BtnH() == 16);
    cRZRect got;
    CHECK(k->GetButtonImageArea(got) && got.right == 32 && got.bottom == 32);

    parent->Release();
    skin->Release();
    CHECK(fonts.font.refs == 0 || fonts.font.refs == 1);
    if (failures == 0) std::printf("gz_winspinner: all checks passed\n");
    return failures ? 1 : 0;
}
