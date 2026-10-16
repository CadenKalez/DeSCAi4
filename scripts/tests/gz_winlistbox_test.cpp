#include <cstdio>
#include <string>
#include <vector>

#include "fake_drawcontext.h"
#include "fake_winmgr.h"
#include "gz/WinListBox.h"

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
    std::int16_t LineHeight(int, int) override { return 8; }
    int CountLines(const char*, std::uint32_t, int) override { return 1; }
    int WrappedWidth(const char*, std::uint32_t, int) override { return 0; }
    void MeasureText(const char*, std::uint32_t, cRZRect&) override {}
    int TextWidth(const char*, std::uint32_t length) override { return int(length) * 5; }
    void DrawText(IWinBuffer*, int x, int y, const char* text, std::uint32_t length, std::uint32_t c1, std::uint32_t c2, const cRZRect*, int) override {
        drawn.push_back(std::string(text, length) + "@" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(c1) + "/" + std::to_string(c2));
    }
    void DrawTextBox(IWinBuffer*, const cRZRect&, const char*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*, int) override {}
    int GlyphHeight() override { return 8; }
    std::uint32_t FitLength(const char*, std::uint32_t length, int width, int) override { return std::min<std::uint32_t>(length, width / 5); }
};
struct Fonts final : IWinFontSource {
    FakeFont font;
    IWinFont* Font(std::uint32_t) override { return &font; }
    bool Available() override { return true; }
};
struct Factory final : IWinCtrlFactory {
    cIGZWinMgr* mgr; std::uint32_t dblClick = 500; std::uint32_t barsCreated = 0;
    explicit Factory(cIGZWinMgr* m) : mgr(m) {}
    bool CreateCtrl(std::uint32_t, std::uint32_t, void**) override { return false; }
    cIGZWinScrollbar* CreateScrollbar(std::uint32_t id, int direction, int low, int high, int value) override {
        ++barsCreated;
        auto* bar = new cGZWinScrollbar(mgr);
        bar->AddRef();
        cIGZWinScrollbar* view = nullptr;
        bar->QueryInterface(kGZIID_cIGZWinScrollbar, reinterpret_cast<void**>(&view));
        bar->Release();
        view->AsIGZWin()->SetID(id);
        view->SetOrientation(direction);
        view->SetMinMax(low, high);
        view->SetValue(value);
        return view;
    }
    std::uint32_t DefaultColor(int id) override { return 0xff000000u | static_cast<unsigned>(id & 0xffffff); }
    std::uint32_t DoubleClickMilliseconds() override { return dblClick; }
};
struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool DoMessage(cGZMessage& m) override { got.push_back(m); return true; }
};
struct List final : cGZWinListBox {
    using cGZWinListBox::cGZWinListBox;
    void SetCtx(cIGZDrawContext* c) { drawContext_ = c; }
    void Fit() { drawArea_ = cRZRect{0, 0, GetW(), GetH()}; }
    void SetElapsed(std::uint64_t ms) { clickTimer_.SetElapsedTimeU64(ms); }
    int Top() const { return topIndex_; }
    std::uint32_t Packed(int i) const { return outlineColors_[i]; }
    std::uint32_t HighlightPacked() const { return highlightColor_; }
};
std::vector<std::string> Texts(cIGZWinListBox* list) {
    std::vector<std::string> out;
    for (int i = 0; i < list->GetLineCount(); ++i) { cRZString s; list->GetStringFromIndex(i, s); out.push_back(s.Data()); }
    return out;
}
}  // namespace

int main() {
    FakeMgr mgr; FakeWinGraphics graphics; fake_dc::Ctx ctx; Fonts fonts; Factory factory(&mgr);
    SetWinGraphics(&graphics); SetWinFontSource(&fonts); SetWinCtrlFactory(&factory);
    auto* parent = new Target(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 400, 400);

    auto* list = new List(&mgr);
    list->SetID(0x31);
    list->SetNotificationTarget(parent);
    list->SetArea(0, 0, 100, 50);
    parent->ChildAdd(list);
    list->SetCtx(&ctx);
    list->SetFlag(kWinEnabled, true);
    list->Fit();
    void* out = nullptr;
    CHECK(list->QueryInterface(kGZIID_cIGZWinListBox, &out) && out == static_cast<cIGZWinListBox*>(list));
    static_cast<cIGZUnknown*>(out)->Release();
    CHECK(!list->QueryInterface(0x1234, &out) && list->AsIGZWin() == static_cast<cGZWin*>(list));
    CHECK(list->GetDrawSunkenRect() && !list->GetOutlineDrawing());
    CHECK(list->GetLineHeight() == 8 && list->GetVisibleLines() == (50 - 2) / 8);          // glyph height 8, text gutter 1

    // Init took the stock colours
    std::uint32_t color = 0;
    CHECK(list->GetFontColor(cIGZWinListBox::kColorDisabled, color) && color == factory.DefaultColor(0x08963fb9));
    CHECK(list->GetFontColor(cIGZWinListBox::kColorSelected, color) && color == factory.DefaultColor(0x28963fbc));
    CHECK(!list->GetFontColor(cIGZWinListBox::kColorText, color));
    CHECK(list->GetHighlightColor(color) && color == factory.DefaultColor(static_cast<int>(0x88963fb4)));
    std::uint32_t c1 = 0, c2 = 0;
    CHECK(list->GetOutlineColors(c1, c2) && c1 == factory.DefaultColor(static_cast<int>(0xa8963fad)) && c2 == factory.DefaultColor(0x08963faf));

    // strings
    cRZString pear("pear"), apple("apple"), fig("fig");
    CHECK(list->InsertString(pear, kListBoxEnd, 10) && list->InsertString(apple, kListBoxEnd, 20) && list->InsertString(fig, 0, 30));
    CHECK(list->GetLineCount() == 3 && Texts(list) == std::vector<std::string>({"fig", "pear", "apple"}));
    cRZString got;
    CHECK(list->GetStringFromIndex(1, got) && std::string(got.Data()) == "pear" && !list->GetStringFromIndex(3, got));
    int index = -1;
    CHECK(list->GetIndexFromString(index, apple) && index == 2 && !list->GetIndexFromString(index, cRZString("kiwi")));
    CHECK(list->ModifyString(cRZString("plum"), 1) && Texts(list)[1] == "plum");
    CHECK(list->GetSelection() == -1 && list->GetSelectionID() == 0);

    // selection notifies the target
    CHECK(list->SetSelection(1, true) && list->GetSelection() == 1 && list->GetSelectionID() == 10);
    CHECK(parent->got.size() == 1 && parent->got[0].type == kGZMsgCommand && parent->got[0].param1 == kGZMsgListBoxSelectionChanged &&
          parent->got[0].param2 == 0x31 && parent->got[0].param3 == 10);
    CHECK(list->SetSelection(99, false) && list->GetSelection() == 2 && parent->got.size() == 1);   // clamped, silent
    CHECK(list->SetSelectionByID(30, true) && list->GetSelection() == 0 && parent->got.size() == 2 && parent->got[1].param3 == 30);
    CHECK(!list->SetSelectionByID(77, true) && list->SetSelection(-1, false) && list->GetSelection() == -1);
    CHECK(list->MoveLineDown() && list->GetSelection() == 0 && parent->got.size() == 2);        // from -1 to 0, silently
    CHECK(list->MoveLineDown() && list->MoveLineDown() && list->GetSelection() == 2 && !list->MoveLineDown());
    CHECK(list->MoveLineUp() && list->GetSelection() == 1 && list->MoveLineUp() && !list->MoveLineUp() && list->GetSelection() == 0);

    // keys: arrows move and notify, unknown keys go to the target
    parent->got.clear();
    CHECK(list->GZOnKeyDown(0x28, 0) && list->GetSelection() == 1 && parent->got.size() == 1 && parent->got[0].param1 == kGZMsgListBoxSelectionChanged);
    CHECK(list->GZOnKeyDown(0x26, 0) && list->GetSelection() == 0);
    parent->got.clear();
    CHECK(list->GZOnKeyDown(0x41, 4) && parent->got.size() == 1 && parent->got[0].type == 5 && parent->got[0].param1 == 0x41);
    CHECK(list->GZOnKeyDown(0xd, 0) && parent->got.back().param1 == kGZMsgListBoxSelectionChanged + 1);   // enter = double click message
    CHECK(list->GZOnKeyUp(0x41, 4) && parent->got.back().type == 6);

    // sorting
    list->SetShouldSort();
    CHECK(list->GetShouldSort() && Texts(list) == std::vector<std::string>({"apple", "fig", "plum"}));
    CHECK(list->GetSelection() == 1 && list->GetSelectionID() == 30);                           // the selected "fig" kept its selection through the sort
    list->GetStringFromIndex(list->GetSelection(), got);
    CHECK(std::string(got.Data()) == "fig");

    // removing
    CHECK(list->RemoveString(cRZString("fig")) && list->GetLineCount() == 2 && !list->RemoveString(cRZString("fig")));
    CHECK(!list->RemoveString(5) && list->RemoveString(1) && Texts(list) == std::vector<std::string>({"apple"}));
    CHECK(list->GetSelection() == 0);
    list->RemoveAllStrings();
    CHECK(list->GetLineCount() == 0 && list->GetSelection() == -1 && list->GetIndexOfTopLine() == 0);

    // many lines: the vertical scrollbar appears and follows the top line
    for (int i = 0; i < 10; ++i) { char buf[8]; std::snprintf(buf, sizeof buf, "line%d", i); list->InsertString(cRZString(buf), kListBoxEnd, 100 + i); }
    CHECK(factory.barsCreated == 1 && list->ChildExists(kListBoxVScrollId));
    cIGZWinScrollbar* bar = nullptr;
    CHECK(list->GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, reinterpret_cast<void**>(&bar)));
    int lo = -1, hi = -1;
    list->SetIndexOfTopLine(0);                                                                 // inserts after the first scrollbar do not refresh its range
    CHECK(bar->GetMinMax(lo, hi) && lo == 0 && hi == 10 - 6 && bar->AsIGZWin()->GetW() == 0x10 && bar->AsIGZWin()->GetL() == 100 - 0x10);
    list->SetIndexOfTopLine(3);
    CHECK(list->GetIndexOfTopLine() == 3 && bar->GetValue() == 3);
    list->SetIndexOfTopLine(99);
    CHECK(list->GetIndexOfTopLine() == 4);                                                      // at most count - visible
    list->SetIndexOfTopLine(-4);
    CHECK(list->GetIndexOfTopLine() == 0);
    CHECK(list->IsIndexInView(5) && !list->IsIndexInView(6));
    list->PutIndexInView(8);
    CHECK(list->GetIndexOfTopLine() == 3 && list->IsIndexInView(8));
    CHECK(list->ScrollToStart() && list->GetIndexOfTopLine() == 0 && list->ScrollToEnd() && list->GetIndexOfTopLine() == 4);
    list->ScrollToStart();
    CHECK(list->ScrollPageDown() && list->GetIndexOfTopLine() == 4 && list->GetSelection() == 9);
    CHECK(list->ScrollPageUp() && list->GetIndexOfTopLine() == 0 && list->GetSelection() == 0);
    cRZRect text;
    list->GetTextArea(text);
    CHECK(text.left == 3 && text.top == 1 && text.right == 100 - 3 - (3 + 0x10) && text.bottom == 50 - 1);
    CHECK(list->ScrollToFirstLetter('l') && list->GetSelection() == 0);
    bar->Release();
    list->Fit();

    // painting: only the visible lines, lines start at the text gutter, selected line uses the selected colour
    fonts.font.drawn.clear();
    list->SetSelection(1, false);
    list->SetFontColor(cIGZWinListBox::kColorText, 0xff112233u);
    list->GZPaint();
    CHECK(fonts.font.drawn.size() == 6);
    CHECK(fonts.font.drawn[0] == "line0@3,1 " + std::to_string(0xff112233u) + "/134217728");
    CHECK(fonts.font.drawn[1] == "line1@3,9 " + std::to_string(factory.DefaultColor(0x28963fbc)) + "/134217728");
    list->InsertString(cRZString("#ff0000red"), kListBoxEnd, 200);
    list->InsertString(cRZString("##hash"), kListBoxEnd, 201);
    list->InsertString(cRZString("#ff00"), kListBoxEnd, 202);
    list->SetIndexOfTopLine(99);
    fonts.font.drawn.clear();
    list->GZPaint();
    CHECK(fonts.font.drawn.size() == 5 && list->GetIndexOfTopLine() == 7);                   // the last line ("#ff00") is too short for a colour and draws nothing
    CHECK(fonts.font.drawn[3] == "red@3,25 " + std::to_string(graphics.MakeNativeColor(0xff, 0, 0)) + "/134217728");
    CHECK(fonts.font.drawn[4].rfind("#hash@", 0) == 0 && fonts.font.drawn[2].rfind("line9@", 0) == 0);
    list->SetFlag(kWinEnabled, false);
    fonts.font.drawn.clear();
    list->GZPaint();
    CHECK(fonts.font.drawn.size() == 5 && fonts.font.drawn[0].find(std::to_string(factory.DefaultColor(0x08963fb9))) != std::string::npos);
    list->SetFlag(kWinEnabled, true);

    // mouse: down selects the line under the pointer; a second click inside the double click time posts the double click
    list->SetIndexOfTopLine(0);
    parent->got.clear();
    CHECK(list->GZOnMouseDownL(10, 1 + 8 * 2 + 1, 0) && list->GetSelection() == 2 && parent->got.size() == 1);
    CHECK(mgr.capture == list);
    list->GZOnMouseUpL(10, 20, 0);
    CHECK(mgr.capture == nullptr && parent->got.size() == 1);
    list->SetElapsed(100);
    list->GZOnMouseDownL(10, 1 + 8 * 2 + 1, 0);
    list->GZOnMouseUpL(10, 20, 0);
    CHECK(parent->got.back().param1 == kGZMsgListBoxDoubleClick);
    list->SetElapsed(900);
    parent->got.clear();
    list->GZOnMouseDownL(10, 1 + 8 * 2 + 1, 0);
    list->GZOnMouseUpL(10, 20, 0);
    CHECK(parent->got.empty());                                                                 // too slow
    CHECK(list->GetItemIndexFromCursorPosition(5, -3, false) == (0 - 1) / 8 + 0 && list->GetItemIndexFromCursorPosition(5, -3, true) == (0 - 1) / 8 + 0);
    CHECK(list->GetItemIndexFromCursorPosition(5, 1000, true) == 6 && list->GetItemIndexFromCursorPosition(5, 1000, false) == -1);

    // post instead of send
    list->SetUsePostMsg(true);
    mgr.posted.clear();
    list->SetSelection(0, true);
    CHECK(mgr.posted.size() == 1 && mgr.posted[0].param1 == kGZMsgListBoxSelectionChanged && list->GetUsePostMsg());

    // gutters, colours, highlight
    std::int8_t gx = 0, gy = 0;
    CHECK(list->GetScrollbarGutters(gx, gy) && gx == 3 && gy == 0 && list->GetTextGutters(gx, gy) && gx == 3 && gy == 1);
    CHECK(list->SetTextGutters(5, 2) && list->GetTextGutters(gx, gy) && gx == 5 && gy == 2 && list->GetVisibleLines() == (50 - 4) / 8);
    list->SetTextGutters(3, 1);
    CHECK(list->SetMinimumLineHeight(12) && list->GetLineHeight() == 12 && list->GetVisibleLines() == 48 / 12);
    list->SetMinimumLineHeight(0);
    list->SetHighlightFillAlpha(5.0f);
    list->SetHighlightDrawFlag(false);
    CHECK(!list->GetHighlightDrawFlag());
    list->SetHighlightDrawFlag(true);
    list->SetOutlineColors(0xff010203u, 0xff040506u);
    CHECK(list->GetOutlineColors(c1, c2) && c1 == 0xff010203u && c2 == 0xff040506u);
    list->SetOutlineDrawing(true);
    CHECK(list->GetOutlineDrawing() && !list->GetDrawSunkenRect());
    list->ClearFontColor(cIGZWinListBox::kColorText);
    CHECK(!list->GetFontColor(cIGZWinListBox::kColorText, color));
    CHECK(!list->SetFontColor(static_cast<cIGZWinListBox::tFontColorIdx>(3), 1));

    // exact line heights
    list->SetArea(0, 0, 100, 53);
    CHECK(list->ResizeWindowForExactLineHeights() && list->GetH() == 8 * 6 + 2 && list->GetVisibleLines() == 6);

    // ---- drop-down list
    auto* drop = new List(&mgr, true);
    drop->SetNotificationTarget(parent);
    drop->SetArea(0, 0, 60, 20);
    parent->ChildAdd(drop);
    drop->SetCtx(&ctx);
    drop->SetFlag(kWinEnabled, true);
    CHECK(drop->GetDrop() && drop->GetOutlineDrawing() && !drop->GetDrawSunkenRect());
    CHECK(mgr.capture == drop);                                                                 // the drop-down grabs the mouse when it opens
    for (int i = 0; i < 3; ++i) drop->InsertString(cRZString("item"), kListBoxEnd, i);
    CHECK(drop->GetH() == 8 * 3 + 2 && drop->GetVisibleLines() == 3);                           // fits its contents
    CHECK(!drop->ChildExists(kListBoxVScrollId));
    drop->SetSelection(1, true);
    CHECK(mgr.posted.size() == 1);                                                              // selection changes stay silent in a drop-down
    mgr.dialogs.clear();
    CHECK(drop->GZOnKeyDown(0xd, 0) && mgr.dialogs.size() == 1 && mgr.dialogs[0].code == 0xf && mgr.dialogs[0].result == 1);
    CHECK(drop->GZOnKeyDown(0x1b, 0) && mgr.dialogs.back().result == -1);
    CHECK(drop->GZOnKeyDown(0x23, 0) && parent->got.size() < 100);                              // other keys are swallowed
    drop->GZOnMouseDownL(5, 200, 0);
    CHECK(mgr.dialogs.back().result == -1);                                                     // outside: cancelled
    drop->GZOnMouseDownL(5, 1 + 8, 0);
    CHECK(mgr.dialogs.back().result == 1);
    drop->GZOnMouseMove(5, 1 + 16, 0);
    CHECK(drop->GetSelection() == 2);
    parent->Release();
    if (failures == 0) std::printf("gz_winlistbox: all checks passed\n");
    return failures ? 1 : 0;
}
