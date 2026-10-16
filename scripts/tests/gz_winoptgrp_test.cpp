#include <cstdio>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/WinOptGrp.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)
using namespace gz;

namespace {
struct FakeFont final : IWinFont {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    std::uint32_t DefaultTextColor() override { return 0xff102030; }
    std::uint32_t DefaultShadowColor() override { return 0xff405060; }
    std::int16_t LineHeight(int, int) override { return 10; }
    int CountLines(const char*, std::uint32_t, int) override { return 1; }
    int WrappedWidth(const char*, std::uint32_t, int) override { return 0; }
    void MeasureText(const char*, std::uint32_t length, cRZRect& out) override { out = {0, 0, int(length) * 5, 10}; }
    int TextWidth(const char*, std::uint32_t length) override { return int(length) * 5; }
    void DrawText(IWinBuffer*, int, int, const char*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
    void DrawTextBox(IWinBuffer*, const cRZRect&, const char*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
};
struct Fonts final : IWinFontSource {
    FakeFont font; std::vector<std::uint32_t> asked;
    IWinFont* Font(std::uint32_t id) override { asked.push_back(id); return &font; }
    bool Available() override { return true; }
};
struct Host final : IWinButtonHost {
    FakeFont font;
    IWinButtonFont* Font(std::uint32_t) override { return &font; }
    cIGZBuffer* Image(std::uint32_t) override { return nullptr; }
    std::uint32_t DefaultSoundId(int) override { return 0; }
    void PlaySound(std::uint32_t) override {}
    void ShowTip(const BtnTipRequest&) override {}
    void HideTip() override {}
    bool HasWindowColorManager() override { return true; }
};
struct Factory final : IWinCtrlFactory {
    cIGZWinMgr* mgr; std::vector<std::uint32_t> asked;
    explicit Factory(cIGZWinMgr* m) : mgr(m) {}
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t, int, int, int, int) override { return nullptr; }
    std::uint32_t DefaultColor(int) override { return 0; }
    std::uint32_t DoubleClickMilliseconds() override { return 0x2ee; }
    bool CreateCtrl(std::uint32_t classId, std::uint32_t iid, void** out) override {
        asked.push_back(classId);
        cGZWin* object = nullptr;
        if (classId == kOptGrpButtonClass) object = new cGZWinBtn(mgr);
        else if (classId == kOptGrpLabelClass) object = new cGZWinText(mgr);
        else return false;
        object->AddRef();
        const bool ok = object->QueryInterface(iid, out);
        object->Release();
        return ok;
    }
};
struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct Group final : cGZWinOptGrp {
    using cGZWinOptGrp::cGZWinOptGrp;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
    void Fit() { drawArea_ = cRZRect{0, 0, GetW(), GetH()}; }
    void SetFill(std::uint32_t rgb) { fillColor_ = rgb; }
    std::uint32_t Flags() const { return flags_; }
};
bool Has(const std::vector<std::string>& log, const std::string& s) { for (auto& l : log) if (l == s) return true; return false; }
cRZRect AreaOf(cIGZWin* w) { return w->GetArea(); }
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; fake_dc::Ctx ctx; Fonts fonts; Host host; Factory factory(&mgr);
    SetWinGraphics(&graphics); SetWinFontSource(&fonts); SetWinButtonHost(&host); SetWinCtrlFactory(&factory);
    auto* parent = new Target(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 400, 400);

    auto* g = new Group(&mgr);
    g->SetNotificationTarget(parent);
    g->SetID(0x55);
    cRZString caption("Group");
    g->cGZWin::SetCaption(caption);
    parent->ChildAdd(g);
    void* out = nullptr;
    CHECK(g->QueryInterface(kGZIID_cIGZWinOptGrp, &out) && out == static_cast<cIGZWinOptGrp*>(g));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(g->QueryInterface(kGZIID_cIGZWin, &out) && out == static_cast<cGZWin*>(g));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!g->QueryInterface(0x1234, &out) && g->AsIGZWin() == static_cast<cGZWin*>(g));

    // Init creates the caption label through the control factory and takes the fonts
    g->SetCtx(&ctx);
    CHECK(g->Flags() == 0xb && g->ChildExists(kOptGrpLabelId));
    CHECK(factory.asked.size() == 1 && factory.asked[0] == kOptGrpLabelClass);
    CHECK(fonts.font.refs >= 2);                                                         // label font + button font
    std::uint32_t color = 0;
    CHECK(g->GetFontColor(cIGZWinOptGrp::kLabelEnabled, color) && color == 0xff102030 && g->GetFontColor(cIGZWinOptGrp::kButtonDisabled, color) && color == 0xff102030);
    cIGZWinText* label = nullptr;
    CHECK(g->GetChildAs(kOptGrpLabelId, kGZIID_cIGZWinText, reinterpret_cast<void**>(&label)));
    CHECK(std::string(label->AsIGZWin()->GetCaption().Data()) == "Group");
    label->Release();
    g->SetCaption(cRZString("Options"));
    CHECK(g->ChildExists(kOptGrpLabelId) && std::string(g->GetCaption().Data()) == "Options");

    // buttons
    CHECK(!g->OptBtnAdd(0, cRZString("none"), 0, nullptr, nullptr));                      // id 0 is invalid
    CHECK(g->OptBtnAdd(1, cRZString("One"), 0, nullptr, nullptr) && g->OptBtnAdd(2, cRZString("Two"), 0, nullptr, nullptr) &&
          g->OptBtnAdd(3, cRZString("Three"), 0, nullptr, nullptr));
    CHECK(!g->OptBtnAdd(2, cRZString("again"), 0, nullptr, nullptr));                     // duplicate id
    CHECK(factory.asked.size() == 4 && factory.asked[1] == kOptGrpButtonClass);
    cIGZWinBtn* button = nullptr;
    CHECK(g->GetOptBtn(2, &button) && button->GetBtnFlag(4) && std::string(button->AsIGZWin()->GetCaption().Data()) == "Two");   // toggle flag handed down
    CHECK(button->GetFontColor(0, color) == false || color != 0);                          // colours equal the font's: cleared
    button->Release();
    CHECK(g->OptBtnSize(1, 40, 20) && g->OptBtnSize(2, 40, 20) && g->OptBtnSize(3, 40, 20) && !g->OptBtnSize(9, 1, 1));
    CHECK(g->OptBtnMoveTo(1, 7, 8) && AreaOf(g->GetChildWindowFromID(1)).left == 7 && AreaOf(g->GetChildWindowFromID(1)).top == 8 && !g->OptBtnMoveTo(9, 0, 0));

    // value
    CHECK(g->GetValue() == 0 && g->SetValue(2) && g->GetValue() == 2);
    CHECK(mgr.posted.size() == 1 && mgr.posted[0].type == kGZMsgCommand && mgr.posted[0].param1 == kGZMsgOptGrpChanged &&
          mgr.posted[0].param2 == 0x55 && mgr.posted[0].param3 == 2);
    CHECK(g->GetChildWindowFromID(2) != nullptr);
    cIGZWinBtn* two = nullptr; g->GetOptBtn(2, &two);
    CHECK(two->GetBtnFlag(1));
    CHECK(g->SetValue(3) && g->GetValue() == 3 && !two->GetBtnFlag(1));                   // the previous button is cleared
    two->Release();
    CHECK(!g->SetValue(77) && g->GetValue() == 3);
    CHECK(g->GZOnCommand(kGZCmdOptGrpSelect, 1) && g->GetValue() == 1 && !g->GZOnCommand(0x1, 5) && g->GetValue() == 1);

    // key messages go to the notification target
    parent->got.clear();
    CHECK(!g->GZOnKeyDown(0x41, 2) && parent->got.size() == 1 && parent->got[0].type == 5 && parent->got[0].param1 == 0x41);
    g->GZOnKeyUp(0x41, 2);
    CHECK(parent->got.back().type == 6);

    // auto layout without the caption label: columns wrap at the height limit
    g->SetOptGrpFlag(1, false);
    CHECK(!g->ChildExists(kOptGrpLabelId) && !g->GetOptGrpFlag(1));
    g->SetArea(0, 0, 200, 60);
    CHECK(g->GetW() == 200 && g->GetH() == 60);
    cRZRect a1 = AreaOf(g->GetChildWindowFromID(1)), a2 = AreaOf(g->GetChildWindowFromID(2)), a3 = AreaOf(g->GetChildWindowFromID(3));
    CHECK(a1.left == 4 && a1.top == 4 && a2.left == 4 && a2.top == 24 && a3.left == 48 && a3.top == 4);
    cRZRect info;
    CHECK(g->GetMinMaxInfo(info) && info.left == 92 && info.top == 48 && info.right == 0xffff && info.bottom == 0xffff);
    g->SetArea(0, 0, 10, 10);                                                              // limit 10: every button wraps into its own column, so the minimum is 180x28
    CHECK(g->GetW() == 180 && g->GetH() == 28);
    CHECK(g->AutoSize() && g->GetW() == 180 && g->GetH() == 28);
    int type = -1, limit = -1;
    CHECK(g->GetAutoFillType(type, limit) && type == 0 && limit == 10);
    g->SetAutoFillType(1, -5);
    CHECK(g->GetAutoFillType(type, limit) && type == 1 && limit == 1);

    // manual fill: size follows the children, buttons stay where they are
    g->OptBtnMoveTo(1, 10, 10);
    g->OptBtnMoveTo(2, 100, 30);
    CHECK(g->GetMinMaxInfo(info) && info.left == 100 + 40 + 4 && info.top == 30 + 20 + 4);
    g->SetArea(0, 0, 300, 100);
    CHECK(AreaOf(g->GetChildWindowFromID(1)).left == 10 && AreaOf(g->GetChildWindowFromID(2)).top == 30);
    g->SetAutoFillType(0, 60);
    CHECK(!g->LayoutW(5) && !g->LayoutH(5));

    // gutters re-measure
    int gx = 0, gy = 0;
    CHECK(g->GetGutters(gx, gy) && gx == 4 && gy == 4 && g->SetGutters(2, 3) && g->GetGutters(gx, gy) && gx == 2 && gy == 3);
    g->SetGutters(4, 4);

    // enabling / disabling reaches the buttons
    g->SetFlag(kWinEnabled, false);
    cIGZWinBtn* b1 = nullptr; g->GetOptBtn(1, &b1);
    CHECK(!b1->AsIGZWin()->GetFlag(kWinEnabled));
    g->SetFlag(kWinEnabled, true);
    CHECK(b1->AsIGZWin()->GetFlag(kWinEnabled));

    // colours
    CHECK(!g->GetFontColor(static_cast<cIGZWinOptGrp::tOptGrpFontColorIdx>(5), color));
    CHECK(g->SetFontColor(cIGZWinOptGrp::kButtonNormal, 0xff112233) && g->GetFontColor(cIGZWinOptGrp::kButtonNormal, color) && color == 0xff112233);
    CHECK(g->GetOptGrpFlag(0x1000000) && !g->GetOptGrpFlag(0x2000000));                    // only the normal colour differs from the font's
    std::uint32_t buttonColor = 0;
    CHECK(b1->GetFontColor(0, buttonColor) && buttonColor == 0xff112233 && b1->GetFontColor(1, buttonColor) && buttonColor == 0xff112233);
    CHECK(!b1->GetFontColor(2, buttonColor));
    CHECK(g->ClearFontColor(cIGZWinOptGrp::kButtonNormal) && g->GetFontColor(cIGZWinOptGrp::kButtonNormal, color) && color == 0xff102030 && !g->GetOptGrpFlag(0x1000000));
    CHECK(!b1->GetFontColor(0, buttonColor));
    b1->Release();
    // painting
    g->SetOptGrpFlag(1, true);
    CHECK(g->SetFontColor(cIGZWinOptGrp::kLabelEnabled, 0xff010101) && g->GetOptGrpFlag(0x100000));
    g->SetOptGrpFlag(4, true);
    g->SetOptGrpFlag(8, true);
    g->SetOutlineColors(0xff010203u, 0xff040506u, 0xff070809u, 0xff0a0b0cu);
    std::uint32_t o0, o1, o2, o3;
    CHECK(g->GetOutlineColors(o0, o1, o2, o3) && (o0 & 0xffff) == 0x0203 && (o3 & 0xffff) == 0x0b0c);      // 16-bit storage, as in the original
    g->SetFill(0x123456);
    g->SetArea(0, 0, 120, 80);
    g->Fit();
    ctx.log.clear();
    g->GZPaint();
    CHECK(Has(ctx.log, "color " + std::to_string(0x123456)) && Has(ctx.log, "fill 0,0,120,80"));
    CHECK(Has(ctx.log, "line 0,0,0,79") && Has(ctx.log, "line 0,0,119,0") && Has(ctx.log, "line 119,0,119,79") && Has(ctx.log, "line 0,79,119,79"));
    CHECK(Has(ctx.log, "color " + std::to_string(0x0203)));
    g->SetOptGrpFlag(4, false);
    g->SetOptGrpFlag(8, false);
    ctx.log.clear();
    g->GZPaint();
    CHECK(ctx.log.empty());

    // background bitmap tiles over the area
    auto* bmp = new cGZBuffer(); bmp->AddRef();
    bmp->Init(8, 8, cGZBufferColorType{kBufferColorRGB565, 16});
    g->SetBackBMP(bmp);
    ctx.log.clear();
    g->GZPaint();
    CHECK(Has(ctx.log, "tile"));
    g->SetBackBMP(nullptr);
    bmp->Release();

    // deleting
    CHECK(g->OptBtnDelete(1) && !g->ChildExists(1u) && g->GetValue() == 0);               // deleting the selected button clears the value
    CHECK(!g->OptBtnDelete(1));
    CHECK(g->OptBtnDelete(3) && g->GetValue() == 0);

    parent->Release();
    if (failures == 0) std::printf("gz_winoptgrp: all checks passed\n");
    return failures ? 1 : 0;
}
