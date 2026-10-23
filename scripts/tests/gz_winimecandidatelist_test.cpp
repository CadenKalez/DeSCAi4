// cGZWinIMECandidateList test: the 0x208-byte layout, construction defaults, QueryInterface / reference counting, the
// focus capture and grid child set up by Init, the two-column SetupWindow layout (filled and empty candidate blocks),
// the fit-to-parent clamps, the message handlers, the flag / colour quirks and shutdown.
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "fake_winmgr.h"
#include "gz/WinFont.h"
#include "gz/WinGrid.h"
#include "gz/WinIMECandidateList.h"
#include "gz/WinIMEServer.h"
#include "gz/WinStyle.h"
#include "rz/cRZSysServPtr.h"

static int failures = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); ++failures; } } while (0)

using namespace gz;

namespace {

// The font the original stores at +0x200 (cIGZFont): 16-pixel lines, 8 pixels per character.
struct FakeFont final : cIGZFont {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    bool Init() override { return true; }
    bool Shutdown() override { return true; }
    void GetName(cIGZString&) const override {}
    std::uint32_t GetStyle() const override { return 0; }
    bool GetStyle(int) const override { return false; }
    bool SetStyle(int, bool) override { return true; }
    std::uint32_t GetType() const override { return 0; }
    std::uint32_t GetSize() const override { return 16; }
    bool SetSize(std::uint32_t) override { return true; }
    std::uint32_t GetFontColor() const override { return 0; }
    std::uint32_t GetFontBackgroundColor() const override { return 0; }
    void SetFontColor(std::uint32_t, std::uint32_t) override {}
    int GetAntialiasing() override { return 0; }
    void SetAntialiasing(int) override {}
    float GetAntialiasingGammaCorrection() override { return 1; }
    void SetAntialiasingGammaCorrection(float) override {}
    float GetAntialiasingSharpness() override { return 1; }
    void SetAntialiasingSharpness(float) override {}
    void GetFilePath(cIGZString&) const override {}
    void SetFilePath(const cIGZString&) override {}
    bool GetCodePage(int&) const override { return true; }
    bool SetCodePage(int) override { return true; }
    bool IsCodePageAvailable(int) override { return true; }
    bool IsCharEncodingAvailable(int) override { return true; }
    bool IsCharAvailable(void*, std::uint32_t) override { return true; }
    bool PreloadChars(void*, std::uint32_t) override { return true; }
    void GetScaling(float& x, float& y) const override { x = y = 1; }
    void SetScaling(float, float) override {}
    void GetAdvanceScaling(float& x, float& y) const override { x = y = 1; }
    void SetAdvanceScaling(float, float) override {}
    void GetSpacingAdjustment(int& x, int& y) const override { x = y = 0; }
    void SetSpacingAdjustment(int, int) override {}
    int GetLineHeight() override { return 16; }
    int CharWidth(const void*, std::uint32_t length) override { return static_cast<int>(length) * 8; }
    int CharHeight(const void*, std::uint32_t) override { return 16; }   // vslot 0x128, as the list calls it
    int GetSizeAboveBaseline() override { return 12; }
    int GetSizeBelowBaseline() override { return 4; }
    int GetAdvanceWidth(const void*, std::uint32_t length) override { return static_cast<int>(length) * 8; }
    std::uint32_t CalculateWordsToFitInWidth(const void*, std::uint32_t length, std::uint32_t, std::uint32_t*,
                                             WordWrapMode) override {
        return length;
    }
    std::uint32_t CalculateCharsToFitInWidth(const void*, std::uint32_t length, std::uint32_t, std::uint32_t*) override {
        return length;
    }
    std::uint32_t CalculateNumberOfLines(const void*, std::uint32_t, std::uint32_t) override { return 1; }
    std::uint32_t CalculateWidthOfLines(const void*, std::uint32_t, std::uint32_t) override { return 0; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, bool, bool) override { return true; }
    bool CalculateTextArea(const void*, std::uint32_t, cRZRect&, int, int, int) override { return true; }
    bool TextArea(const void*, std::uint32_t, cRZRect&) override { return true; }
    int GetStringWidth(const void*, std::uint32_t length) override {   // vslot 0x180
        return static_cast<int>(length) * 8;
    }
    int GetStringVisibleWidth(const void*, std::uint32_t length, std::uint32_t& visible) override {
        visible = length;
        return static_cast<int>(length) * 8;
    }
    bool FormatForEllipsis(cIGZString&, const cIGZString&, const cRZRect&, bool) override { return true; }
    bool DrawTextLine(void*, int, int, const void*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*,
                      cRZPoint*) override {
        return true;
    }
    bool DrawTextParagraph(void*, const cRZRect&, const void*, std::uint32_t, int, std::uint32_t, std::uint32_t,
                           const cRZRect*, cRZPoint*) override {
        return true;
    }
    cIGZFontRenderer* GetRenderer() const override { return nullptr; }
    void SetRenderer(cIGZFontRenderer*) override {}
    bool LockSurfaceForFontUse(void*) override { return true; }
    bool UnlockSurfaceForFontUse(void*) override { return true; }
};

// The grid's font view (IWinFont). The recovered grid takes this interface; the original took the cIGZFont above.
struct GridFont final : IWinFont {
    int refs = 0;
    bool QueryInterface(std::uint32_t, void**) override { return false; }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override { return static_cast<std::uint32_t>(--refs); }
    std::uint32_t DefaultTextColor() override { return 0; }
    std::uint32_t DefaultShadowColor() override { return 0; }
    std::int16_t LineHeight(int, int) override { return 16; }
    int CountLines(const char*, std::uint32_t, int) override { return 1; }
    int WrappedWidth(const char*, std::uint32_t, int) override { return 0; }
    void MeasureText(const char*, std::uint32_t, cRZRect&) override {}
    int TextWidth(const char*, std::uint32_t length) override { return static_cast<int>(length) * 8; }
    void DrawText(IWinBuffer*, int, int, const char*, std::uint32_t, std::uint32_t, std::uint32_t, const cRZRect*,
                  int) override {}
    void DrawTextBox(IWinBuffer*, const cRZRect&, const char*, std::uint32_t, std::uint32_t, std::uint32_t,
                     std::uint32_t, const cRZRect*, int) override {}
};

struct Styles final : IWinStyleManager {
    FakeFont font;
    cIGZUnknown* GetFontStyle(std::uint32_t) override { return &font; }
};

struct Fonts final : IWinFontSource {
    GridFont font;
    IWinFont* Font(std::uint32_t) override { return &font; }
    bool Available() override { return true; }
};

struct FakeMessageServer final : cIGZMessageServer {
    struct Note { ::cIGZMessageTarget* target; std::uint32_t type; };
    std::vector<Note> added, removed, live;
    int refs = 1;
    bool QueryInterface(std::uint32_t iid, void** out) override {
        if (iid == 1) { *out = this; AddRef(); return true; }
        return false;
    }
    std::uint32_t AddRef() override { return static_cast<std::uint32_t>(++refs); }
    std::uint32_t Release() override {
        if (--refs == 0) { delete this; return 0; }
        return static_cast<std::uint32_t>(refs);
    }
    bool MessageSend(const cGZMessage&) override { return true; }
    bool MessagePost(const cGZMessage&, bool) override { return true; }
    bool AddNotification(::cIGZMessageTarget* target, std::uint32_t type) override {
        target->AddRef();
        added.push_back({target, type});
        live.push_back({target, type});
        return true;
    }
    bool RemoveNotification(::cIGZMessageTarget* target, std::uint32_t type) override {
        removed.push_back({target, type});
        for (auto it = live.begin(); it != live.end(); ++it) {
            if (it->target == target && it->type == type) {
                target->Release();
                live.erase(it);
                return true;
            }
        }
        return false;
    }
    bool GeneralMessagePostToTarget(const cGZMessage&, ::cIGZMessageTarget*) override { return true; }
    bool CancelGeneralMessagePostsToTarget(::cIGZMessageTarget*) override { return true; }
    bool OnTick(std::uint32_t) override { return true; }
    std::uint32_t GetMessageQueueSize() override { return 0; }
    void SetAlwaysClearQueueOnTick(bool) override {}
};

FakeMessageServer* g_server = nullptr;
bool ServiceResolver(std::uint32_t serviceId, std::uint32_t iid, void** out) {
    if (serviceId == kGZSID_cGZMessageServer && iid == kGZIID_cIGZMessageServer && g_server) {
        g_server->AddRef();
        *out = g_server;
        return true;
    }
    return false;
}

// A plain window used both as the candidate list's parent and as the focus window it mirrors / forwards to.
struct Target final : cGZWin {
    using cGZWin::cGZWin;
    std::vector<cGZMessage> got;
    bool answer = true;
    bool DoMessage(cGZMessage& message) override {
        got.push_back(message);
        return answer;
    }
};

std::string CellText(cGZWinGrid* grid, int column, int row) {
    cRZString value;
    if (!grid->GetCellTextValue(column, row, value)) return "<none>";
    return value.Data();
}

}  // namespace

int main() {
    FakeMgr mgr;
    FakeWinGraphics graphics;
    Styles styles;
    Fonts fonts;
    SetWinGraphics(&graphics);
    SetWinStyleManager(&styles);
    SetWinFontSource(&fonts);
    SetRZServiceResolver(&ServiceResolver);

    g_server = new FakeMessageServer();   // refs starts at 1: our own reference

    auto* parent = new Target(&mgr);
    parent->AddRef();
    parent->SetArea(0, 0, 400, 300);

    auto* focus = new Target(&mgr);
    focus->AddRef();
    focus->Init();
    focus->SetArea(0, 0, 100, 50);
    mgr.focus = focus;

    auto* list = new cGZWinIMECandidateList(&mgr);
    list->AddRef();
    cIGZWinIMECandidateList* view = list;

    // --- layout and construction defaults (0x1001f8abc / 0x1001fa2b6) ---
    CHECK(sizeof(cGZWinIMECandidateList) == 0x208);
    CHECK(view->GetFont() == nullptr);
    CHECK(view->GetCandidateListFlags() == 0);
    CHECK(list->FocusTarget() == nullptr);
    CHECK(list->Grid() == nullptr);
    std::uint32_t native = 0xdeadbeefu;
    CHECK(view->GetCandidateListFontColor(0, native) && native == 0);
    CHECK(view->GetCandidateListFontColor(5, native) && native == 0);
    CHECK(!view->GetCandidateListFontColor(6, native));
    CHECK(static_cast<cGZWin*>(list)->GetID() == 0);   // SetID only happens in Init

    // --- QueryInterface (0x1001fa462) and the reference count ---
    void* out = nullptr;
    CHECK(view->QueryInterface(kGZIID_cIGZWinIMECandidateList, &out) && out == view);
    const std::uint32_t afterFirst = view->AddRef();
    const std::uint32_t afterSecond = view->AddRef();
    CHECK(afterSecond == afterFirst + 1);
    view->Release();
    view->Release();
    CHECK(list->QueryInterface(kGZIID_cIGZWinGen, &out) && out == static_cast<cIGZWinGen*>(list));
    CHECK(list->QueryInterface(1, &out) &&
          out == static_cast<cIGZWin*>(static_cast<cGZWinGen*>(list)));   // forwarded to cGZWinGen / cGZWin

    // --- SetFont (0x1001fb2fe): reference counted, the old font released ---
    view->SetFont(&styles.font);                              // the list's reference (refs 1)
    styles.font.AddRef();                                     // the style manager's own reference (refs 2)
    view->SetFont(&styles.font);                              // the same pointer: no reference churn
    CHECK(view->GetFont() == &styles.font);
    CHECK(styles.font.refs == 2);
    FakeFont other;
    other.AddRef();                                           // refs 1
    view->SetFont(&other);                                    // other 2, the style font 1
    CHECK(view->GetFont() == &other);
    CHECK(other.refs == 2 && styles.font.refs == 1);
    view->SetFont(&styles.font);                              // style font 2, other 1
    CHECK(styles.font.refs == 2 && other.refs == 1);

    // --- Init (0x1001fa528), reached through ChildAdd like the IME proxy does ---
    CHECK(parent->ChildAdd(list->AsIGZWin()));
    CHECK(list->AsIGZWin()->GetParentWin() == parent);
    CHECK(list->FocusTarget() == focus);
    CHECK(mgr.focus == list->AsIGZWin());                                    // cGZWinGen::Init's SetFocus
    CHECK(list->AsIGZWin()->GetID() == kIMECandidateListWindowID);
    CHECK(list->AsIGZWin()->GetFlag(kWinPrivateBuffer));
    CHECK(list->AsIGZWin()->GetFillColor() == 0xff808080u);
    CHECK(list->GetWinGenFlag(kGenMovable) && list->GetWinGenFlag(kGenDialog));
    CHECK(list->GetWinGenFlag(kGenFlatFill) && !list->GetWinGenFlag(kGenResizable));
    CHECK(!list->GetWinGenFlag(kGenNoBackground) && !list->GetWinGenFlag(kGenNoPaint));
    CHECK(list->GetWinGenFlag(kGenCloseDisabled) && list->GetWinGenFlag(kGenMinimizeDisabled));
    CHECK(view->GetFont() == &styles.font);
    CHECK(styles.font.refs == 2);

    // Six list colours: 0/2/5 black, 1/3/4 white.
    CHECK(view->GetCandidateListFontColor(0, native) && native == 0xff000000u);
    CHECK(view->GetCandidateListFontColor(1, native) && native == 0xffffffffu);
    CHECK(view->GetCandidateListFontColor(2, native) && native == 0xff000000u);
    CHECK(view->GetCandidateListFontColor(3, native) && native == 0xffffffffu);
    CHECK(view->GetCandidateListFontColor(4, native) && native == 0xffffffffu);
    CHECK(view->GetCandidateListFontColor(5, native) && native == 0xff000000u);

    // The window sits below the focus window: (0, height + 2) in the parent's coordinates.
    CHECK(list->AsIGZWin()->GetL() == 0 && list->AsIGZWin()->GetT() == 52);

    // The grid child: list-box flags, the font-source default font, the six colours and the fill colour.
    cGZWinGrid* grid = list->Grid();
    CHECK(grid != nullptr);
    CHECK(grid->GetParentWin() == list->AsIGZWin());
    CHECK(grid->GetFlags() == 0x3ba0);   // ActLikeAListbox(2,1,1) 0x3ba6 with the horizontal/vertical flags cleared
    CHECK(grid->GetDefaultFont() == &fonts.font);
    CHECK(grid->AsIGZWin()->GetFillColor() == 0xffffffffu);
    CHECK(!grid->AsIGZWin()->GetFlag(kWinEnabled));
    int gl = -1, gt = -1, gr = -1, gb = -1;
    CHECK(grid->GetGutters(gl, gt, gr, gb) && gl == 0 && gt == 0 && gr == 0 && gb == 0);
    CHECK(grid->GetDefaultFontColor(native, 0) && native == 0xff000000u);
    CHECK(grid->GetDefaultFontColor(native, 1) && native == 0xffffffffu);
    CHECK(grid->GetDefaultFontColor(native, 5) && native == 0xff000000u);

    // The two candidate-list messages are registered with the message server (0x2ac44ed8 then 0x2ac44ef0).
    CHECK(g_server->added.size() == 2);
    CHECK(g_server->added[0].type == kGZMsgIMECandidateListData);
    CHECK(g_server->added[1].type == kGZMsgIMECandidateListSelection);
    CHECK(g_server->added[0].target == reinterpret_cast<::cIGZMessageTarget*>(list->AsIGZWin()));

    // A second Init is a no-op: no extra notifications, same focus.
    CHECK(view->Init());
    CHECK(g_server->added.size() == 2);

    // --- SetupWindow (0x1001fad6a) with three candidates: number column + UTF-16 -> UTF-8 text column ---
    const std::u16string text0 = u"abc", text1 = u"de", text2 = u"f";
    const auto* raw0 = reinterpret_cast<const std::uint16_t*>(text0.data());
    const auto* raw1 = reinterpret_cast<const std::uint16_t*>(text1.data());
    const auto* raw2 = reinterpret_cast<const std::uint16_t*>(text2.data());
    const std::uint16_t* candidates[3] = {raw0, raw1, raw2};
    cGZIMECandidateListData data{};
    data.candidates = candidates;
    data.count = 3;
    data.selection = 2;
    list->SetCandidateListData(&data);   // 0x1001fad46
    CHECK(CellText(grid, 0, 0) == "1");
    CHECK(CellText(grid, 0, 1) == "2");
    CHECK(CellText(grid, 0, 2) == "3");
    CHECK(CellText(grid, 1, 0) == "abc");
    CHECK(CellText(grid, 1, 1) == "de");
    CHECK(CellText(grid, 1, 2) == "f");
    CHECK(CellText(grid, 1, 3) == "<none>");
    CHECK(grid->GetDefaultRowHeight() == 16);
    CHECK(grid->GetColumnWidth(0, 1) == 20);   // max(0xc, 8 + 2) + 4 = 16, plus 2 * gutter
    CHECK(grid->GetColumnWidth(1, 1) == 34);   // max(0xc, 3 * 8 + 2) + 4 = 30, plus 2 * gutter
    CHECK(grid->GetCellGutters(gl, gt, gr, gb) && gl == 2 && gt == 2 && gr == 2 && gb == 2);
    CHECK(grid->GetL() == 6 && grid->GetT() == 6);
    CHECK(grid->GetW() == 50 && grid->GetH() == 60);   // (16 + 2*2) + 30 wide, 3 * (16 + 2*2) tall
    int selColumn = -1, selRow = -1;
    CHECK(grid->GetSelection(selColumn, selRow) && selColumn == 0 && selRow == 2);
    CHECK(list->AsIGZWin()->GetL() == 1 && list->AsIGZWin()->GetT() == 52);
    CHECK(list->AsIGZWin()->GetR() == 64 && list->AsIGZWin()->GetB() == 124);   // 50 + 0xe, 60 + 0xc

    // --- Empty candidate block: the used rows are deleted, the empty path has no +4 margin ---
    cGZIMECandidateListData empty{};
    empty.selection = 0;
    list->SetCandidateListSelectionPosition(&empty);   // 0x1001fad58, the same SetupWindow tail jump
    CHECK(CellText(grid, 0, 1) == "<none>");
    CHECK(CellText(grid, 1, 2) == "<none>");
    CHECK(grid->GetColumnWidth(0, 1) == 20);   // 0x10 + 2 * gutter
    CHECK(grid->GetColumnWidth(1, 1) == 20);
    CHECK(grid->GetW() == 36 && grid->GetH() == 0);   // (16 + 2*2) + 16, zero rows
    CHECK(list->AsIGZWin()->GetR() == 51 && list->AsIGZWin()->GetB() == 64);   // 36 + 0xe, 0 + 0xc

    // --- Fit-to-parent: both clamps fire with a small parent ---
    parent->SetArea(0, 0, 20, 30);
    list->SetupWindow(&empty);
    CHECK(list->AsIGZWin()->GetL() == -31 && list->AsIGZWin()->GetT() == 17);
    CHECK(list->AsIGZWin()->GetR() == 19 && list->AsIGZWin()->GetB() == 29);

    // A vertical-only clamp (the window is narrower than the parent but taller).
    list->AsIGZWin()->SetArea(10, 10, 42, 82);
    parent->SetArea(0, 0, 400, 20);
    list->SetupWindow(&empty);
    CHECK(list->AsIGZWin()->GetL() == 10 && list->AsIGZWin()->GetT() == 7);
    CHECK(list->AsIGZWin()->GetR() == 60 && list->AsIGZWin()->GetB() == 19);

    // Nothing to clamp: the area is left where it was.
    list->AsIGZWin()->SetArea(10, 10, 42, 82);
    parent->SetArea(0, 0, 400, 300);
    list->SetupWindow(&empty);
    CHECK(list->AsIGZWin()->GetL() == 10 && list->AsIGZWin()->GetT() == 10);
    CHECK(list->AsIGZWin()->GetR() == 60 && list->AsIGZWin()->GetB() == 22);   // only SetSize ran, no clamp

    // --- More than nine candidates: every row is written but the height stops at nine rows ---
    const std::u16string one = u"a";
    const auto* rawOne = reinterpret_cast<const std::uint16_t*>(one.data());
    std::vector<const std::uint16_t*> manyCandidates(10, rawOne);
    cGZIMECandidateListData many{};
    many.candidates = manyCandidates.data();
    many.count = 10;
    many.selection = 0;
    list->AsIGZWin()->SetArea(10, 10, 42, 82);
    list->SetupWindow(&many);
    CHECK(CellText(grid, 1, 9) == "a");         // row 9 (the tenth) was still filled
    CHECK(grid->GetH() == 180);                  // but the window is sized for nine rows: 9 * (16 + 2 * 2)
    CHECK(grid->GetColumnWidth(0, 1) == 26);     // "10" is 16 px -> max(0xc, 18) + 4 + 2 * gutter
    CHECK(grid->GetColumnWidth(1, 1) == 20);
    CHECK(list->AsIGZWin()->GetR() == 66 && list->AsIGZWin()->GetB() == 202);   // 10 + 42 + 0xe, 10 + 180 + 0xc

    // --- DoMessage (0x1001facc4) ---
    focus->got.clear();
    cGZMessage character{kGZMsgCharacter, static_cast<std::uint64_t>('A'), 0, 0};
    CHECK(list->DoMessage(character));   // forwarded to the focus window
    CHECK(focus->got.size() == 1 && focus->got[0].type == kGZMsgCharacter && focus->got[0].param1 == 'A');
    focus->answer = false;
    CHECK(!list->DoMessage(character));   // the focus window's answer is returned
    focus->answer = true;

    data.selection = 1;
    cGZMessage dataMessage{kGZMsgIMECandidateListData, reinterpret_cast<std::uint64_t>(&data), 0, 0};
    CHECK(!list->DoMessage(dataMessage));   // the handler runs, then cGZWin::DoMessage answers false for the type
    CHECK(CellText(grid, 1, 2) == "f");
    CHECK(grid->GetSelection(selColumn, selRow) && selRow == 1);

    empty.selection = 0;
    cGZMessage selectionMessage{kGZMsgIMECandidateListSelection, reinterpret_cast<std::uint64_t>(&empty), 0, 0};
    CHECK(!list->DoMessage(selectionMessage));
    CHECK(CellText(grid, 1, 2) == "<none>");
    CHECK(grid->GetSelection(selColumn, selRow) && selRow == 0);

    cGZMessage unknown{0xdeadbeefu, 0, 0, 0};
    CHECK(!list->DoMessage(unknown));

    // --- Flag quirks (0x1001fb38e / 0x1001fb3ac / 0x1001fb3ca) ---
    view->SetCandidateListFlags(0);
    view->SetCandidateListFlag(0x10);
    CHECK(view->GetCandidateListFlags() == 0x10 && view->IsCandidateListFlagSet(0x10));
    CHECK(!view->IsCandidateListFlagSet(0x20));
    view->ClearCandidateListFlag(0x20);   // already clear: the merged word never reaches SetCandidateListFlags
    CHECK(view->GetCandidateListFlags() == 0x10);
    view->ClearCandidateListFlag(0x10);
    CHECK(view->GetCandidateListFlags() == 0);
    view->SetCandidateListFlags(5);
    CHECK(view->GetCandidateListFlags() == 5);
    view->SetCandidateListFlags(5);   // equal: no change
    CHECK(view->GetCandidateListFlags() == 5);

    // --- Colour quirks (0x1001fb354 / 0x1001fb372): only `index > 5` is rejected, so -1 is the flag word ---
    CHECK(view->SetCandidateListFontColor(2, 0xff00ff00u));
    CHECK(view->GetCandidateListFontColor(2, native) && native == 0xff00ff00u);
    CHECK(!view->SetCandidateListFontColor(6, 0) && !view->GetCandidateListFontColor(7, native));
    view->SetCandidateListFlags(0x1234);
    CHECK(view->GetCandidateListFontColor(-1, native) && native == 0x1234u);
    CHECK(view->SetCandidateListFontColor(-1, 0x5678u));
    CHECK(view->GetCandidateListFlags() == 0x5678u);   // the write landed on +0x1d0
    view->SetCandidateListFlags(0);

    // --- SetupWindow without a parent: no crash, the area is untouched ---
    list->AsIGZWin()->SetParentWin(nullptr);
    list->SetupWindow(&data);
    CHECK(CellText(grid, 1, 0) == "abc");
    list->AsIGZWin()->SetParentWin(parent);

    // --- A window that was never initialised has no grid / font, so SetupWindow is a guarded no-op ---
    auto* uninitialised = new cGZWinIMECandidateList(&mgr);
    uninitialised->AddRef();
    uninitialised->SetupWindow(&data);
    CHECK(uninitialised->Grid() == nullptr);
    // With no focus it answers character messages itself (cGZWin::GZOnCharacter answers false).
    CHECK(!uninitialised->DoMessage(character));
    CHECK(uninitialised->Release() == 0);   // the deleting destructor runs from the cGZWin base

    // --- Shutdown (0x1001fabbe): capture released, focus restored, notifications removed ---
    static_cast<cGZWin*>(static_cast<cGZWinGen*>(list))->GZSetCapture();
    CHECK(mgr.capture == list->AsIGZWin());
    CHECK(view->Shutdown());
    CHECK(mgr.capture == nullptr);
    CHECK(mgr.focus == focus);   // the mirrored window got the focus back
    CHECK(list->FocusTarget() == nullptr);
    CHECK(g_server->removed.size() == 2);
    CHECK(g_server->removed[0].type == kGZMsgIMECandidateListData);
    CHECK(g_server->removed[1].type == kGZMsgIMECandidateListSelection);
    CHECK(view->Shutdown());                 // not initialised any more: nothing else happens
    CHECK(g_server->removed.size() == 2);

    // --- A window built without an explicit font takes it from the style seam (0x1001fa528 vslot 0x38 path) ---
    auto* viaStyles = new cGZWinIMECandidateList(&mgr);
    viaStyles->AddRef();
    CHECK(viaStyles->GetFont() == nullptr);
    CHECK(viaStyles->Init());
    CHECK(viaStyles->GetFont() == &styles.font);   // only the style manager could have supplied it
    CHECK(viaStyles->Grid() != nullptr);

    if (failures == 0) std::printf("gz_winimecandidatelist: all checks passed\n");
    return failures == 0 ? 0 : 1;
}
