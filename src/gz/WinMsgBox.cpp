#include "gz/WinMsgBox.h"

#include "gz/DrawContext.h"
#include "gz/RZDraw.h"
#include "gz/WinCtrlMgr.h"

namespace gz {

namespace {
template <class T>
void ReleaseAndClear(T*& p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
}  // namespace

cGZWinMsgBox::cGZWinMsgBox(cIGZWinMgr* mgr) : cGZWinDlg(mgr) {   // @ 0x10051fb7a
    titleFill_ = MakeFillColor(0x7c, 0x90, 0xff);
}

cGZWinMsgBox::~cGZWinMsgBox() { cGZWinMsgBox::Shutdown(); }   // @ 0x10051fc7e

bool cGZWinMsgBox::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10051fd68
    if (iid == kGZIID_cIGZWinMsgBox) {
        *out = static_cast<cIGZWinMsgBox*>(this);
    } else if (iid == kGZIID_cIGZWinDlg) {
        *out = static_cast<cIGZWinDlg*>(this);
    } else if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cIGZWin*>(this);
    } else {
        return false;
    }
    AddRef();
    return true;
}

bool cGZWinMsgBox::Init() {   // @ 0x10051fe40
    if (get_init_flag()) return true;
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    int buttonRow = 100;   // the width the button row is centred on
    int width = 200;
    if (style_ >= 1 && style_ <= 3) {
        buttonRow = width = 0xd0;
    } else if (style_ == 4 || style_ == 5) {
        buttonRow = width = 0x13c;
    } else if (style_ != 0) {
        buttonRow = width = 200;
    }

    int titleWidth = 0;
    cIGZWin* titleWin = nullptr;
    if (GetCaption().Strlen() != 0) {
        cRZString title(GetCaption());
        if (manager) {
            titleText_ = manager->CreateLabel(0, title);
        }
        if (!titleText_) {
            auto* created = new cGZWinText(GetWindowManager());
            titleText_ = created;
            titleText_->SetCaption(title);
            titleText_->AddRef();
        }
        titleWin = titleText_->AsIGZWin();
        titleText_->SetBackgroundOpaque(true);
        titleWin->SetFillColor(titleFill_);
        titleText_->SetWinTextFlag(1, true);
        if (!ChildAdd(titleWin)) {
            GetWindowManager()->DestroyWindow(titleWin);
            return false;
        }
        titleText_->FitWindowToText(false, false);
        titleWidth = titleWin->GetW() + 0x10;
    }
    if (width < titleWidth) width = titleWidth;

    messageText_ = manager ? manager->CreateLabel(0, message_) : messageText_;
    if (!messageText_) {
        auto* created = new cGZWinText(GetWindowManager());
        messageText_ = created;
        messageText_->SetCaption(message_);
        messageText_->AddRef();
    }
    cIGZWin* messageWin = messageText_->AsIGZWin();
    messageText_->SetAlignment(4);
    messageText_->SetWinTextFlag(2, true);
    messageText_->SetWinTextFlag(4, false);
    if (!ChildAdd(messageWin)) {
        GetWindowManager()->DestroyWindow(messageWin);
        return false;
    }
    // Widen the message until it is at least twice as wide as it is tall.
    for (;;) {
        messageWin->SetW(width);
        messageText_->FitWindowToText(false, false);
        if (messageWin->GetH() * 2 < width) break;
        width += 0x10;
    }
    int rowY = GetCaption().Strlen() == 0 ? messageWin->GetH() + 0x18 : titleWin->GetH() + 0x18 + messageWin->GetH();
    if (style_ > 5) return false;

    const int firstX = (width - buttonRow) / 2 + 8;
    int bottom = 0;
    auto place = [&](cIGZWinBtn* button, int x) {   // moves the button and widens the dialog if it sticks out
        cIGZWin* window = button->AsIGZWin();
        window->GZWinMoveTo(x, rowY);
        bottom = window->GetH() + rowY;
        if (width < window->GetW() + x) width = window->GetW() + x;
        return window;
    };
    cIGZWinBtn* last = nullptr;
    int nextX = 0;
    switch (style_) {
        case kMsgBoxOK_: {
            cIGZWinBtn* ok = create_and_add_button(kMsgBoxOK);
            if (!ok) return false;
            place(ok, firstX);
            defaultButton_ = ok;
            ok->AddRef();
            cancelButton_ = ok;
            break;
        }
        case kMsgBoxOKCancel:
        case kMsgBoxRetryCancel:
        case kMsgBoxYesNo: {
            const std::uint32_t first = style_ == kMsgBoxOKCancel ? kMsgBoxOK : style_ == kMsgBoxRetryCancel ? kMsgBoxRetry : kMsgBoxYes;
            const std::uint32_t second = style_ == kMsgBoxYesNo ? kMsgBoxNo : kMsgBoxCancel;
            cIGZWinBtn* a = create_and_add_button(first);
            if (!a) return false;
            cIGZWin* aw = a->AsIGZWin();
            aw->GZWinMoveTo(firstX, rowY);
            defaultButton_ = a;
            const int x2 = firstX + 8 + aw->GetW();
            last = create_and_add_button(second);
            if (!last) return false;
            place(last, x2);
            cancelButton_ = last;
            break;
        }
        case kMsgBoxYesNoCancel:
        case kMsgBoxAbortRetryIgnore: {
            const bool yesNo = style_ == kMsgBoxYesNoCancel;
            cIGZWinBtn* a = create_and_add_button(yesNo ? kMsgBoxYes : kMsgBoxAbort);
            if (!a) return false;
            cIGZWin* aw = a->AsIGZWin();
            aw->GZWinMoveTo(firstX, rowY);
            defaultButton_ = a;
            nextX = firstX + 8 + aw->GetW();
            cIGZWinBtn* b = create_and_add_button(yesNo ? kMsgBoxNo : kMsgBoxRetry);
            if (!b) return false;
            cIGZWin* bw = b->AsIGZWin();
            bw->GZWinMoveTo(nextX, rowY);
            const int x3 = nextX + 8 + bw->GetW();
            b->Release();
            last = create_and_add_button(yesNo ? kMsgBoxCancel : kMsgBoxIgnore);
            if (!last) return false;
            place(last, x3);
            cancelButton_ = last;
            break;
        }
        default:
            return false;
    }

    SetArea(0, 0, width + 8, bottom + 8);
    if (GetCaption().Strlen() != 0) {
        titleWin->SetW(GetW() - 0x10);
        titleWin->GZWinMoveTo(8, 8);
    }
    messageWin->SetW(GetW() - 0x10);
    messageWin->GZWinMoveTo(8, GetCaption().Strlen() == 0 ? 8 : titleWin->GetB() + 8);
    PrivateBuffer(true);
    cIGZWin* parent = GetMainWindow();
    if (!parent) return false;
    GZWinMoveTo((parent->GetW() - GetW()) / 2, (parent->GetH() - GetH()) / 2);
    SetFillColor(MakeFillColor(0x7c, 0x90, 0xff));
    outlineLight_ = 0xffffffff;
    outlineDark_ = 0xff808080;
    SetID(kWinMsgBoxDialogId);
    return cGZWin::Init();
}

cIGZWinBtn* cGZWinMsgBox::create_and_add_button(std::uint32_t id) {   // @ 0x100520814
    const std::uint32_t red = MakeFillColor(0xc0, 0, 0), green = MakeFillColor(0, 0xc0, 0), yellow = MakeFillColor(0xc0, 0xc0, 0);
    cIGZWinCtrlMgr* manager = WinCtrlMgr();
    cIGZString* defaultText = manager ? manager->DefaultText(static_cast<int>(id)) : nullptr;
    cRZString empty;
    cIGZWinBtn* button = manager ? manager->CreateButton(id, empty, nullptr, nullptr, 0) : nullptr;
    if (!button) {
        auto* created = new cGZWinBtn(GetWindowManager());
        button = created;
        button->AddRef();
        button->SetStyle(2, nullptr, nullptr);
        button->AsIGZWin()->SetID(id);
    }
    cIGZWin* window = button->AsIGZWin();
    const char* fallback = nullptr;
    std::uint32_t color = 0;
    switch (id) {
        case kMsgBoxAbort: fallback = "Abort"; color = red; break;
        case kMsgBoxCancel: fallback = "Cancel"; color = red; break;
        case kMsgBoxIgnore: fallback = "Ignore"; color = yellow; break;
        case kMsgBoxNo: fallback = "No"; color = red; break;
        case kMsgBoxOK: fallback = "OK"; color = green; break;
        case kMsgBoxRetry: fallback = "Retry"; color = green; break;
        case kMsgBoxYes: fallback = "Yes"; color = green; break;
        default:
            GetWindowManager()->DestroyWindow(window);
            button->Release();
            return nullptr;
    }
    if (defaultText) {
        window->SetCaption(*defaultText);
    } else {
        window->SetCaption(cRZString(fallback));
    }
    window->SetFillColor(color);
    button->FitSizeToContent();
    button->SetBtnFlag(0x20, false);
    window->SetW(100);
    if (!ChildAdd(window)) {
        GetWindowManager()->DestroyWindow(window);
        button->Release();
        return nullptr;
    }
    return button;
}

bool cGZWinMsgBox::Shutdown() {   // @ 0x100520d8c
    ReleaseAndClear(defaultButton_);
    ReleaseAndClear(cancelButton_);
    ReleaseAndClear(titleText_);
    ReleaseAndClear(messageText_);
    SetFlag(kWinVisible, false);
    PrivateBuffer(false);
    return cGZWin::Shutdown();
}

bool cGZWinMsgBox::GZPaint() {   // @ 0x100520e38
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    if (!background_) {
        context->SetColor(GetFillColorRGB());
        context->Fill(drawArea_);
    } else {
        context->BltTiled(background_, *background_->GetBufferArea(), drawArea_, 0, 0);
    }
    RZDraw3DRectangleOutline(context, drawArea_, outlineLight_, outlineDark_);
    return true;
}

bool cGZWinMsgBox::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x100520eec
    std::uint32_t button;
    if (key == 0x73) {                       // F4
        if (!(modifiers & 4)) return true;   // only with Alt
        button = kMsgBoxCancel;
    } else if (key == 0x1b) {
        button = kMsgBoxCancel;
    } else if (key == 0xd) {
        button = kMsgBoxOK;
    } else {
        return true;
    }
    GZOnCommand(kWinMsgBoxCommand, button);
    return true;
}

bool cGZWinMsgBox::GZOnMouseDownL(int x, int y, std::uint32_t) {   // @ 0x100520f32
    set_mouse_down_offset(x, y);
    SetFlag(kWinFlagDragging, true);
    GZSetCapture();
    return true;
}

bool cGZWinMsgBox::GZOnMouseUpL(int, int, std::uint32_t) {   // @ 0x100520f68
    const bool dragging = GetFlag(kWinFlagDragging);
    if (dragging) {
        GZReleaseCapture();
        SetFlag(kWinFlagDragging, false);
    }
    return dragging;
}

bool cGZWinMsgBox::GZOnMouseMove(int x, int y, std::uint32_t) {   // @ 0x100520fac
    if (GetFlag(kWinFlagDragging)) GZWinOffset(x - mouseDownX_, y - mouseDownY_);
    return true;
}

bool cGZWinMsgBox::GZOnCommand(std::uint32_t command, std::uint64_t param) {   // @ 0x100520ffe
    if (command == kWinMsgBoxCommand) GetWindowManager()->EndDialog(kDialogEndCode, this, static_cast<std::int64_t>(param));
    return command == kWinMsgBoxCommand;
}

bool cGZWinMsgBox::CreateDlg(const cIGZString& message, const cIGZString& title, std::uint32_t style) {   // @ 0x100521054
    message_.AssignFromIGZString(message);
    SetCaption(title);
    style_ = static_cast<std::uint16_t>(style);
    background_ = nullptr;
    messageText_ = nullptr;
    titleText_ = nullptr;
    cancelButton_ = nullptr;
    defaultButton_ = nullptr;
    return true;
}

bool cGZWinMsgBox::CreateDlg(const cIGZString& message, std::uint32_t style) {   // @ 0x100521118
    message_.AssignFromIGZString(message);
    style_ = static_cast<std::uint16_t>(style);
    // The original zeroes 0x18c..0x1bc here, which also wipes the title fill colour, the background and the outline colours.
    titleFill_ = 0;
    defaultButton_ = nullptr;
    cancelButton_ = nullptr;
    titleText_ = nullptr;
    messageText_ = nullptr;
    background_ = nullptr;
    outlineLight_ = 0;
    outlineDark_ = 0;
    return true;
}

}  // namespace gz
