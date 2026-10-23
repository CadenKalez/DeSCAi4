#include "gz/WinIMECandidateList.h"

#include <algorithm>

#include "gz/WinFont.h"
#include "gz/WinGrid.h"
#include "gz/WinIMEServer.h"
#include "gz/WinStyle.h"
#include "gz/cGZMessageServer.h"
#include "rz/StringConversion.h"
#include "rz/cRZSysServPtr.h"

namespace gz {

namespace {

// The original hands the cGZWin* to cIGZMessageServer::AddNotification directly. cIGZWin's third slot is DoMessage,
// exactly like cIGZMessageTarget's (0x18), so the message server can use the window through that layout-compatible
// target view; this cast is that call site's implicit conversion.
::cIGZMessageTarget* AsMessageTarget(cGZWin* window) {
    return reinterpret_cast<::cIGZMessageTarget*>(window);
}

constexpr int kColumnNumber = 0;
constexpr int kColumnText = 1;
constexpr int kMinColumnWidth = 0xc;      // the original's 0xc floor on both measured widths
constexpr int kColumnMargin = 4;          // added to both widths after the rows are measured
constexpr int kRowLimit = 9;              // the window is sized for at most nine visible rows
constexpr int kGridMargin = 0xe;          // the list window is this much wider than its grid
constexpr int kGridHeightPad = 0xc;       // ... and this much taller
}  // namespace

cGZWinIMECandidateList::cGZWinIMECandidateList(cIGZWinMgr* mgr) : cGZWinGen(mgr) {}   // 0x1001f8abc / 0x1001fa2b6

cGZWinIMECandidateList::~cGZWinIMECandidateList() {   // 0x1001fa33e
    // Release order: font, grid, focus (the reverse of the order Init takes them in).
    if (font_) {
        font_->Release();
        font_ = nullptr;
    }
    if (grid_) {
        grid_->Release();
        grid_ = nullptr;
    }
    if (focus_) {
        focus_->Release();
        focus_ = nullptr;
    }
}

bool cGZWinIMECandidateList::QueryInterface(std::uint32_t iid, void** out) {   // 0x1001fa462
    if (iid == kGZIID_cIGZWinIMECandidateList) {
        *out = static_cast<cIGZWinIMECandidateList*>(this);
        AddRef();
        return true;
    }
    return cGZWinGen::QueryInterface(iid, out);
}

std::uint32_t cGZWinIMECandidateList::AddRef() { return cGZWinGen::AddRef(); }   // 0x1001fa4d4

std::uint32_t cGZWinIMECandidateList::Release() { return cGZWinGen::Release(); }   // 0x1001fa4fe

cIGZWin* cGZWinIMECandidateList::AsIGZWin() {   // 0x1001fb58c
    return static_cast<cIGZWin*>(static_cast<cGZWin*>(static_cast<cGZWinGen*>(this)));
}

std::uint32_t* cGZWinIMECandidateList::ColorWord(int index) {
    return index < 0 ? &flags_ : &colors_[index];
}

const std::uint32_t* cGZWinIMECandidateList::ColorWord(int index) const {
    return index < 0 ? &flags_ : &colors_[index];
}

bool cGZWinIMECandidateList::GetCandidateListFontColor(int index, std::uint32_t& native) const {   // 0x1001fb354
    if (index > 5) return false;
    native = *ColorWord(index);
    return true;
}

bool cGZWinIMECandidateList::SetCandidateListFontColor(int index, std::uint32_t native) {   // 0x1001fb372
    if (index > 5) return false;
    *ColorWord(index) = native;
    return true;
}

void cGZWinIMECandidateList::SetCandidateListFlag(int flag) {   // 0x1001fb38e
    const std::uint32_t flags = flags_;
    if ((flags & static_cast<std::uint32_t>(flag)) != 0) return;   // the quirk: the merged word is only pushed when clear
    SetCandidateListFlags(static_cast<int>(flags | static_cast<std::uint32_t>(flag)));
}

void cGZWinIMECandidateList::SetCandidateListFlags(int flags) {   // 0x1001fb3ac
    if (this->flags_ != static_cast<std::uint32_t>(flags)) {
        this->flags_ = static_cast<std::uint32_t>(flags);
        static_cast<cGZWin*>(static_cast<cGZWinGen*>(this))->get_init_flag();   // the original's dead read
    }
}

void cGZWinIMECandidateList::ClearCandidateListFlag(int flag) {   // 0x1001fb3ca
    const std::uint32_t flags = flags_;
    if ((flags & static_cast<std::uint32_t>(flag)) == 0) return;
    SetCandidateListFlags(static_cast<int>(flags & ~static_cast<std::uint32_t>(flag)));
}

bool cGZWinIMECandidateList::IsCandidateListFlagSet(int flag) {   // 0x1001fb3ea
    return (flags_ & static_cast<std::uint32_t>(flag)) != 0;
}

void cGZWinIMECandidateList::SetFont(cIGZFont* font) {   // 0x1001fb2fe
    cIGZFont* old = font_;
    if (old != font) {
        if (font) font->AddRef();
        font_ = font;
        if (old) old->Release();
    }
    static_cast<cGZWin*>(static_cast<cGZWinGen*>(this))->get_init_flag();   // the original's dead read
}

bool cGZWinIMECandidateList::Init() {   // 0x1001fa528
    cGZWinGen* gen = static_cast<cGZWinGen*>(this);
    cGZWin* win = static_cast<cGZWin*>(gen);
    if (win->get_init_flag()) return true;

    win->SetArea(10, 10, 100, 100);
    win->SetFlag(kWinPrivateBuffer, true);
    win->SetID(kIMECandidateListWindowID);

    // Pick up the window that has focus and remember the grid position below it: (0, its height + 2) converted into
    // the parent's coordinates. Without a focus window the original moves to (0x20, 0x20) unconverted.
    cIGZWin* focus = nullptr;
    if (cIGZWinMgr* mgr = win->GetWindowManager()) focus = mgr->GZGetFocus();
    if (focus != focus_) {
        if (focus) focus->AddRef();
        cIGZWin* old = focus_;
        focus_ = focus;
        if (old) old->Release();
    }
    int x = 0, y = 0x20;
    if (focus_) {
        y = focus_->GetH() + 2;
        focus_->WindowToWindowCoordinates(win->GetParentWin(), x, y);
    }
    win->GZWinMoveTo(x, y);

    gen->SetWinGenFlag(kGenMovable, true);
    gen->SetWinGenFlag(kGenResizable, false);
    gen->SetWinGenFlag(kGenEscapeCloses, false);
    gen->SetWinGenFlag(kGenCloseButton, false);
    gen->SetWinGenFlag(kGenZoomButton, false);
    gen->SetWinGenFlag(kGenMinimizeButton, false);
    gen->SetWinGenFlag(kGenCloseDisabled, true);
    gen->SetWinGenFlag(kGenMinimizeDisabled, true);
    gen->SetWinGenFlag(kGenZoomDisabled, true);
    gen->SetWinGenFlag(kGenDialog, true);
    gen->SetWinGenFlag(kGenNoBackground, false);
    gen->SetWinGenFlag(kGenFlatFill, true);
    gen->SetWinGenFlag(kGenNoPaint, false);
    win->SetFillColor(0x80, 0x80, 0x80);
    gen->SetGutters(static_cast<std::uint8_t>(0x10), static_cast<std::uint8_t>(0x10));
    gen->cGZWinGen::Init();   // the original calls cGZWinGen::Init directly, not through the vtable

    if (!font_) {
        // Original: RZGetFramework()->GetSystemService(0xc2c2eb0f, 0x22c2eb1f) -> cIGZWinCtrlMgr vslot 0x28
        // SystemFont(0x68963c4c). The recovered font seam is IWinStyleManager's GetFontStyle, the same style id and
        // the lookup cGZWinIMEComposition::Init uses.
        if (IWinStyleManager* styles = WinStyleManager())
            SetFont(static_cast<cIGZFont*>(styles->GetFontStyle(kWinDefaultFontStyle)));
    }

    // Six list colours: 0/2/5 black, 1/3/4 white.
    SetCandidateListFontColor(0, win->MakeFillColor(0, 0, 0));
    SetCandidateListFontColor(1, win->MakeFillColor(0xff, 0xff, 0xff));
    SetCandidateListFontColor(2, win->MakeFillColor(0, 0, 0));
    SetCandidateListFontColor(3, win->MakeFillColor(0xff, 0xff, 0xff));
    SetCandidateListFontColor(4, win->MakeFillColor(0xff, 0xff, 0xff));
    SetCandidateListFontColor(5, win->MakeFillColor(0, 0, 0));

    auto* grid = new cGZWinGrid(win->GetWindowManager(), 0x19bff);
    if (grid != grid_) {
        grid->AddRef();
        cGZWinGrid* old = grid_;
        grid_ = grid;
        if (old) old->Release();
    }

    // The original passes the cIGZFont* from +0x200 to the grid's SetDefaultFont (vslot 0x78). The recovered grid
    // takes an IWinFont*, so the font comes from the font source under the same style id (see the header's seams
    // note); with both seams resolving to the same font the grid is set up identically.
    if (IWinFontSource* fonts = WinFontSource()) grid_->SetDefaultFont(fonts->Font(kWinDefaultFontStyle));
    grid_->SetGutters(0, 0, 0, 0);
    grid_->ActLikeAListbox(2, 1, 1);
    grid_->ClearFlag(kGridHorizontalScroll);
    grid_->ClearFlag(kGridVerticalScroll);
    grid_->SetFlag(kGridOutline);
    grid_->SetFlag(kGridColumnLines);
    grid_->ClearFlag(kGridRowLines);
    grid_->SetFlag(kGridFlag1000);
    grid_->ClearFlag(kGridFlag1);
    grid_->AsIGZWin()->SetFlag(kWinEnabled, false);
    grid_->AsIGZWin()->SetFillColor(colors_[1]);
    for (int i = 0; i < 6; ++i) grid_->SetDefaultFontColor(colors_[i], i);
    win->ChildAdd(grid_->AsIGZWin());

    // Register for the two candidate-list messages (message server 0x64063387 / iid 0xc1085722, vslot 0x28).
    cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
    if (server) {
        ::cIGZMessageTarget* target = AsMessageTarget(win);
        server->AddNotification(target, kGZMsgIMECandidateListData);
        server->AddNotification(target, kGZMsgIMECandidateListSelection);
    }
    return true;
}

bool cGZWinIMECandidateList::Shutdown() {   // 0x1001fabbe
    cGZWinGen* gen = static_cast<cGZWinGen*>(this);
    cGZWin* win = static_cast<cGZWin*>(gen);
    if (!win->get_init_flag()) return true;
    if (cIGZWinMgr* mgr = win->GetWindowManager()) {
        if (win->GZAmCapturing()) win->GZReleaseCapture();
        if (focus_) {
            // The original gives focus back to the window it mirrored before dropping its reference (mgr vslot 0x128).
            mgr->GZSetFocus(focus_);
            focus_->Release();
            focus_ = nullptr;
        }
    }
    cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
    if (server) {
        ::cIGZMessageTarget* target = AsMessageTarget(win);
        server->RemoveNotification(target, kGZMsgIMECandidateListData);
        server->RemoveNotification(target, kGZMsgIMECandidateListSelection);
    }
    return gen->cGZWinGen::Shutdown();   // direct call, as in the original
}

bool cGZWinIMECandidateList::DoMessage(cGZMessage& message) {   // 0x1001facc4
    if (message.type == kGZMsgCharacter) {
        if (focus_) return focus_->DoMessage(message);
    } else if (message.type == kGZMsgIMECandidateListSelection) {
        SetCandidateListSelectionPosition(reinterpret_cast<cGZIMECandidateListData*>(message.param1));
    } else if (message.type == kGZMsgIMECandidateListData) {
        SetCandidateListData(reinterpret_cast<cGZIMECandidateListData*>(message.param1));
    }
    // Quirk: the original tail-jumps to cGZWin::DoMessage, skipping cGZWinGen's override.
    return static_cast<cGZWin*>(static_cast<cGZWinGen*>(this))->cGZWin::DoMessage(message);
}

void cGZWinIMECandidateList::SetCandidateListData(cGZIMECandidateListData* data) {   // 0x1001fad46
    // The original tail-jumps to the virtual SetupWindow slot (+0xa0) with the argument unchanged.
    SetupWindow(data);
}

void cGZWinIMECandidateList::SetCandidateListSelectionPosition(cGZIMECandidateListData* data) {   // 0x1001fad58
    SetupWindow(data);   // same tail jump as SetCandidateListData
}

void cGZWinIMECandidateList::SetupWindow(cGZIMECandidateListData* data) {   // 0x1001fad6a
    // The original dereferences the grid, the font and the data block without a check; the window only reaches here
    // after Init, so the guards cover the crash-only paths.
    if (!grid_ || !font_ || !data) return;
    cGZWinGen* gen = static_cast<cGZWinGen*>(this);
    cGZWin* win = static_cast<cGZWin*>(gen);

    const int fontHeight = font_->CharHeight(nullptr, 0);
    cRZRect used{};
    grid_->GetCellUsedRange(used);
    if (used.bottom != used.top) grid_->DeleteRows(0, used.bottom - used.top, 3);
    grid_->SetDefaultRowHeight(fontHeight);

    int numberWidth = kMinColumnWidth;
    int textWidth = kMinColumnWidth;
    std::uint32_t rows = 0;
    const std::uint32_t count = data->count;
    if (count == 0) {
        // The empty-list path never adds the +4 margin the filled path adds below.
        numberWidth = 0x10;
        textWidth = 0x10;
    } else {
        for (std::uint32_t i = 0; i < count; ++i) {
            cRZString number;
            number.Sprintf("%d", static_cast<int>(i + 1));
            grid_->SetCellTextValue(kColumnNumber, static_cast<int>(i), number.Data(), number.Strlen(), 1, false);
            numberWidth = std::max(numberWidth, font_->GetStringWidth(number.Data(), number.Strlen()) + 2);

            const std::uint16_t* candidate = data->candidates ? data->candidates[i] : nullptr;
            std::uint32_t length = 0;
            if (candidate) {
                while (candidate[length] != 0) ++length;   // the original copies the string into a u16string first
            }
            cRZString text;
            if (candidate) ConvertStringEncoding(candidate, length, 0x10, text);
            grid_->SetCellTextValue(kColumnText, static_cast<int>(i), text.Data(), text.Strlen(), 1, false);
            textWidth = std::max(textWidth, font_->GetStringWidth(text.Data(), text.Strlen()) + 2);
        }
        numberWidth += kColumnMargin;
        textWidth += kColumnMargin;
        rows = std::min(count, static_cast<std::uint32_t>(kRowLimit));
    }

    const int gutter = fontHeight / 8;
    const int textColumnWidth = textWidth + 2 * gutter;
    const int width = textColumnWidth + numberWidth;
    const int height = (fontHeight + 2 * gutter) * static_cast<int>(rows);
    grid_->SetCellGutters(gutter, gutter, gutter, gutter);
    grid_->SetColumnWidth(0, 1, numberWidth + 2 * gutter);
    grid_->SetColumnWidth(1, 1, textColumnWidth);
    grid_->AsIGZWin()->SetSize(width, height);
    grid_->AsIGZWin()->GZWinMoveTo(6, 6);
    grid_->SetSelection(0, static_cast<int>(data->selection));
    grid_->MakeCellVisible(0, static_cast<int>(data->selection));
    win->SetSize(width + kGridMargin, height + kGridHeightPad);   // the list is 0xe wider and 0xc taller than the grid

    // Fit the window into the parent. The original only sets a new area when a clamp ran (r >= parent width or
    // b >= parent height) or when the left / top edge was already non-positive; the clamps shift by ~overflow.
    if (cIGZWin* parent = win->GetParentWin()) {
        const int parentWidth = parent->GetW();
        const int parentHeight = parent->GetH();
        const cRZRect& area = win->GetArea();
        int left = area.left;
        int top = area.top;
        int right = area.right;
        int bottom = area.bottom;
        bool adjust = left <= 0;
        if (left <= 0) left = 1;
        const bool topWasSmall = top <= 0;
        if (top <= 0) top = 1;
        if (right >= parentWidth) {
            const int delta = ~(right - parentWidth);
            left += delta;
            right += delta;
            adjust = true;
        } else if (topWasSmall) {
            adjust = true;
        }
        if (bottom >= parentHeight) {
            const int delta = ~(bottom - parentHeight);
            top += delta;
            bottom += delta;
            adjust = true;
        }
        if (adjust) win->SetArea(left, top, right, bottom);
    }
    win->InvalidateSelfAndParents();
}

}  // namespace gz
