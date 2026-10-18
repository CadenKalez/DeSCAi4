#include "gz/SmallObjects.h"

#include "gz/DrawContext.h"

namespace gz {

bool cGZClipboardManagerClipboardData::QueryInterface(std::uint32_t iid, void** out) {
    if (iid != 1 && iid != kGZIID_cIGZClipboardData) return false;
    *out = this;
    AddRef();
    return true;
}

std::uint32_t cGZClipboardManagerClipboardData::Release() {
    if (refCount_ > 1) return --refCount_;
    delete this;
    return 0;
}

cGZWinSpyMessageFilter::cGZWinSpyMessageFilter(cIGZWin* window, cIGZWinSpyTarget* target)
    : window_(window), target_(target) {
    window->AddMessageFilter(this);
}

bool cGZWinSpyMessageFilter::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinSpyMessageFilter || iid == 1) {
        *out = static_cast<cIGZWinMessageFilter*>(this);
        AddRef();
        return true;
    }
    *out = nullptr;
    return false;
}

void cGZWinSpyMessageFilter::Detach() {
    AddRef();
    if (window_) {
        window_->RemoveMessageFilter(this);
        window_ = nullptr;
    }
    Release();
}

bool cGZWinSpyMessageFilter::DoMessage(cIGZWin* window, cGZMessage& message) {
    if (window_) target_->OnSpyMessage(window, message);
    return false;
}

bool cGZWinCustom::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinCustom) {
        if (out) {
            *out = static_cast<cIGZWinCustom*>(this);
            AddRef();
        }
        return out != nullptr;
    }
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cIGZWin*>(this);
        AddRef();
        return true;
    }
    return false;
}

// Mouse down / up / move (types 7, 10, 13) are made parent-relative before they go on to the parent.
bool cGZWinCustom::DoMessage(cGZMessage& message) {
    if (message.type < 0xe && ((0x2480u >> (message.type & 0x1f)) & 1)) {
        message.param1 += static_cast<std::int64_t>(GetL());
        message.param2 += static_cast<std::int64_t>(GetT());
    }
    return parent_ ? parent_->DoMessage(message) : false;
}

bool cGZWinCustom::GZPaint() {
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    context->SetColor(GetFillColorRGB());
    context->Fill(drawArea_);
    return true;
}

bool cGZWinCustom::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    cGZMessage message{kGZMsgKeyDown, key, modifiers, 0};
    return notify_->DoMessage(message);
}

bool cGZWinCustom::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {
    cGZMessage message{kGZMsgKeyUp, key, modifiers, 0};
    return notify_->DoMessage(message);
}

bool cGZWinDlg::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinDlg) {
        *out = static_cast<cIGZWinDlg*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cIGZWin*>(this);
        AddRef();
        return true;
    }
    return false;
}

// A key that matches a child's key equivalent gives that child the focus and is delivered to it.
bool cGZWinDlg::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    cIGZWin* child = IsChildKeyEquivalent(key, modifiers);
    if (!child) return false;
    GetWindowManager()->GZSetFocus(child);
    SendMsg(child, kGZMsgKeyDown, key, modifiers, 0);
    return true;
}

void cGZWinDlg::GZEndDialog(std::uint64_t result) {
    GetWindowManager()->EndDialog(kDialogEndCode, this, static_cast<std::int64_t>(result));
}

}  // namespace gz
