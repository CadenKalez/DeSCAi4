#include "gz/WinCombo.h"

#include <algorithm>

#include "gz/RZDraw.h"
#include "gz/WinBtn.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinFont.h"
#include "gz/WinListBox.h"
#include "gz/WinStyle.h"

namespace gz {

namespace {
constexpr std::uint32_t kComboColorFill = 0xa8963fb2, kComboImageId = 0x68963ed8, kComboDownColorId = 0x28963f93,
                        kComboFontStyle = 0x68963c4c, kComboColorSecond = 0x08963fb9, kComboColorSelected = 0x28963fbc,
                        kComboColorHighlight = 0x88963fb4, kComboColorOutline = 0xa8963fad;
std::uint32_t DefaultColor(std::uint32_t id) {
    IWinCtrlFactory* factory = WinCtrlFactory();
    return factory ? factory->DefaultColor(static_cast<int>(id)) : 0;
}
}  // namespace

cGZWinCombo::cGZWinCombo(cIGZWinMgr* mgr, bool editable) : cGZWin(mgr), editable_(editable) {   // 0x1002f3440
    cGZWin::SetFillColor(DefaultColor(kComboColorFill));
}

cGZWinCombo::~cGZWinCombo() {   // 0x1002f359a
    cGZWinCombo::Shutdown();
    strings_.clear();
}

bool cGZWinCombo::QueryInterface(std::uint32_t iid, void** out) {   // 0x1002f3670
    if (iid == kGZIID_cIGZWinCombo) {
        if (out == nullptr) return false;
        *out = static_cast<cIGZWinCombo*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

bool cGZWinCombo::Init() {   // 0x1002f37a4
    if (get_init_flag()) return true;
    if (WinStyleManager() || WinButtonHost()) {
        IWinButtonHost* host = WinButtonHost();
        if (!arrowImage_ && host) SetComboDownArrow(host->Image(kComboImageId), nullptr);
        if (arrowColor_ == kComboUnsetColor) SetComboDownColor(DefaultColor(kComboDownColorId));
        if (!font_) {
            if (IWinStyleManager* styles = WinStyleManager()) SetFont(static_cast<cIGZFont*>(styles->GetFontStyle(kComboFontStyle)));
        }
        if (fontColors_[1] == kComboUnsetColor) SetFontColor(kColorDisabled, DefaultColor(kComboColorSecond));
        if (fontColors_[2] == kComboUnsetColor) SetFontColor(kColorSelected, DefaultColor(kComboColorSelected));
        if (highlightColor_ == kComboUnsetColor) SetHighlightColor(DefaultColor(kComboColorHighlight));
    }
    if (!font_) return false;
    GetTextEdit();
    cGZWin::SetFlag(kWinEnabled, GetFlag(kWinEnabled));
    SetSelection(selection_, false);
    layout(GetW(), GetH());
    return cGZWin::Init();
}

bool cGZWinCombo::Shutdown() {   // 0x1002f399a
    HideWindow();
    if (arrowImage_) { arrowImage_->Release(); arrowImage_ = nullptr; }
    if (font_) { font_->Release(); font_ = nullptr; }
    return cGZWin::Shutdown();
}

void cGZWinCombo::SetFlags(std::uint32_t flags) {   // 0x1002f3aa4
    const std::uint32_t old = comboFlags_;
    if (old == flags) return;
    comboFlags_ = flags;
    if ((old ^ flags) & 1) widthDirty_ = true;
}

void cGZWinCombo::SetArea(int l, int t, int r, int b) {   // 0x1002f3afc
    cRZRect minimum;
    GetMinMaxInfo(minimum);
    const int width = std::max(r - l, minimum.left);
    const int height = std::max(b - t, minimum.top);
    if (width != GetW() || height != GetH()) {
        layout(width, height);
        r = width + l;
        b = height + t;
    }
    cGZWin::SetArea(l, t, r, b);
}

std::uint32_t cGZWinCombo::SetFlag(std::uint32_t flag, bool on) {   // 0x1002f3bbc
    if ((flag & 2) && dropped_) return 0;
    const std::uint32_t result = cGZWin::SetFlag(flag, on);
    if (flag == 2) {
        if (cIGZWinTextEdit* edit = get_text_edit()) edit->AsIGZWin()->SetFlag(kWinEnabled, on);
        arrowFrameX_ = (on ? arrowWidth_ : 0) + arrowOriginX_;
        InvalidateSelfAndParents();
    }
    return result;
}

void cGZWinCombo::SetFillColor(std::uint32_t nativeColor) {   // 0x1002f3c76
    cGZWin::SetFillColor(nativeColor);
    if (cIGZWinTextEdit* edit = get_text_edit()) edit->SetFillColor(nativeColor);
}

bool cGZWinCombo::SetComboDownArrow(cIGZBuffer* image, const cRZRect* area) {   // 0x1002f3cbe
    cIGZBuffer* old = arrowImage_;
    arrowImage_ = image;
    if (old) old->Release();
    int height;
    if (image) {
        image->AddRef();
        cRZRect bounds;
        if (area == nullptr) {
            image->GetBufferArea(bounds);
            arrowOriginX_ = 0;
            arrowOriginY_ = 0;
            arrowWidth_ = (bounds.right - bounds.left) / 4;
            height = bounds.bottom - bounds.top;
        } else {
            arrowOriginX_ = area->left;
            arrowOriginY_ = area->top;
            arrowWidth_ = (area->right - area->left) / 4;
            height = area->bottom - area->top;
        }
    } else {
        arrowOriginX_ = 0;
        arrowOriginY_ = 0;
        arrowWidth_ = 0x10;
        height = GetH();
    }
    arrowHeight_ = height;
    SetSize(std::max(arrowWidth_, GetW()), std::max(arrowHeight_, GetH()));
    return true;
}

bool cGZWinCombo::GetComboDownArrow(cIGZBuffer*& image, cRZRect& area) const {   // 0x1002f3dfa
    image = arrowImage_;
    area = cRZRect{arrowOriginX_, arrowOriginY_, arrowOriginX_ + arrowWidth_ * 4, arrowHeight_ + arrowOriginY_};
    return image != nullptr;
}

bool cGZWinCombo::SetComboDownColor(std::uint32_t native) {   // 0x1002f3e70
    arrowColor_ = native;
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinCombo::SetFontColor(tFontColorIdx index, std::uint32_t native) {   // 0x1002f3eca
    if (index >= 3) return false;
    const std::uint32_t old = fontColors_[index];
    fontColors_[index] = native;
    if (old == native) return true;
    InvalidateSelfAndParents();
    if (cIGZWinTextEdit* edit = get_text_edit())
        edit->SetFontColor(static_cast<cIGZWinTextEdit::ColorIndex>(index * 2), native);
    return true;
}

bool cGZWinCombo::GetFontColor(tFontColorIdx index, std::uint32_t& native) const {   // 0x1002f3f70
    if (fontColors_[index] == kComboUnsetColor) return false;
    native = fontColors_[index];
    return true;
}

bool cGZWinCombo::SetBtnGutter(std::int8_t gutter) {   // 0x1002f3fe2
    buttonGutter_ = gutter;
    layout(GetW(), GetH());
    return true;
}

bool cGZWinCombo::SetTextGutters(std::int8_t x, std::int8_t y) {   // 0x1002f40aa
    textGutterX_ = x;
    textGutterY_ = y;
    if (cIGZWinTextEdit* edit = get_text_edit()) {
        edit->SetTextGutterX(static_cast<std::uint32_t>(static_cast<int>(textGutterX_)));
        edit->SetTextGutterY(static_cast<std::uint32_t>(static_cast<int>(textGutterY_)));
        SetSize(GetW(), GetH());
    }
    return true;
}

bool cGZWinCombo::GZPaint() {   // 0x1002f417a
    auto* context = static_cast<cIGZDrawContext*>(drawContext_);
    cRZRect dest{arrowX_ + drawArea_.left, arrowY_ + drawArea_.top, 0, 0};
    dest.right = dest.left + arrowWidth_;
    dest.bottom = dest.top + arrowHeight_;
    if (!arrowImage_) {
        RZDrawRaisedRect(context, dest, arrowColor_, !dropped_, true);
    } else {
        const cRZRect source{arrowFrameX_, arrowOriginY_, arrowFrameX_ + arrowWidth_, arrowOriginY_ + arrowHeight_};
        context->Blt(reinterpret_cast<IDrawTargetBuffer*>(arrowImage_), source, dest);
    }
    return true;
}

void cGZWinCombo::SetFont(cIGZFont* font) {   // 0x1002f422a
    if (font_) font_->Release();
    font_ = font;
    if (font) font->AddRef();
    if (cIGZWinTextEdit* edit = get_text_edit()) edit->SetFont(font_);
}

bool cGZWinCombo::SetEditable(bool editable) {   // 0x1002f42b4
    if (cIGZWinTextEdit* edit = get_text_edit()) {
        if (editable) edit->SetFlag(kTEEditable);
        else edit->ClearFlag(kTEEditable);
    }
    editable_ = editable;
    return true;
}

bool cGZWinCombo::SetHighlightColor(std::uint32_t native) {   // 0x1002f4314
    highlightColor_ = native;
    if (cIGZWinTextEdit* edit = get_text_edit()) edit->SetFontColor(cIGZWinTextEdit::kColorSelectedBack, highlightColor_);
    return true;
}

bool cGZWinCombo::GetHighlightColor(std::uint32_t& native) const {   // 0x1002f4390
    if (highlightColor_ == kComboUnsetColor) return false;
    native = highlightColor_;
    return true;
}

bool cGZWinCombo::SetHighlightFillAlpha(float alpha) {   // 0x1002f43c4
    float clamped = 0.0f;
    if (0.0f <= alpha) clamped = alpha;
    float result = 1.0f;
    if (clamped <= 1.0f) result = clamped;
    highlightAlpha_ = result;
    return true;
}

bool cGZWinCombo::GZOnMouseDownL(int, int, std::uint32_t) {   // 0x1002f445e
    do_drop_down();
    return true;
}

bool cGZWinCombo::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // 0x1002f4470
    int target;
    if (key == 0x26) {
        target = selection_ - 1;
    } else {
        if (key != 0x28) {
            SendMsg(notify_, kGZMsgKeyDown, key, modifiers, 0);
            return true;
        }
        if (modifiers & 4) {
            do_drop_down();
            return true;
        }
        target = selection_ + 1;
    }
    SetSelection(target, true);
    return true;
}

bool cGZWinCombo::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {   // 0x1002f44d8
    SendMsg(notify_, kGZMsgKeyUp, key, modifiers, 0);
    return true;
}

bool cGZWinCombo::DoMessage(cGZMessage& message) {   // 0x1002f44fe
    if (message.type == kTextEditMsgMouseDownL || message.type == kTextEditMsgMouseDownR) {
        if (!editable_) do_drop_down();
    } else {
        if (message.type != kTextEditMsgKey) return cGZWin::DoMessage(message);
        send_combo_changed_msg();
    }
    return true;
}

bool cGZWinCombo::SetSelection(int index, bool notify) {   // 0x1002f453e
    if (index < 0 || count_ <= index) return false;
    selection_ = index;
    if (cIGZWinTextEdit* edit = get_text_edit()) {
        auto it = strings_.begin();
        std::advance(it, index);
        cRZString trimmed(*it);
        trimmed.Trim();
        edit->SetText(trimmed.Data(), trimmed.Strlen(), false);
    }
    InvalidateSelfAndParents();
    if (notify) send_combo_changed_msg();
    return true;
}

bool cGZWinCombo::GetSelection(int& index, cIGZString& text) const {   // 0x1002f4698
    index = selection_;
    GetStringFromIndex(selection_, text);
    return true;
}

bool cGZWinCombo::InsertString(const cIGZString& text, int index) {   // 0x1002f46dc
    cRZString copy(text.Data());
    if (index == 0) {
        strings_.push_front(copy);
    } else if (index != -2 && index < count_) {
        if (index < 0) return false;   // the walk never reaches a zero counter for a negative index
        auto it = strings_.begin();
        std::advance(it, index);
        strings_.insert(it, copy);
    } else {
        strings_.push_back(copy);
    }
    ++count_;
    widthDirty_ = true;
    return true;
}

bool cGZWinCombo::get_string_address_from_index(int index, const cRZString*& out) const {   // 0x1002f4d84
    if (index < count_ && 0 < count_ && index >= 0) {
        auto it = strings_.begin();
        std::advance(it, index);
        out = &*it;
        return true;
    }
    return false;
}

bool cGZWinCombo::GetStringFromIndex(int index, cIGZString& out) const {   // 0x1002f4b3e
    const cRZString* found = nullptr;
    if (!get_string_address_from_index(index, found)) return false;
    out.Copy(*found);
    return true;
}

bool cGZWinCombo::GetIndexFromString(int& index, const cIGZString& text) const {   // 0x1002f4a5c
    int i = 0;
    for (const cRZString& item : strings_) {
        if (text.IsEqualToStringPointer(&item, true)) {
            index = i;
            return true;
        }
        ++i;
    }
    return false;
}

bool cGZWinCombo::RemoveString(const cIGZString& text) {   // 0x1002f4bb8
    int index = 0;
    if (!GetIndexFromString(index, text)) return false;
    widthDirty_ = true;
    return RemoveString(index);
}

bool cGZWinCombo::RemoveString(int index) {   // 0x1002f4c4a
    if (index < count_ && index >= 0) {
        auto it = strings_.begin();
        std::advance(it, index);
        strings_.erase(it);
        --count_;
        widthDirty_ = true;
        return true;
    }
    return false;
}

void cGZWinCombo::RemoveAllStrings() {   // 0x1002f4d06
    strings_.clear();
    count_ = 0;
    selection_ = -1;
    widthDirty_ = true;
}

bool cGZWinCombo::GetMinMaxInfo(cRZRect& info) {   // 0x1002f4dc2
    info.left = arrowWidth_;
    int height;
    if (!arrowImage_) {
        info.left = 0x10;
        height = 8;
    } else {
        info.left = arrowWidth_;
        height = arrowHeight_;
    }
    info.top = height;
    if (get_text_edit()) info.left = static_cast<int>(buttonGutter_) + 1 + info.left;
    info.right = 0x7fffffff;
    info.bottom = 0x7fffffff;
    return true;
}

cIGZWinTextEdit* cGZWinCombo::get_text_edit() {   // 0x1002f4e74
    void* raw = nullptr;
    if (GetChildAs(kComboTextEditId, kGZIID_cIGZWinTextEdit, &raw)) {
        auto* edit = static_cast<cIGZWinTextEdit*>(raw);
        edit->Release();      // the lookup took a reference; the combo keeps none
        return edit;
    }
    return nullptr;
}

cIGZWinTextEdit* cGZWinCombo::GetTextEdit() {   // 0x1002f3712
    cIGZWinTextEdit* edit = get_text_edit();
    if (!edit && create_text_edit()) edit = get_text_edit();
    return edit;
}

bool cGZWinCombo::create_text_edit() {   // 0x1002f4eb6
    if (get_text_edit()) return true;
    auto* created = new cGZWinTextEdit(GetWindowManager(), 0x11ff);
    cIGZWinTextEdit* edit = created;
    edit->AddRef();
    cIGZWin* window = edit->AsIGZWin();
    window->SetID(kComboTextEditId);
    window->GZWinMoveTo(0, 0);
    window->SetFillColor(GetFillColor());
    edit->SetFlag(kTESingleLine);
    edit->SetFlag(kTEEditable);
    edit->SetFlag(kTEReturnNotify);
    edit->SetFont(font_);
    edit->SetTextGutterX(static_cast<std::uint32_t>(static_cast<int>(textGutterX_)));
    edit->SetTextGutterY(static_cast<std::uint32_t>(static_cast<int>(textGutterY_)));
    edit->SetOutlineUse(true, DefaultColor(kComboColorOutline));
    const bool added = ChildAdd(window);
    edit->Release();
    SetEditable(editable_);
    return added;
}

bool cGZWinCombo::layout(int width, int height) {   // 0x1002f5080
    if (!arrowImage_) arrowHeight_ = height;
    arrowX_ = width - arrowWidth_;
    arrowY_ = (height - arrowHeight_) / 2;
    if (cIGZWinTextEdit* edit = get_text_edit()) edit->AsIGZWin()->SetSize(arrowX_ - static_cast<int>(buttonGutter_), height);
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinCombo::send_combo_changed_msg() {   // 0x1002f5534
    SendMsg(notify_, kGZMsgCommand, kComboChanged, GetID(), 0);
    return true;
}

bool cGZWinCombo::do_drop_down() {   // 0x1002f5114
    if (dropped_) return false;
    auto* list = new cGZWinListBox(GetWindowManager(), true);
    list->AddRef();
    dropped_ = true;
    arrowFrameX_ = arrowWidth_ * 2 + arrowOriginX_;
    InvalidateSelfAndParents();
    int popupWidth = absoluteArea_.right - absoluteArea_.left;
    if (comboFlags_ & 1) {
        int widest = widestString_;
        if (widthDirty_) {
            widestString_ = 0;
            widest = 0;
            for (const cRZString& item : strings_) {
                const int width = font_->GetStringWidth(item.Data(), item.Strlen());
                if (widestString_ < width) widestString_ = width;
                widest = widestString_;
            }
            widthDirty_ = false;
        }
        const int wanted = std::max(widest + 8, popupWidth);
        cIGZWin* main = GetMainWindow();
        const int limit = main ? main->GetW() - 0x10 : 0x400;
        popupWidth = std::min(wanted, limit);
    }
    cIGZWin* window = list->AsIGZWin();
    window->SetID(kComboListBoxId);
    window->SetArea(absoluteArea_.left, absoluteArea_.bottom, popupWidth + absoluteArea_.left, GetH() + absoluteArea_.bottom);
    window->SetFillColor(GetFillColor());
    if (IWinFontSource* fonts = WinFontSource()) list->SetFont(fonts->Font(kWinDefaultFontStyle));
    for (int i = 0; i < 3; ++i)
        if (fontColors_[i] != kComboUnsetColor) list->SetFontColor(static_cast<cIGZWinListBox::tFontColorIdx>(i), fontColors_[i]);
    list->SetOutlineDrawing(true);
    list->SetOutlineColors(outlineColor_, outlineColor_);
    list->SetHighlightColor(highlightColor_);
    list->SetHighlightFillAlpha(highlightAlpha_);
    list->SetScrollbarGutters(0, 0);
    list->SetTextGutters(textGutterX_, textGutterY_);
    window->SetFlag(kWinPrivateBuffer, false);
    for (const cRZString& item : strings_) list->InsertString(item, -2, 0);
    window->SetFlag(kWinPrivateBuffer, true);
    list->SetSelection(selection_, false);
    const int chosen = GetWindowManager()->RunPopup(window);
    SetSelection(chosen, true);
    list->Shutdown();
    list->Release();
    dropped_ = false;
    arrowFrameX_ = arrowWidth_ + arrowOriginX_;
    InvalidateSelfAndParents();
    return true;
}

}  // namespace gz
