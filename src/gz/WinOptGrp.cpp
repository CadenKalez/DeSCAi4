#include "gz/WinOptGrp.h"

#include <algorithm>

namespace gz {

namespace {
constexpr std::uint32_t kColorUnset = 0xffffffffu;
constexpr std::uint32_t kFlagLabelColor = 0x100000, kFlagLabelDisabledColor = 0x400000;
constexpr std::uint32_t kFlagBtnNormal = 0x1000000, kFlagBtnHover = 0x2000000, kFlagBtnDisabled = 0x4000000;
constexpr std::uint32_t kMsgKeyDown = 5, kMsgKeyUp = 6;

// Releases the references the typed children handed out.
template <class T> struct Held {
    T* p = nullptr;
    ~Held() { if (p) p->Release(); }
    Held() = default;
    Held(const Held&) = delete;
    void** out() { return reinterpret_cast<void**>(&p); }
};
}  // namespace

cGZWinOptGrp::~cGZWinOptGrp() {   // @ 0x10027c93c
    cGZWinOptGrp::Shutdown();
}

bool cGZWinOptGrp::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x10027c974
    if (iid == kGZIID_cIGZWinOptGrp) {
        if (!out) return false;
        *out = static_cast<cIGZWinOptGrp*>(this);
        AddRef();
        return true;
    }
    if (iid == kGZIID_cGZWinAlt || iid == 1 || iid == kGZIID_cIGZWin) {
        *out = static_cast<cGZWin*>(this);
        AddRef();
        return true;
    }
    return false;
}

bool cGZWinOptGrp::Init() {   // @ 0x10027ca0e
    if (get_init_flag()) return true;
    if (!CheckBtnFont() || !CheckLabelFont()) return false;
    if ((flags_ & kOptGrpFlagLabel) && !CreateMainCaption()) return false;
    if (fillType_ == 0) AutoSize();
    else OnLayout(GetW(), GetH());
    return cGZWin::Init();
}

bool cGZWinOptGrp::CheckBtnFont() {   // @ 0x10027caa2
    if (buttonFont_) return true;
    IWinCtrlFactory* factory = WinCtrlFactory();
    IWinFontSource* fonts = WinFontSource();
    if (!factory || !fonts) return false;
    IWinFont* font = fonts->Font(kOptGrpFontButtonSystemId);
    if (!font) font = fonts->Font(kWinDefaultFontStyle);
    if (!font) return false;
    const std::uint32_t color = font->DefaultTextColor();
    for (int i = 2; i < 5; ++i)
        if (colors_[i] == kColorUnset) colors_[i] = color;
    SetBtnFont(font);
    return buttonFont_ != nullptr;
}

bool cGZWinOptGrp::CheckLabelFont() {   // @ 0x10027cbb4
    if (labelFont_) return true;
    IWinCtrlFactory* factory = WinCtrlFactory();
    IWinFontSource* fonts = WinFontSource();
    if (!factory || !fonts) return false;
    IWinFont* font = fonts->Font(kOptGrpFontButtonSystemId);
    if (!font) font = fonts->Font(kWinDefaultFontStyle);
    if (!font) return false;
    const std::uint32_t color = font->DefaultTextColor();
    for (int i = 0; i < 2; ++i)
        if (colors_[i] == kColorUnset) colors_[i] = color;
    SetLabelFont(font);
    return labelFont_ != nullptr;
}

bool cGZWinOptGrp::CreateMainCaption() {   // @ 0x10027ccb0
    if (ChildExists(kOptGrpLabelId)) return true;
    if (!CheckLabelFont()) return false;
    IWinCtrlFactory* factory = WinCtrlFactory();
    if (!factory) return false;
    bool created = false;
    Held<cIGZWinText> label;
    if (factory->CreateCtrl(kOptGrpLabelClass, kGZIID_cIGZWinText, label.out())) {
        label.p->AsIGZWin()->SetID(kOptGrpLabelId);
        label.p->SetCaption(GetCaption());
        label.p->SetFont(labelFont_);
        label.p->SetWinTextFlag(2, false);
        label.p->SetBackgroundOpaque(false);
        label.p->FitWindowToText(false, false);
        created = ChildAdd(label.p->AsIGZWin());
    }
    UpdateFontFlagsLabel();
    UpdateLabelColor();
    return created;
}

bool cGZWinOptGrp::OnLayout(int, int height) {   // @ 0x10027ce42
    int top = gutterY_;
    if (flags_ & kOptGrpFlagLabel) {
        Held<cIGZWinText> label;
        if (QueryLabel(label.out())) {
            label.p->FitWindowToText(false, false);
            cIGZWin* window = label.p->AsIGZWin();
            window->GZWinMoveTo(gutterX_, gutterY_);
            top = window->GetH() + gutterY_ + gutterY_;
        }
    }
    if (fillType_ == 1) return true;
    int x = gutterX_, y = top, columnWidth = 0;
    ChildList* list = LockChildListRead();
    for (auto it = list->items.rbegin(); it != list->items.rend(); ++it) {   // back to front: the order the buttons were added in
        cIGZWin* child = *it;
        Held<cIGZWinBtn> button;
        if (!child->QueryInterface(kGZIID_cIGZWinBtn, button.out())) continue;
        cIGZWin* window = button.p->AsIGZWin();
        if (height - gutterY_ < window->GetH() + y) {
            x = x + columnWidth + gutterX_;
            y = top;
        }
        columnWidth = std::max(columnWidth, window->GetW());
        window->GZWinMoveTo(x, y);
        y += window->GetH();
    }
    UnlockChildListRead(list);
    return true;
}

bool cGZWinOptGrp::Shutdown() {   // @ 0x10027d028
    HideWindow();
    if (background_) {
        background_->Release();
        background_ = nullptr;
    }
    SetLabelFont(nullptr);
    SetBtnFont(nullptr);
    cGZWin::Shutdown();
    return true;
}

void cGZWinOptGrp::SetArea(int l, int t, int r, int b) {   // @ 0x10027d084
    int w = r - l, h = b - t;
    if (w <= 0) w = 1;
    if (h <= 0) h = 1;
    if (!(w == GetW() && h == GetH())) {
        if (fillType_ == 0) heightLimit_ = h;
        cRZRect limits;
        if (GetMinMaxInfo(limits)) {
            w = std::min(std::max(w, limits.left), limits.right);
            h = std::min(std::max(h, limits.top), limits.bottom);
        }
    }
    cGZWin::SetArea(l, t, w + l, h + t);
    OnLayout(l, h);
}

std::uint32_t cGZWinOptGrp::SetFlag(std::uint32_t flag, bool on) {   // @ 0x10027d14e
    const std::uint32_t result = cGZWin::SetFlag(flag, on);
    if (flag == kWinEnabled) {
        bool enable = on;
        EnumChildren(kGZIID_cIGZWinBtn, &EnumEnable, &enable);
        UpdateFontFlagsLabel();
        UpdateLabelColor();
    }
    return result;
}

bool cGZWinOptGrp::EnumEnable(cIGZWin*, std::uint32_t, void* button, void* enable) {   // @ 0x10027d1aa
    static_cast<cIGZWinBtn*>(button)->AsIGZWin()->SetFlag(kWinEnabled, *static_cast<bool*>(enable));
    return true;
}

bool cGZWinOptGrp::UpdateFontFlagsLabel() {   // @ 0x10027d1da
    if (!(flags_ & kOptGrpFlagLabel)) return true;
    if (!labelFont_) return false;
    const std::uint32_t own = labelFont_->DefaultTextColor();
    flags_ &= ~kFlagLabelColor;
    if (colors_[0] != own) flags_ |= kFlagLabelColor;
    if (colors_[1] == own) flags_ &= ~kFlagLabelDisabledColor;
    else flags_ |= kFlagLabelDisabledColor;
    return true;
}

bool cGZWinOptGrp::UpdateLabelColor() {   // @ 0x10027d24e
    if (!(flags_ & kOptGrpFlagLabel)) return true;
    Held<cIGZWinText> label;
    if (!QueryLabel(label.out())) return false;
    if (GetFlag(kWinEnabled)) {
        if (flags_ & kFlagLabelColor) label.p->SetTextColor(colors_[0]);
    } else if (flags_ & kFlagLabelDisabledColor) {
        label.p->SetTextColor(colors_[1]);
    }
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinOptGrp::OptBtnAdd(std::uint32_t id, const cIGZString& caption, std::uint32_t, cIGZBuffer* image, const cRZRect* area) {   // @ 0x10027d2f8
    if (id == 0 || GetChildWindowFromID(id) != nullptr || !CheckBtnFont()) return false;
    IWinCtrlFactory* factory = WinCtrlFactory();
    if (!factory) return false;
    Held<cIGZWinBtn> button;
    if (!factory->CreateCtrl(kOptGrpButtonClass, kGZIID_cIGZWinBtn, button.out()) || !button.p) return false;
    if (image) button.p->SetStyle(4, image, area);
    button.p->AsIGZWin()->SetID(id);
    button.p->AsIGZWin()->SetCaption(caption);
    button.p->SetFont(buttonFont_);
    button.p->SetBtnFlag(4, (flags_ & kOptGrpFlagToggle) != 0);
    const bool added = ChildAdd(button.p->AsIGZWin());
    InvalidateSelfAndParents();
    EnumChildren(kGZIID_cIGZWinBtn, &EnumButtonColors, &colors_[0]);
    OnLayout(GetW(), GetH());
    InvalidateSelfAndParents();
    return added;
}

bool cGZWinOptGrp::OptBtnDelete(std::uint32_t id) {   // @ 0x10027d506
    cIGZWin* button = GetChildWindowFromID(id);
    if (!mgr_->IsWindowValid(button)) return false;
    ChildDelete(button);
    if (value_ == id) value_ = 0;
    InvalidateAllParents();
    return true;
}

bool cGZWinOptGrp::OptBtnMoveTo(std::uint32_t id, int x, int y) {   // @ 0x10027d57e
    cIGZWin* button = GetChildWindowFromID(id);
    const bool valid = mgr_->IsWindowValid(button);
    if (valid) button->GZWinMoveTo(x, y);
    return valid;
}

bool cGZWinOptGrp::OptBtnSize(std::uint32_t id, int w, int h) {   // @ 0x10027d5e2
    cIGZWin* button = GetChildWindowFromID(id);
    const bool valid = mgr_->IsWindowValid(button);
    if (valid) button->SetSize(w, h);
    return valid;
}

bool cGZWinOptGrp::GZPaint() {   // @ 0x10027d68a
    if (background_) {
        Context()->BltTiled(background_, *background_->GetBufferArea(), drawArea_, 0, 0);
    } else if (flags_ & kOptGrpFlagOpaque) {
        Context()->SetColor(GetFillColorRGB());
        Context()->Fill(drawArea_);
    }
    if (flags_ & kOptGrpFlagOutline) {
        const int l = drawArea_.left, t = drawArea_.top, r = drawArea_.right - 1, b = drawArea_.bottom - 1;
        Context()->SetColor(outline_[0]);
        Context()->Line(l, t, l, b);
        Context()->SetColor(outline_[1]);
        Context()->Line(l, t, r, t);
        Context()->SetColor(outline_[2]);
        Context()->Line(r, t, r, b);
        Context()->SetColor(outline_[3]);
        Context()->Line(l, b, r, b);
    }
    return true;
}

bool cGZWinOptGrp::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x10027d826
    SendMsg(notify_, kMsgKeyDown, key, modifiers, 0);
    return false;
}

bool cGZWinOptGrp::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x10027d84c
    SendMsg(notify_, kMsgKeyUp, key, modifiers, 0);
    return false;
}

bool cGZWinOptGrp::GZOnCommand(std::uint32_t command, std::uint64_t parameter) {   // @ 0x10027d872
    if (command == kGZCmdOptGrpSelect) SetValue(static_cast<std::uint32_t>(parameter));
    return command == kGZCmdOptGrpSelect;
}

void cGZWinOptGrp::SetCaption(const cIGZString& caption) {   // @ 0x10027d892
    Held<cIGZWinText> label;
    if (QueryLabel(label.out())) label.p->SetCaption(caption);
    cGZWin::SetCaption(caption);
}

void cGZWinOptGrp::SetFlagInternal(int flag, bool on) {   // @ 0x10027d934
    flags_ = on ? (flags_ | flag) : (flags_ & ~static_cast<std::uint32_t>(flag));
    if (flag == 8 || flag == 4) {
        InvalidateSelfAndParents();
    } else if (flag == 1) {
        if (on) CreateMainCaption();
        else ChildDelete(GetChildWindowFromID(kOptGrpLabelId));
    }
}

bool cGZWinOptGrp::SetValue(std::uint32_t id) {   // @ 0x10027d9d6
    Held<cIGZWinBtn> old;
    if (GetChildAs(value_, kGZIID_cIGZWinBtn, old.out())) old.p->SetBtnFlag(1, false);
    Held<cIGZWinBtn> next;
    const bool found = GetChildAs(id, kGZIID_cIGZWinBtn, next.out());
    if (found) {
        value_ = id;
        next.p->SetBtnFlag(1, true);
        PostMsg(notify_, kGZMsgCommand, kGZMsgOptGrpChanged, GetID(), value_);
    }
    return found;
}

bool cGZWinOptGrp::SetBackBMP(cIGZBuffer* image) {   // @ 0x10027db20
    if (image) image->AddRef();
    if (background_) background_->Release();
    background_ = image;
    return true;
}

bool cGZWinOptGrp::SetGutters(std::uint8_t x, std::uint8_t y) {   // @ 0x10027db92
    gutterX_ = x;
    gutterY_ = y;
    AutoSize();
    return true;
}

bool cGZWinOptGrp::SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) {   // @ 0x10027dc02
    outline_[0] = static_cast<std::uint16_t>(ConvertNativeToPackedRGB(left));
    outline_[1] = static_cast<std::uint16_t>(ConvertNativeToPackedRGB(top));
    outline_[2] = static_cast<std::uint16_t>(ConvertNativeToPackedRGB(right));
    outline_[3] = static_cast<std::uint16_t>(ConvertNativeToPackedRGB(bottom));
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinOptGrp::GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) const {   // @ 0x10027dc96
    left = ConvertPackedRGBToNative(outline_[0]);
    top = ConvertPackedRGBToNative(outline_[1]);
    right = ConvertPackedRGBToNative(outline_[2]);
    bottom = ConvertPackedRGBToNative(outline_[3]);
    return true;
}

bool cGZWinOptGrp::AutoSize() {   // @ 0x10027dd30
    cRZRect info;
    if (GetMinMaxInfo(info)) SetSize(info.left, info.top);
    return true;
}

bool cGZWinOptGrp::GetMinMaxInfo(cRZRect& out) {   // @ 0x10027ddce
    if (fillType_ == 0) GetMinMaxAuto(out);
    else GetMinMaxManual(out);
    return true;
}

void cGZWinOptGrp::GetMinMaxAuto(cRZRect& out) {   // @ 0x10027ddec
    out = cRZRect{gutterX_, gutterY_, 0xffff, 0xffff};
    int top = gutterY_;
    if (flags_ & kOptGrpFlagLabel) {
        Held<cIGZWinText> label;
        if (QueryLabel(label.out())) {
            label.p->FitWindowToText(false, false);
            cIGZWin* window = label.p->AsIGZWin();
            out.left = std::max(out.left, window->GetW() + gutterX_);
            out.top += window->GetH();
            top = window->GetH() + gutterY_ + gutterY_;
        }
    }
    int x = gutterX_, y = top, columnWidth = 0;
    ChildList* list = LockChildListRead();
    for (auto it = list->items.rbegin(); it != list->items.rend(); ++it) {   // back to front: the order the buttons were added in
        cIGZWin* child = *it;
        Held<cIGZWinBtn> button;
        if (!child->QueryInterface(kGZIID_cIGZWinBtn, button.out())) continue;
        cIGZWin* window = button.p->AsIGZWin();
        const int width = window->GetW();
        columnWidth = std::max(columnWidth, width);
        if (heightLimit_ - gutterY_ < window->GetH() + y) {
            x = columnWidth + x + gutterX_;
            y = top;
            columnWidth = width;
        }
        y += window->GetH();
        if (out.left < x + columnWidth) out.left = x + columnWidth;
        if (out.top < y) out.top = y;
    }
    UnlockChildListRead(list);
    out.left += gutterX_;
    out.top += gutterY_;
}

void cGZWinOptGrp::GetMinMaxManual(cRZRect& out) {   // @ 0x10027e05e
    out = cRZRect{0, 0, 0xffff, 0xffff};
    ChildList* list = LockChildListRead();
    for (cIGZWin* child : list->items) {
        if (out.left < child->GetR()) out.left = child->GetR();
        if (out.top < child->GetB()) out.top = child->GetB();
    }
    out.left += gutterX_;
    out.top += gutterY_;
    UnlockChildListRead(list);
}

bool cGZWinOptGrp::SetLabelFont(IWinFont* font) {   // @ 0x10027e152
    if (font) font->AddRef();
    if (labelFont_) labelFont_->Release();
    labelFont_ = font;
    if (flags_ & kOptGrpFlagLabel) {
        Held<cIGZWinText> label;
        if (QueryLabel(label.out())) {
            label.p->SetFont(labelFont_);
            label.p->FitWindowToText(false, false);
        }
    }
    UpdateFontFlagsLabel();
    UpdateLabelColor();
    return true;
}

bool cGZWinOptGrp::SetBtnFont(IWinFont* font) {   // @ 0x10027e214
    if (font) font->AddRef();
    if (buttonFont_) buttonFont_->Release();
    buttonFont_ = font;
    EnumChildren(kGZIID_cIGZWinBtn, &EnumButtonFont, font);
    UpdateFontFlagsButton();
    InvalidateSelfAndParents();
    EnumChildren(kGZIID_cIGZWinBtn, &EnumButtonColors, &colors_[0]);
    return true;
}

bool cGZWinOptGrp::EnumButtonFont(cIGZWin*, std::uint32_t, void* button, void* font) {   // @ 0x10027e29e
    static_cast<cIGZWinBtn*>(button)->SetFont(static_cast<IWinFont*>(font));
    return true;
}

bool cGZWinOptGrp::UpdateFontFlagsButton() {   // @ 0x10027e2b6
    if (!buttonFont_) return false;
    const std::uint32_t own = buttonFont_->DefaultTextColor();
    flags_ &= ~kFlagBtnNormal;
    if (colors_[2] != own) flags_ |= kFlagBtnNormal;
    flags_ &= ~kFlagBtnHover;
    if (colors_[3] != own) flags_ |= kFlagBtnHover;
    if (colors_[4] == own) flags_ &= ~kFlagBtnDisabled;
    else flags_ |= kFlagBtnDisabled;
    return true;
}

bool cGZWinOptGrp::SetFontColor(tOptGrpFontColorIdx index, std::uint32_t color) {   // @ 0x10027e344
    if (index >= 5) return false;
    colors_[index] = color;
    if (index >= 2) {
        UpdateFontFlagsButton();
        InvalidateSelfAndParents();
        EnumChildren(kGZIID_cIGZWinBtn, &EnumButtonColors, &colors_[0]);
    } else {
        UpdateFontFlagsLabel();
        UpdateLabelColor();
    }
    return true;
}

bool cGZWinOptGrp::GetFontColor(tOptGrpFontColorIdx index, std::uint32_t& color) const {   // @ 0x10027e3c8
    if (index < 5 && colors_[index] != kColorUnset) {
        color = colors_[index];
        return true;
    }
    return false;
}

bool cGZWinOptGrp::ClearFontColor(tOptGrpFontColorIdx index) {   // @ 0x10027e408
    IWinFont* font;
    if (index >= 2 && index < 5) font = buttonFont_;
    else if (index < 2) font = labelFont_;
    else return false;
    if (!font) return false;
    SetFontColor(index, font->DefaultTextColor());
    return true;
}

bool cGZWinOptGrp::EnumButtonColors(cIGZWin* parent, std::uint32_t, void* buttonPtr, void* colorsPtr) {   // @ 0x10027e476
    Held<cIGZWinOptGrp> group;
    if (!parent->QueryInterface(kGZIID_cIGZWinOptGrp, group.out())) return false;
    auto* button = static_cast<cIGZWinBtn*>(buttonPtr);
    const auto* colors = static_cast<const std::uint32_t*>(colorsPtr);
    const std::uint32_t flags[3] = {kFlagBtnNormal, kFlagBtnHover, kFlagBtnDisabled};
    for (int pair = 0; pair < 3; ++pair) {
        if (group.p->GetOptGrpFlag(static_cast<int>(flags[pair]))) {
            button->SetFontColor(pair * 2, colors[2 + pair]);
            button->SetFontColor(pair * 2 + 1, colors[2 + pair]);
        } else {
            button->ClearFontColor(pair * 2);
            button->ClearFontColor(pair * 2 + 1);
        }
    }
    return true;
}

}  // namespace gz
