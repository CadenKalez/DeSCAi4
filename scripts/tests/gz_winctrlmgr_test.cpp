#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/WinCtrlMgr.h"

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
    std::uint32_t DefaultShadowColor() override { return 0; }
    std::int16_t LineHeight(int, int) override { return 10; }
    int CountLines(const char*, std::uint32_t, int) override { return 1; }
    int WrappedWidth(const char*, std::uint32_t, int) override { return 0; }
    void MeasureText(const char*, std::uint32_t length, cRZRect& out) override { out = {0, 0, int(length) * 5, 10}; }
    int TextWidth(const char*, std::uint32_t length) override { return int(length) * 5; }
    void DrawText(IWinBuffer*, int, int, const char*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
    void DrawTextBox(IWinBuffer*, const cRZRect&, const char*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
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
struct Sound final : IWinSoundSettings {
    int volume = 5; std::map<int, std::uint32_t> ids;
    void SetVolume(int v) override { volume = v; }
    int GetVolume() override { return volume; }
    std::uint32_t SoundId(int w) override { return ids[w]; }
    void SetSoundId(int w, std::uint32_t id) override { ids[w] = id; }
};
struct Creator final : cIGZCtrlCreator {
    int refs = 1; bool accept = true; int inits = 0, shutdowns = 0; std::vector<std::uint32_t> asked; bool answers = false;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid != kGZIID_cIGZCtrlCreator) return false;
        *out = static_cast<cIGZCtrlCreator*>(this); AddRef(); return true;
    }
    std::uint32_t AddRef() override { return ++refs; }
    std::uint32_t Release() override { return --refs; }
    bool Initialize() override { ++inits; return accept; }
    void Shutdown() override { ++shutdowns; }
    bool CreateCtrl(std::uint32_t classId, std::uint32_t, void**) override { asked.push_back(classId); return answers; }
};
struct Com final : IWinComSource {
    std::vector<std::uint32_t> asked;
    bool GetCOMObject(std::uint32_t classId, std::uint32_t, void**) override { asked.push_back(classId); return false; }
};
cGZBuffer* MakeImage(int w, int h) {
    auto* b = new cGZBuffer(); b->AddRef();
    b->Init(static_cast<std::uint32_t>(w), static_cast<std::uint32_t>(h), cGZBufferColorType{kBufferColorRGB565, 16});
    return b;
}
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; Sound sound; Host host; Com com; FakeFont font, boldFont;
    SetWinGraphics(&graphics); SetWinSoundSettings(&sound); SetWinButtonHost(&host); SetWinComSource(&com);
    auto* m = new cGZWinCtrlMgr(&mgr);
    m->AddRef();

    void* out = nullptr;
    CHECK(m->QueryInterface(kGZIID_cIGZWinCtrlMgr, &out) && out == static_cast<cIGZWinCtrlMgr*>(m));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(m->QueryInterface(kGZIID_cIGZSystemService, &out) && out == static_cast<cIGZSystemService*>(m));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(m->GetServiceID() == kGZSID_cGZWinCtrlMgr);

    // default colours come with Init
    CHECK(m->DefaultColor(0x28963f93) == 0);                                              // nothing before Init
    CHECK(m->Init() && m->Init());
    CHECK(m->DefaultColor(0x28963f93) == 0xffc0c0c0u && m->DefaultColor(0xe8963f95) == 0xffff0000u);
    std::uint8_t r = 9, g = 9, b = 9;
    m->DefaultColor(0x48963f84, r, g, b);
    CHECK(r == 0x80 && g == 0x80 && b == 0x40);
    m->DefaultColor(0x12345, r, g, b);
    CHECK(r == 0 && g == 0 && b == 0 && m->DefaultColor(0x12345) == 0);
    m->SetDefaultColor(0x12345, 1, 2, 3);
    CHECK(m->DefaultColor(0x12345) == 0xff010203u);
    m->SetDefaultColor(0x12345, 0xff0a0b0cu);
    m->DefaultColor(0x12345, r, g, b);
    CHECK(r == 0x0a && g == 0x0b && b == 0x0c);

    // fonts and bitmaps are reference counted
    CHECK(m->SystemFont(1) == nullptr);
    m->SetSystemFont(1, &font);
    CHECK(m->SystemFont(1) == &font && font.refs == 1);
    m->SetSystemFont(1, &boldFont);
    CHECK(m->SystemFont(1) == &boldFont && font.refs == 0 && boldFont.refs == 1);
    m->SetSystemFont(1, nullptr);
    CHECK(m->SystemFont(1) == nullptr && boldFont.refs == 0);
    auto* image = MakeImage(8, 8);
    m->SetSystemBMP(2, image);
    CHECK(m->SystemBMP(2) == image && image->Release() == 1);
    m->SetSystemBMP(2, nullptr);

    // texts, font mapping, sound and the double click time
    cRZString* text = new cRZString("hello");
    text->AddRef();
    m->SetDefaultText(3, text);
    CHECK(m->DefaultText(3) == text && m->DefaultText(4) == nullptr);
    cRZString* name = new cRZString("Arial"); name->AddRef();
    m->SetFontMapping(7, name);
    cRZString probe("arial");
    CHECK(m->GetFontMapping(7) == name && m->GetFontMappingID(name) == 7 && m->GetFontMappingID(&probe) == 0);   // exact match only
    CHECK(m->GetDoubleClickTimeMilliseconds() == 0x2ee);
    m->SetDoubleClickTimeMilliseconds(100);
    CHECK(m->GetDoubleClickTimeMilliseconds() == 100);
    m->SetSystemSndVol(11);
    CHECK(m->GetSystemSndVol() == 11);
    m->SetSystemSndID(2, 0x99);
    CHECK(m->SystemSndID(2) == 0x99);

    // stock fonts / images for the Create* functions
    m->SetSystemFont(0x68963c4c, &font);
    m->SetSystemFont(static_cast<int>(0xc8963c5c), &boldFont);
    m->SetSystemFont(0x68963c54, &font);

    // line input: raised outline in the shadow / light colours, editable as asked
    cIGZWinLineInput* line = m->CreateLineInput(9, false);
    cIGZWinLineInput::tOutlineType lineOutline{};
    std::uint32_t lineShadow = 0, lineLight = 0;
    CHECK(line && line->AsIGZWin()->GetID() == 9 && !line->IsEditable());
    CHECK(line->GetOutlineType(lineOutline) && lineOutline == cIGZWinLineInput::kOutlineRaised);
    CHECK(line->GetOutlineColor(cIGZWinLineInput::kOutlineColor0, lineShadow) && line->GetOutlineColor(cIGZWinLineInput::kOutlineColor1, lineLight));
    CHECK(lineShadow != lineLight);
    line->Release();

    // buttons
    cRZString caption("Go");
    cIGZWinBtn* btn = m->CreateButton(5, caption, nullptr, nullptr, 0x41);
    CHECK(btn && btn->AsIGZWin()->GetID() == 5 && std::string(btn->AsIGZWin()->GetCaption().Data()) == "Go" && btn->AsIGZWin()->GetKeyEquivalent() == 0x41);
    CHECK(btn->AsIGZWin()->GetFillColor() == 0xffc0c0c0u);
    CHECK(!com.asked.empty() && com.asked.back() == 0x22ecfc47);                          // the COM source was asked first
    btn->Release();
    cIGZWinBtn* toggle = m->CreateToggleButton(6, caption, nullptr, nullptr, 0);
    CHECK(toggle->GetStyle() == 3);
    toggle->Release();
    cIGZWinBtn* radio = m->CreateRadioButton(7, caption, nullptr, nullptr, 0);
    CHECK(radio->GetStyle() == 4);
    radio->Release();

    // label
    cIGZWinText* label = m->CreateLabel(8, caption);
    std::uint32_t textColor = 0; std::uint32_t alignment = 9;
    CHECK(label->AsIGZWin()->GetID() == 8 && label->GetTextColor(cIGZWinText::kColorText, textColor) && textColor == 0xff102030 && label->GetAlignment(alignment) && alignment == 1);
    CHECK(!label->IsBackgroundOpaque() && !label->GetWinTextFlag(2));
    label->Release();

    // sliders, scrollbars, spinners
    cIGZWinSlider* slider = m->CreateSlider(9, 1, 10, 50, 30);
    CHECK(slider->GetMinimumValue() == 10 && slider->GetMaximumValue() == 50 && slider->GetValue() == 30);
    tSliderWindowDirection dir; slider->GetDirection(dir);
    CHECK(dir == kSliderHorizontal && slider->AsIGZWin()->GetID() == 9);
    slider->Release();
    cIGZWinScrollbar* bar = m->CreateScrollbar(10, 2, 0, 100, 20);
    int lo = 0, hi = 0;
    CHECK(bar->GetValue() == 20 && bar->GetMinMax(lo, hi) && lo == 0 && hi == 100 && bar->GetOrientation() == 2 && bar->AsIGZWin()->GetID() == 10);
    bar->Release();
    cIGZWinSpinner* spinner = m->CreateSpinner(11, 1, -5, 5, 2, 3);
    int step = 0;
    cIGZWinSpinner::tSpinnerType type;
    CHECK(spinner->GetValue() == 3 && spinner->GetStepSize(step) && step == 2 && spinner->GetSpinnerType(type) && type == cIGZWinSpinner::kButtonsRight && spinner->AsIGZWin()->GetID() == 11);
    spinner->Release();

    // option group
    cRZRect area{0, 0, 100, 60};
    cIGZWinOptGrp* group = m->CreateOptGrp(12, caption, &area);
    int fillType = -1, fillHeight = -1;
    CHECK(group->AsIGZWin()->GetID() == 12 && group->GetOptGrpFlag(1) && !group->GetOptGrpFlag(4) && group->GetOptGrpFlag(8));
    CHECK(group->GetAutoFillType(fillType, fillHeight) && fillType == 0 && fillHeight == 60);
    group->Release();
    group = m->CreateOptGrp(13, caption, nullptr);
    CHECK(group->GetAutoFillType(fillType, fillHeight) && fillHeight == 0x80);
    group->Release();

    // bitmap window
    auto* pic = MakeImage(16, 8);
    cIGZWinBMP* bmp = m->CreateWinBMP(14, pic, true);
    CHECK(bmp->AsIGZWin()->GetW() == 16 && bmp->AsIGZWin()->GetH() == 8);
    bmp->Release();
    bmp = m->CreateWinBMP(15, pic, false);
    bmp->Release();
    pic->Release();

    // creators: newest first, then the default creation
    Creator first, second;
    CHECK(m->RegisterCtrlCreator(&first) && m->RegisterCtrlCreator(&second));
    CHECK(first.inits == 1 && first.refs == 2);
    second.accept = false;
    Creator third; third.accept = false;
    CHECK(!m->RegisterCtrlCreator(&third) && third.inits == 1);
    void* made = nullptr;
    first.answers = false; second.answers = true;
    CHECK(m->CreateCtrl(0xdead, 1, &made) && second.asked.size() == 1 && first.asked.empty());
    second.answers = false;
    CHECK(!m->CreateCtrl(0xdead, 1, &made) && first.asked.size() == 1 && second.asked.size() == 2);
    CHECK(m->CreateCtrl(0x48963e33, kGZIID_cIGZWinText, &made));                           // default: a label
    static_cast<cIGZUnknown*>(made)->Release();
    CHECK(m->CreateCtrl(0xa8963e4b, kGZIID_cIGZWinBtn, &made));                            // default: a skinned button
    cIGZWinBtn* skinned = static_cast<cIGZWinBtn*>(made);
    std::uint32_t fc = 0;
    CHECK(skinned->GetFontColor(2, fc) && fc == 0xffffffffu && skinned->GetFontColor(4, fc) && fc == 0xffa0a0a0u);
    skinned->Release();
    CHECK(m->CreateCtrl(0x68963e21, kGZIID_cIGZWinBtn, &made));
    static_cast<cIGZWinBtn*>(made)->Release();
    CHECK(m->CreateCtrl(0x28963e2c, kGZIID_cIGZWinScrollbar, &made));
    static_cast<cIGZUnknown*>(made)->Release();
    CHECK(m->CreateCtrl(0xc8963e3c, kGZIID_cIGZWinOptGrp, &made));
    cIGZWinOptGrp* grp = static_cast<cIGZWinOptGrp*>(made);
    CHECK(grp->GetOptGrpFlag(1) && grp->GetOptGrpFlag(8));
    grp->Release();
    CHECK(m->CreateCtrl(0x28963e36, kGZIID_cIGZWinListBox, &made));                       // default: a list box with the stock look
    cIGZWinListBox* list = static_cast<cIGZWinListBox*>(made);
    std::uint32_t lc = 0;
    CHECK(list->GetFontColor(cIGZWinListBox::kColorSelected, lc) && lc == 0xffffffffu && list->GetDrawSunkenRect() && !list->GetOutlineDrawing());
    list->Release();
    CHECK(!m->CreateCtrl(0x88963e39, 1, &made));                                           // generic window: no constructor registered
    int built = 0;
    m->SetFallbackConstructor(0x4386d516, [&] { ++built; auto* w = new cGZWin(&mgr); w->AddRef(); return static_cast<cIGZUnknown*>(static_cast<cIGZWin*>(w)); });
    CHECK(m->CreateCtrl(0x88963e39, kGZIID_cIGZWin, &made) && built == 1);
    static_cast<cIGZUnknown*>(made)->Release();
    CHECK(!m->CreateCtrl(0x1234abcd, 1, &made));

    // removing and shutdown
    CHECK(m->RemoveCtrlCreator(&first) && first.shutdowns == 1 && first.refs == 1 && !m->RemoveCtrlCreator(&first));
    CHECK(m->Shutdown() && second.shutdowns == 1 && second.refs == 1);
    CHECK(m->SystemFont(0x68963c4c) == nullptr && m->DefaultColor(0x28963f93) == 0 && m->DefaultText(3) == nullptr);
    CHECK(font.refs == 0 && boldFont.refs == 0 && text->Release() == 0 && name->Release() == 0);

    m->Release();
    if (failures == 0) std::printf("gz_winctrlmgr: all checks passed\n");
    return failures ? 1 : 0;
}
