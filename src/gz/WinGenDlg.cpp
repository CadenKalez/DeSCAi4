#include "gz/WinGenDlg.h"

#include "gz/BarGraph.h"
#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/LineGraph.h"
#include "gz/RZDraw.h"
#include "gz/ScatterGraph.h"
#include "gz/SmallObjects.h"
#include "gz/WinCombo.h"
#include "gz/WinCtrlMgr.h"
#include "gz/WinListBox.h"
#include "gz/WinTextEdit.h"

namespace gz {

namespace {
constexpr cRZRect kDialogEdgeRect{0, 0, 0x18, 0x18};   // DIALOG_EDGE_RECT: the 24 x 24 nine-slice source
constexpr std::uint32_t kGZMsgCommand = 0x287259f6;
constexpr std::uint32_t kWinFlagMovable = 0x100, kWinFlagDragging = 0x400;
}  // namespace

cGZWinGenDlg::cGZWinGenDlg(cIGZWinMgr* mgr, tWindowBackgroundStyle style) : cGZWin(mgr), style_(style) {}   // @ 0x1003ca670

cGZWinGenDlg::~cGZWinGenDlg() { cGZWinGenDlg::Shutdown(); }   // @ 0x1003ca6e4

bool cGZWinGenDlg::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x1003ca76a
    if (iid != 1 && iid != kGZIID_cIGZWinGenDlg) return cGZWin::QueryInterface(iid, out);
    *out = static_cast<cIGZWinGenDlg*>(this);
    AddRef();
    return true;
}

bool cGZWinGenDlg::Init() {   // @ 0x1003ca7e0
    if (get_init_flag()) return true;
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    if (!manager) return true;   // kept: nothing is set up, yet Init reports success
    if (style_ == kWindowBackgroundBevelled) {
        if (!background_) {
            background_ = manager->SystemBMP(static_cast<int>(kWinGenDlgBackground));
            if (background_) background_->AddRef();
            PrivateBuffer(true);
        }
        borders_ = cRZRect{6, 0x17, 6, 8};
    }
    if (!background_ && style_ != kWindowBackgroundFilled) return false;
    if (!font_) {
        font_ = manager->SystemFont(static_cast<int>(kWinGenDlgFont));
        if (font_) font_->AddRef();
    }
    if (!titleFont_) {
        titleFont_ = manager->SystemFont(static_cast<int>(kWinGenDlgTitleFont));
        if (titleFont_) titleFont_->AddRef();
    }
    return cGZWin::Init();
}

bool cGZWinGenDlg::Shutdown() {   // @ 0x1003ca900
    SetFlag(kWinVisible, false);
    if (style_ == kWindowBackgroundBevelled) PrivateBuffer(false);
    if (background_) {
        background_->Release();
        background_ = nullptr;
    }
    if (font_) {
        font_->Release();
        font_ = nullptr;
    }
    if (titleFont_) {
        titleFont_->Release();
        titleFont_ = nullptr;
    }
    return cGZWin::Shutdown();
}

bool cGZWinGenDlg::GZPaint() {   // @ 0x1003ca996
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    switch (style_) {
        case kWindowBackgroundBevelled:
            RZDrawBltEdge(context, background_, kDialogEdgeRect, drawArea_, true);
            break;
        case kWindowBackgroundTiled:
            context->Blt(background_, *background_->GetBufferArea(), drawArea_);
            break;
        case kWindowBackgroundFilled:
            context->SetColor(GetFillColorRGB());
            context->Fill(drawArea_);
            break;
        default:
            return true;
    }
    if (style_ == kWindowBackgroundBevelled && GetCaption().Strlen() != 0) {
        const int lineHeight = titleFont_->LineHeight(0, 0);
        const cRZRect box{0, (0x17 - lineHeight) / 2, GetW(), 0x17};
        titleFont_->DrawTextBox(static_cast<IWinBuffer*>(drawBuffer_), box, GetCaption().Data(), GetCaption().Strlen(), 1, 0x8000000, 0x8000000, nullptr, 0);
    }
    return true;
}

bool cGZWinGenDlg::GZOnCommand(std::uint32_t command, std::uint64_t param) {   // @ 0x1003cab5c
    if (command == kGZMsgCommand) {
        auto* sender = reinterpret_cast<cIGZWin*>(param);
        if (!ChildExists(sender)) return false;
        if (sender->GetID() == 0) {
            GetWindowManager()->EndDialog(kDialogEndCode, this, 0);
            return false;
        }
    }
    if (callback_) callback_(command, static_cast<std::uint32_t>(param), callbackContext_);
    return false;
}

bool cGZWinGenDlg::GZOnMouseDownL(int x, int y, std::uint32_t) {   // @ 0x1003cabe6
    set_mouse_down_offset(x, y);
    if (GetFlag(kWinFlagMovable)) SetFlag(kWinFlagDragging, true);
    GZSetCapture();
    return true;
}

bool cGZWinGenDlg::GZOnMouseUpL(int, int, std::uint32_t) {   // @ 0x1003cac30
    if (GetFlag(kWinFlagDragging)) SetFlag(kWinFlagDragging, false);
    GZReleaseCapture();
    return true;
}

bool cGZWinGenDlg::GZOnMouseMove(int x, int y, std::uint32_t flags) {   // @ 0x1003cac70
    if (!GetFlag(kWinFlagMovable)) {   // a fixed dialog hands the mouse to its parent
        cIGZWin* parent = GetParentWin();
        WindowToWindowCoordinates(parent, x, y);
        SendMsg(parent, kGZMsgMouseMove, static_cast<std::uint64_t>(static_cast<std::int64_t>(x)),
                static_cast<std::uint64_t>(static_cast<std::int64_t>(y)), flags);
        return true;
    }
    if (GetFlag(kWinFlagDragging)) GZWinOffset(x - mouseDownX_, y - mouseDownY_);
    return true;
}

bool cGZWinGenDlg::SetBackground(cIGZBuffer* image) {   // @ 0x1003cad42
    if (background_) {
        background_->Release();
        background_ = nullptr;
    }
    background_ = image;
    image->AddRef();
    return true;
}

std::uint32_t cGZWinGenDlg::ControlColor() const {
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    return manager ? manager->DefaultColor(static_cast<int>(kWinGenDlgControlColor)) : 0;
}

bool cGZWinGenDlg::AddChild(cIGZWin* window) {
    if (ChildAdd(window)) return true;
    GetWindowManager()->DestroyWindow(window);
    return false;
}

cIGZWin* cGZWinGenDlg::AddListBox(const cRZRect& area) {   // @ 0x1003cadbc
    auto* list = new cGZWinListBox(GetWindowManager(), false);
    list->SetID(IdFromAddress(list));
    list->SetArea(area);
    list->SetFont(font_);
    list->SetFillColor(ControlColor());
    ChildAdd(list);
    return list;
}

cIGZWin* cGZWinGenDlg::AddBMP(const cRZRect& area, cIGZBuffer* image) {   // @ 0x1003cae86
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    cIGZWinBMP* bmp = manager ? manager->CreateWinBMP(0, image, false) : nullptr;
    if (!bmp) return nullptr;
    cIGZWin* window = bmp->AsIGZWin();
    window->SetID(IdFromAddress(bmp));
    window->SetArea(area);
    return AddChild(window) ? window : nullptr;
}

cIGZWin* cGZWinGenDlg::AddBtn(const cRZRect& area, const cIGZString& caption) {   // @ 0x1003caf3c
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    cIGZWinBtn* button = manager ? manager->CreateButton(0, caption, nullptr, nullptr, 0) : nullptr;
    if (!button) return nullptr;
    cIGZWin* window = button->AsIGZWin();
    window->SetID(IdFromAddress(button));
    window->SetArea(area);
    return AddChild(window) ? window : nullptr;
}

cIGZWin* cGZWinGenDlg::AddCombo(const cRZRect& area, bool editable) {   // @ 0x1003caff8
    auto* combo = new cGZWinCombo(GetWindowManager(), editable);
    combo->SetID(IdFromAddress(combo));
    combo->SetFont(font_ ? font_->AsFont() : nullptr);
    combo->SetArea(area);
    combo->SetFillColor(ControlColor());
    ChildAdd(combo);
    return combo;
}

cIGZWin* cGZWinGenDlg::AddLineInput(const cRZRect& area) {   // @ 0x1003cb0ca
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    cIGZWinLineInput* input = manager ? manager->CreateLineInput(0, true) : nullptr;
    if (!input) return nullptr;
    cIGZWin* window = input->AsIGZWin();
    input->SetFont(font_ ? font_->AsFont() : nullptr);
    window->SetFillColor(ControlColor());
    window->SetID(IdFromAddress(input));
    window->SetArea(area);
    return AddChild(window) ? window : nullptr;
}

cIGZWin* cGZWinGenDlg::AddSlider(const cRZRect& area, int low, int high, int value, int direction) {   // @ 0x1003cb1bc
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    cIGZWinSlider* slider = manager ? manager->CreateSlider(0, direction, low, high, value) : nullptr;
    if (!slider) return nullptr;
    cIGZWin* window = slider->AsIGZWin();
    window->SetArea(area);
    window->SetID(IdFromAddress(window));
    return AddChild(window) ? window : nullptr;
}

cIGZWin* cGZWinGenDlg::Scrollbar(const cRZRect& area, int low, int high, int value, int direction) {   // @ 0x1003cb288
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    cIGZWinScrollbar* bar = manager ? manager->CreateScrollbar(0, direction, low, high, value) : nullptr;
    if (!bar) return nullptr;
    cIGZWin* window = bar->AsIGZWin();
    window->SetArea(area);
    window->SetID(IdFromAddress(bar));
    return AddChild(window) ? window : nullptr;
}

cIGZWin* cGZWinGenDlg::AddSpinner(const cRZRect& area, int low, int high, int value) {   // @ 0x1003cb35a
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    cIGZWinSpinner* spinner = manager ? manager->CreateSpinner(0, 2, low, high, 1, value) : nullptr;
    if (!spinner) return nullptr;   // the original would dereference the null result
    cIGZWin* window = spinner->AsIGZWin();
    window->SetArea(area);
    window->SetID(IdFromAddress(spinner));
    ChildAdd(window);
    return window;
}

cIGZWin* cGZWinGenDlg::AddText(const cRZRect& area, const cIGZString& text, std::uint32_t alignment, bool opaque) {   // @ 0x1003cb410
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    if (!manager) return nullptr;
    cIGZWinText* label = manager->CreateLabel(0, text);
    if (!label) return nullptr;
    cIGZWin* window = label->AsIGZWin();
    window->SetArea(area);
    label->SetFont(font_);
    label->SetAlignment(alignment);
    label->SetWinTextFlag(1, true);
    label->SetBackgroundOpaque(!opaque);   // kept: the flag is inverted
    window->SetFillColor(ControlColor());
    if (!ChildAdd(window)) {
        GetWindowManager()->DestroyWindow(window);
        return nullptr;
    }
    window->SetFlag(kWinFlag800, true);
    ChildToBack(window);
    window->SetFlag(kWinFlag800, false);
    return window;
}

cIGZWin* cGZWinGenDlg::AddTextEdit(const cRZRect& area, const cIGZString& text) {   // @ 0x1003cb58a
    auto* edit = new cGZWinTextEdit(GetWindowManager(), 0x11ff);
    edit->SetArea(area);
    edit->SetCaption(text);
    edit->SetFont(font_ ? font_->AsFont() : nullptr);
    if (WinCtrlMgr()) edit->SetFillColor(ControlColor());
    edit->SetText(text.Data(), text.Strlen(), false);
    ChildAdd(edit);
    return edit;
}

cIGZWin* cGZWinGenDlg::AddCheck(const cRZRect& area, const cIGZString& caption) {   // @ 0x1003cb69a
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    if (!manager) return nullptr;
    cIGZWinBtn* check = manager->CreateButton(4, 0, caption, nullptr, nullptr, 0);
    if (!check) return nullptr;
    cIGZWin* window = check->AsIGZWin();
    window->SetArea(area);
    return AddChild(window) ? window : nullptr;
}

cIGZWin* cGZWinGenDlg::AddOptionGroup(const cRZRect& area, const cIGZString& caption) {   // @ 0x1003cb750
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    if (!manager) return nullptr;
    cIGZWinOptGrp* group = manager->CreateOptGrp(0, caption, &area);
    if (!group) return nullptr;
    cIGZWin* window = group->AsIGZWin();
    window->SetArea(area);
    return AddChild(window) ? window : nullptr;
}

cIGZWin* cGZWinGenDlg::AddLineGraph(const cRZRect& area) {   // @ 0x1003cb7ee
    auto* graph = new cGZLineGraph(GetWindowManager());
    graph->SetArea(area);
    ChildAdd(graph);
    return graph;
}

cIGZWin* cGZWinGenDlg::AddBarGraph(const cRZRect& area) {   // @ 0x1003cb86a
    auto* graph = new cGZBarGraph(GetWindowManager());
    graph->SetArea(area);
    ChildAdd(graph);
    return graph;
}

cIGZWin* cGZWinGenDlg::AddScatterGraph(const cRZRect& area) {   // @ 0x1003cb8e6
    auto* graph = new cGZScatterGraph(GetWindowManager());
    graph->SetArea(area);
    ChildAdd(graph);
    return graph;
}

}  // namespace gz
