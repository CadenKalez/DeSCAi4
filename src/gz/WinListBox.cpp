#include "gz/WinListBox.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#include "gz/RZDraw.h"
#include "gz/WinScrollbar2.h"

namespace gz {

namespace {
template <class T> struct Held {
    T* p = nullptr;
    ~Held() { if (p) p->Release(); }
    Held() = default;
    Held(const Held&) = delete;
    void** out() { return reinterpret_cast<void**>(&p); }
};
constexpr int kStockFontStyle = 0x68963c4c;
constexpr int kColorBackground = 0x28963fa8, kColorShadow = static_cast<int>(0xa8963fad), kColorLight = 0x08963faf,
              kColorDisabledText = 0x08963fb9, kColorSelectedText = 0x28963fbc, kColorHighlight = static_cast<int>(0x88963fb4);
constexpr std::uint32_t kPopupImageId = 0xe8963ee5;
constexpr std::uint32_t kMsgKeyDown = 5, kMsgKeyUp = 6;
constexpr std::uint32_t kFontColorOwn = 0x8000000;

IWinFontSource* Fonts() { return WinFontSource(); }
}  // namespace

cGZWinListBox::cGZWinListBox(cIGZWinMgr* mgr, bool drop) : cGZWin(mgr), drop_(drop) {   // @ 0x100270950
    IWinCtrlFactory* factory = WinCtrlFactory();
    SetFillColor(factory ? factory->DefaultColor(kColorBackground) : 0u);
    if (!drop_) SetDrawSunkenRect(true);
    else SetOutlineDrawing(true);
}

cGZWinListBox::~cGZWinListBox() {   // @ 0x100270bf4
    cGZWinListBox::Shutdown();
}

bool cGZWinListBox::QueryInterface(std::uint32_t iid, void** out) {   // @ 0x100270c64
    if (iid == kGZIID_cIGZWinListBox) {
        if (!out) return false;
        *out = static_cast<cIGZWinListBox*>(this);
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

bool cGZWinListBox::Init() {   // @ 0x100270cde
    if (get_init_flag()) return true;
    IWinCtrlFactory* factory = WinCtrlFactory();
    if (factory) {
        if (!font_ && Fonts()) SetFont(Fonts()->Font(kStockFontStyle));
        if (outlineColors_[0] == kWinUnsetTextColor) outlineColors_[0] = ConvertNativeToPackedRGB(factory->DefaultColor(kColorShadow));
        if (outlineColors_[1] == kWinUnsetTextColor) outlineColors_[1] = ConvertNativeToPackedRGB(factory->DefaultColor(kColorLight));
        if (colors_[1] == kWinUnsetTextColor) SetFontColor(kColorDisabled, factory->DefaultColor(kColorDisabledText));
        if (colors_[2] == kWinUnsetTextColor) SetFontColor(kColorSelected, factory->DefaultColor(kColorSelectedText));
        if (highlightColor_ == kWinUnsetTextColor) SetHighlightColor(factory->DefaultColor(kColorHighlight));
    }
    if (drop_) {
        CalculateAllMetrics();
        if (fitHeight_) ResizeBoxToFitContents();
        if (fitWidth_) ResizeBoxToFitContentsHorizontal();
        SetIndexOfTopLine(0);
        PutIndexInView(selection_);
        GZSetCapture();
        if (factory && !popupBackground_ && WinImageSource()) SetPopupBackgroundBuffer(WinImageSource()->GetImage(kPopupImageId));
    }
    SetScrollBarSizes();
    SetScrollBarValue();
    CalculateAllMetrics();
    return cGZWin::Init();
}

bool cGZWinListBox::Shutdown() {   // @ 0x100270f12
    HideWindow();
    for (cIGZBuffer** image : {&popupBackground_, &lineBackground_}) {
        if (*image) { (*image)->Release(); *image = nullptr; }
    }
    if (font_) { font_->Release(); font_ = nullptr; }
    for (cIGZBuffer** image : {&scrollImageV_, &scrollImageH_}) {
        if (*image) { (*image)->Release(); *image = nullptr; }
    }
    return cGZWin::Shutdown();
}

void cGZWinListBox::CalculateAllMetrics() {   // @ 0x100270fc4
    lineHeight_ = minLineHeight_;
    if (lineBackground_) lineHeight_ = std::max(lineHeight_, static_cast<int>(lineBackground_->Height()));
    if (font_) lineHeight_ = std::max(lineHeight_, font_->GlyphHeight());
    visibleLines_ = lineHeight_ == 0 ? 0 : (GetH() - textGutterY_ * 2) / lineHeight_;
}

void cGZWinListBox::SetArea(int l, int t, int r, int b) {   // @ 0x100271080
    const bool resized = !((r - l) == GetW() && (b - t) == GetH());
    cGZWin::SetArea(l, t, r, b);
    CalculateAllMetrics();
    if (resized) {
        SetScrollBarSizes();
        SetScrollBarValue();
    }
    InvalidateSelfAndParents();
}

std::uint32_t cGZWinListBox::SetFlag(std::uint32_t flag, bool on) {   // @ 0x10027112a
    if (flag == kWinEnabled) {
        Held<cIGZWinScrollbar> bar;
        if (GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, bar.out()) && bar.p) bar.p->AsIGZWin()->SetFlag(kWinEnabled, on);
        Held<cIGZWinScrollbar> hbar;
        if (GetChildAs(kListBoxHScrollId, kGZIID_cIGZWinScrollbar, hbar.out()) && hbar.p) hbar.p->AsIGZWin()->SetFlag(kWinEnabled, on);
    }
    return cGZWin::SetFlag(flag, on);
}

void cGZWinListBox::SetOutlineDrawing(bool on) {   // @ 0x10027120c
    outline_ = on;
    sunken_ = false;
    InvalidateSelfAndParents();
}

void cGZWinListBox::SetDrawSunkenRect(bool on) {   // @ 0x100271260
    sunken_ = on;
    outline_ = false;
    InvalidateSelfAndParents();
}

void cGZWinListBox::SetOutlineColors(std::uint32_t first, std::uint32_t second) {   // @ 0x1002712b4
    outlineColors_[0] = ConvertNativeToPackedRGB(first);
    outlineColors_[1] = ConvertNativeToPackedRGB(second);
    InvalidateSelfAndParents();
}

bool cGZWinListBox::GetOutlineColors(std::uint32_t& first, std::uint32_t& second) const {   // @ 0x100271350
    if (outlineColors_[0] == kWinUnsetTextColor || outlineColors_[1] == kWinUnsetTextColor) return false;
    first = ConvertPackedRGBToNative(outlineColors_[0]);
    second = ConvertPackedRGBToNative(outlineColors_[1]);
    return true;
}

bool cGZWinListBox::SetScrollbarGutters(std::int8_t x, std::int8_t y) {   // @ 0x1002713c4
    scrollGutterX_ = x;
    scrollGutterY_ = y;
    CalculateAllMetrics();
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinListBox::SetTextGutters(std::int8_t x, std::int8_t y) {   // @ 0x100271462
    textGutterX_ = x;
    textGutterY_ = y;
    CalculateAllMetrics();
    InvalidateSelfAndParents();
    return true;
}

void cGZWinListBox::SetLineItemBackgroundBuffer(cIGZBuffer* image) {   // @ 0x10027152e
    if (image) image->AddRef();
    if (lineBackground_) lineBackground_->Release();
    lineBackground_ = image;
}

void cGZWinListBox::SetPopupBackgroundBuffer(cIGZBuffer* image) {   // @ 0x10027159c
    if (image) image->AddRef();
    if (popupBackground_) popupBackground_->Release();
    popupBackground_ = image;
}

void cGZWinListBox::SetFont(IWinFont* font) {   // @ 0x1002715d6
    if (font) font->AddRef();
    if (font_) font_->Release();
    font_ = font;
    CalculateAllMetrics();
}

bool cGZWinListBox::SetFontColor(tFontColorIdx index, std::uint32_t native) {   // @ 0x10027162c
    if (index >= 3) return false;
    const std::uint32_t previous = colors_[index];
    colors_[index] = native;
    if (previous == native) InvalidateSelfAndParents();
    return true;
}

bool cGZWinListBox::GetFontColor(tFontColorIdx index, std::uint32_t& native) {   // @ 0x10027169e
    if (index < 3 && colors_[index] != kWinUnsetTextColor) {
        native = colors_[index];
        return true;
    }
    return false;
}

void cGZWinListBox::SetHighlightColor(std::uint32_t native) {   // @ 0x10027171a
    highlightColor_ = ConvertNativeToPackedRGB(native);
    InvalidateSelfAndParents();
}

bool cGZWinListBox::GetHighlightColor(std::uint32_t& native) const {   // @ 0x10027177c
    if (highlightColor_ != kWinUnsetTextColor) native = ConvertPackedRGBToNative(highlightColor_);
    return highlightColor_ != kWinUnsetTextColor;
}

void cGZWinListBox::SetHighlightFillAlpha(float alpha) {   // @ 0x10027180e
    highlightAlpha_ = std::max(0.0f, std::min(alpha, 1.0f));
    InvalidateSelfAndParents();
}

bool cGZWinListBox::ResizeWindowForExactLineHeights() {   // @ 0x10027186a
    if (lineHeight_ == 0) return false;
    const int height = GetH();
    const int inner = height - textGutterY_ * 2;
    const int lines = inner / lineHeight_;
    const int exact = lineHeight_ * lines + textGutterY_ * 2;
    if (exact != height) {
        SetH(exact);
        visibleLines_ = lines;
    }
    SetScrollBarSizes();
    SetScrollBarValue();
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinListBox::GZPaint() {   // @ 0x100271902
    auto node = entries_.begin();
    cRZRect rect = drawArea_;
    Held<cIGZWinScrollbar> bar;
    if (GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, bar.out()) && bar.p) {
        rect.right -= scrollGutterX_ + bar.p->AsIGZWin()->GetW();
        topIndex_ = bar.p->GetValue();
    }
    if (!lineBackground_) {
        Context()->SetColor(GetFillColorRGB());
        Context()->Fill(rect);
    } else {
        Context()->BltTiled(lineBackground_, *lineBackground_->GetBufferArea(), rect, 0, 0);
    }
    if (drop_ && popupBackground_) {
        const cRZRect source{0, 0, static_cast<int>(popupBackground_->Width() / 3), static_cast<int>(popupBackground_->Height() / 3)};
        RZDrawBltEdge(Context(), popupBackground_, source, rect, false);
    }
    if (highlightDrawn_ && highlightAlpha_ != 0.0f && selection_ >= 0 && IsIndexInView(selection_)) {
        cRZRect fill{rect.left + 1, textGutterY_ + (selection_ - topIndex_) * lineHeight_ + rect.top, rect.right - 1, 0};
        fill.bottom = std::min(fill.top + lineHeight_, rect.bottom);
        if (highlightAlpha_ == 1.0f) {
            Context()->SetColor(highlightColor_);
            Context()->Fill(fill);
        }
    }
    if (font_) {
        int skipped = 0;
        for (; skipped < topIndex_ && node != entries_.end(); ++skipped) ++node;
        const int x = rect.left + textGutterX_;
        const int width = (rect.right - textGutterX_) - x;
        const int glyph = font_->GlyphHeight();
        int y = (lineHeight_ - glyph) / 2 + rect.top + textGutterY_;
        for (int line = 0; node != entries_.end() && line < visibleLines_; ++node, ++line) {
            cRZString text = node->text;
            std::uint32_t color = 0;
            bool colored = false;
            if (!GetFlag(kWinEnabled)) {
                colored = colors_[1] != kWinUnsetTextColor;
                if (colored) color = colors_[1];
                if (text.BeginsWith("#", 1)) {
                    if (!text.BeginsWith("##", 2) && text.Strlen() < 7) text.Erase(0, 0xffffffffu);
                    else text = text.Mid(text.BeginsWith("##", 2) ? 1 : 7, std::string::npos);
                }
            } else {
                const std::uint32_t base = (skipped + line == selection_ && highlightDrawn_) ? colors_[2] : colors_[0];
                colored = base != kWinUnsetTextColor;
                if (colored) color = base;
                if (text.BeginsWith("#", 1)) {
                    if (text.BeginsWith("##", 2)) {
                        text = text.Mid(1, std::string::npos);
                    } else if (text.Strlen() < 7) {
                        text.Erase(0, 0xffffffffu);
                    } else {
                        unsigned r = 0, g = 0, b = 0;
                        std::sscanf(text.Data(), "#%02x%02x%02x", &r, &g, &b);
                        color = MakeFillColor(static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b));
                        text = text.Mid(7, std::string::npos);
                        colored = true;
                    }
                }
            }
            if (text.Strlen() != 0) {
                const std::uint32_t fit = font_->FitLength(text.Data(), text.Strlen(), width, 0);
                font_->DrawText(drawBuffer_, x, y, text.Data(), fit, colored ? color : 0u, kFontColorOwn, nullptr, 0);
            }
            y += lineHeight_;
        }
        if (sunken_) RZDraw3DRectangleOutline(Context(), rect, outlineColors_[0], outlineColors_[1]);
        else if (outline_) RZDrawRectangleOutline(Context(), rect, outlineColors_[0], 1);
    }
    return true;
}

bool cGZWinListBox::RemoveString(const cIGZString& text) {   // @ 0x100272064
    return RemoveString(cRZString(text));
}

bool cGZWinListBox::RemoveString(const cRZString& text) {   // @ 0x100272172
    int index = 0;
    if (!GetIndexFromString(index, text)) return false;
    return RemoveString(index);
}

bool cGZWinListBox::RemoveString(int index) {   // @ 0x1002721ac
    const int count = GetLineCount();
    if (count <= index) return false;
    RemoveStringAtIndex(index);
    int top = topIndex_;
    if (index < top) --top;
    SetIndexOfTopLine(top);
    if (selection_ == index) {
        if (count - 1 == index) SetSelection(index - 1, true);
    } else if (selection_ > index) {
        SetSelection(selection_ - 1, true);
    }
    if (drop_) {
        if (fitHeight_) ResizeBoxToFitContents();
        if (fitWidth_) ResizeBoxToFitContentsHorizontal();
    }
    SetScrollBarSizes();
    SetScrollBarValue();
    InvalidateSelfAndParents();
    return true;
}

void cGZWinListBox::RemoveAllStrings() {   // @ 0x10027231e
    entries_.clear();
    count_ = 0;
    selection_ = -1;
    topIndex_ = 0;
    CalculateAllMetrics();
    RemoveScrollBar(true, true);
    InvalidateSelfAndParents();
}

bool cGZWinListBox::ModifyString(const cIGZString& text, int index) {   // @ 0x100272406
    const int count = GetLineCount();
    if (count == 0) return false;
    int at = std::max(index, 0);
    if (index >= count) at = count - 1;
    if (index == kListBoxEnd) at = count - 1;
    cRZString* entry = nullptr;
    if (!GetStringAddressFromIndex(at, entry)) return false;
    entry->Copy(text);
    if (drop_) {
        if (fitHeight_) ResizeBoxToFitContents();
        if (fitWidth_) ResizeBoxToFitContentsHorizontal();
    }
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinListBox::InsertString(const cIGZString& text, int index, std::uint32_t id) {   // @ 0x1002724dc
    const int count = GetLineCount();
    int at = std::max(index, 0);
    if (count < index) at = count;
    if (index == kListBoxEnd) at = count;
    cListEntry entry;
    entry.text = cRZString(text);
    entry.id = id;
    InsertEntryAtIndex(at, entry);
    if (count == 0) {
        if (selection_ != -1) SetSelection(0, true);
    } else if (!(selection_ < at || selection_ == -1)) {
        SetSelection(selection_ + 1, true);
    }
    if (drop_) {
        if (fitHeight_) ResizeBoxToFitContents();
        if (fitWidth_) ResizeBoxToFitContentsHorizontal();
    }
    if (visibleLines_ < GetLineCount()) {
        AddScrollBar(true, false);
    } else {
        SetScrollBarSizes();
        SetScrollBarValue();
    }
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinListBox::GetStringAddressFromIndex(int index, cRZString*& out) {   // @ 0x10027271c
    if (index < static_cast<int>(count_) && count_ != 0) {
        auto it = entries_.begin();
        std::advance(it, std::max(index, 0));
        out = &it->text;
        return true;
    }
    return false;
}

bool cGZWinListBox::GetStringFromIndex(int index, cIGZString& out) const {   // @ 0x10027275a
    cRZString text;
    const bool ok = GetStringFromIndex(index, text);
    if (ok) out.Copy(text);
    return ok;
}

bool cGZWinListBox::GetStringFromIndex(int index, cRZString& out) const {   // @ 0x1002727f4
    cRZString* entry = nullptr;
    if (!const_cast<cGZWinListBox*>(this)->GetStringAddressFromIndex(index, entry)) return false;
    out = *entry;
    return true;
}

bool cGZWinListBox::GetIndexFromString(int& index, const cIGZString& text) const {   // @ 0x100272830
    int i = 0;
    for (const cListEntry& entry : entries_) {
        if (text.IsEqualToStringPointer(&entry.text, true)) {
            index = i;
            return true;
        }
        ++i;
    }
    return false;
}

bool cGZWinListBox::GetIndexFromString(int& index, const cRZString& text) const {   // @ 0x100272912
    int i = 0;
    for (const cListEntry& entry : entries_) {
        if (entry.text.Strlen() == text.Strlen() && std::equal(text.Data(), text.Data() + text.Strlen(), entry.text.Data())) {
            index = i;
            return true;
        }
        ++i;
    }
    return false;
}

bool cGZWinListBox::RemoveStringAtIndex(int index) {   // @ 0x100272a0e
    if (count_ == 0) return false;
    if (index != kListBoxEnd) {
        if (index == 0) {
            entries_.pop_front();
            --count_;
            return true;
        }
        if (index < static_cast<int>(count_)) {
            auto it = entries_.begin();
            std::advance(it, index);
            entries_.erase(it);
            --count_;
            return true;
        }
    }
    entries_.pop_back();
    --count_;
    return true;
}

bool cGZWinListBox::InsertEntryAtIndex(int index, const cListEntry& entry) {   // @ 0x100272b4c
    if (index != kListBoxEnd) {
        if (index == 0) {
            entries_.push_front(entry);
            ++count_;
            return true;
        }
        if (index < GetLineCount()) {
            auto it = entries_.begin();
            std::advance(it, index);
            entries_.insert(it, entry);
            ++count_;
            return true;
        }
    }
    entries_.push_back(entry);
    ++count_;
    return true;
}

bool cGZWinListBox::GZCharacter(std::uint32_t character) {   // @ 0x100272d90
    if (!GZAmCapturing()) ScrollToFirstLetter(static_cast<char>(character));
    return false;
}

bool cGZWinListBox::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x100272dbe
    const int previous = selection_;
    auto forward = [&]() -> bool {
        if (drop_) return true;
        cGZMessage message;
        message.type = kMsgKeyDown;
        message.param1 = key;
        message.param2 = modifiers;
        return SendMsg(notify_, message);
    };
    switch (key) {
        case 0x21: ScrollPageUp(); break;
        case 0x22: ScrollPageDown(); break;
        case 0x23: case 0x24: case 0x25: case 0x27: return forward();
        case 0x26: MoveLineUp(); break;
        case 0x28: MoveLineDown(); break;
        default:
            if (key == 0xd) {
                SetSelection(previous, true);
                if (!drop_) return EmitDoubleClickMsg();
                EndDialog(static_cast<std::uint32_t>(selection_));
                return true;
            }
            if (key != 0x1b) return forward();
            if (!drop_) {
                cGZMessage message;
                message.type = kMsgKeyDown;
                message.param1 = 0x1b;
                message.param2 = modifiers;
                return SendMsg(notify_, message);
            }
            EndDialog(0xffffffffu);
            return true;
    }
    if (selection_ != previous) SetSelection(selection_, true);
    return true;
}

bool cGZWinListBox::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {   // @ 0x100272f28
    if (drop_) return true;
    cGZMessage message;
    message.type = kMsgKeyUp;
    message.param1 = key;
    message.param2 = modifiers;
    return SendMsg(notify_, message);
}

bool cGZWinListBox::GZOnMouseDownL(int x, int y, std::uint32_t) {   // @ 0x100272f5c
    const int index = GetItemIndexFromCursorPosition(x, y, true);
    if (!GZAmCapturing()) {
        GZSetCapture();
        if (index < GetLineCount() && static_cast<std::uint32_t>(index) != static_cast<std::uint32_t>(GetSelection())) SetSelection(index, true);
        if (doubleClickIndex_ == index) {
            IWinCtrlFactory* factory = WinCtrlFactory();
            if (factory && clickTimer_.GetElapsedTimeU32() < factory->DoubleClickMilliseconds()) doubleClickPending_ = true;
        } else {
            doubleClickIndex_ = index;
        }
        clickTimer_.Restart();
    } else if (!IsPointInWindowWindowCoordinates(x, y)) {
        EndDialog(0xffffffffu);
    } else {
        EndDialog(static_cast<std::uint32_t>(index));
    }
    return true;
}

bool cGZWinListBox::GZOnMouseUpL(int x, int y, std::uint32_t modifiers) {   // @ 0x100273186
    if (!GZAmCapturing()) return true;
    if (!drop_) {
        GZReleaseCapture();
    } else {
        if (forwarding_ || IsPointInWindowWindowCoordinates(x, y)) {
            Held<cIGZWinScrollbar> bar;
            if (GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, bar.out()) && bar.p) {
                cIGZWin* window = bar.p->AsIGZWin();
                int wx = x, wy = y;
                WindowToWindowCoordinates(window, wx, wy);
                bool forward = forwarding_;
                if (!forward && wx >= 0 && wy >= 0 && wx < window->GetW() && wy < window->GetH()) forward = true;
                if (forward) {
                    cGZMessage message;
                    message.type = kGZMsgMouseUpL;
                    message.param1 = static_cast<std::uint64_t>(static_cast<std::int64_t>(wx));
                    message.param2 = static_cast<std::uint64_t>(static_cast<std::int64_t>(wy));
                    message.param3 = modifiers;
                    if (forwarding_) mgr_->GZSetCapture(window);
                    const bool handled = window->DoMessage(message);
                    forwarding_ = mgr_->GZGetCapture() != window;
                    GZSetCapture();
                    return handled;
                }
            }
        }
    }
    if (doubleClickPending_) {
        EmitDoubleClickMsg();
        doubleClickIndex_ = -1;
        doubleClickPending_ = false;
    }
    return true;
}

bool cGZWinListBox::GZOnMouseMove(int x, int y, std::uint32_t) {   // @ 0x100273354
    if (GZAmCapturing() && drop_ && (forwarding_ || IsPointInWindowWindowCoordinates(x, y))) {
        const int index = (y - textGutterY_) / lineHeight_ + topIndex_;
        if (selection_ != index && index < GetLineCount()) SetSelection(index, false);
    }
    return true;
}

bool cGZWinListBox::SetSelection(int index, bool notify) {   // @ 0x100273538
    if (index != -1) {
        if (!(index > -1 && index < GetLineCount())) {
            if (index < 0) index = 0;
            if (GetLineCount() <= index) index = GetLineCount() - 1;
        }
    }
    if (index != selection_) {
        selection_ = index;
        if (index > -1 && !IsIndexInView(index)) PutIndexInView(index);
    }
    if (notify && !drop_) EmitSelectionChangedMsg();
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinListBox::SetSelectionByID(std::uint32_t id, bool notify) {   // @ 0x100273628
    int index = 0;
    auto it = entries_.begin();
    for (; it != entries_.end() && it->id != id; ++it) ++index;
    if (it == entries_.end()) return false;
    if (index != selection_) {
        selection_ = index;
        if (index > -1 && !IsIndexInView(index)) PutIndexInView(index);
    }
    if (notify && !drop_) EmitSelectionChangedMsg();
    InvalidateSelfAndParents();
    return true;
}

std::uint32_t cGZWinListBox::GetSelectionID() const {   // @ 0x1002736e2
    if (selection_ > -1 && static_cast<std::uint32_t>(selection_) < count_) {
        auto it = entries_.begin();
        std::advance(it, selection_);
        return it->id;
    }
    return 0;
}

bool cGZWinListBox::IsIndexInView(int index) const {   // @ 0x100273750
    if (index < topIndex_) return false;
    return index < topIndex_ + visibleLines_;
}

void cGZWinListBox::PutIndexInView(int index) {   // @ 0x10027378a
    if (index < topIndex_) SetIndexOfTopLine(index);
    else if (visibleLines_ - 1 + topIndex_ < index) SetIndexOfTopLine(index - (visibleLines_ - 1));
    SetScrollBarValue();
    InvalidateSelfAndParents();
}

void cGZWinListBox::SetIndexOfTopLine(int index) {   // @ 0x1002737fa
    topIndex_ = index;
    const int count = GetLineCount();
    if (visibleLines_ < count && count - visibleLines_ < topIndex_) topIndex_ = count - visibleLines_;
    if (topIndex_ < 0) topIndex_ = 0;
    SetScrollBarValue();
    InvalidateSelfAndParents();
}

bool cGZWinListBox::ScrollToStart() {   // @ 0x100273896
    const int count = GetLineCount();
    if (count > 0) SetIndexOfTopLine(0);
    return count > 0;
}

bool cGZWinListBox::ScrollToEnd() {   // @ 0x100273908
    const int count = GetLineCount();
    if (count > 0) {
        int top = count - visibleLines_;
        if (top == 0 || count < visibleLines_) top = 0;
        SetIndexOfTopLine(top);
    }
    return true;
}

bool cGZWinListBox::MoveLineUp() {   // @ 0x10027395c
    if (GetLineCount() == 0 || selection_ < 1) return false;
    SetSelection(selection_ - 1, false);
    return true;
}

bool cGZWinListBox::MoveLineDown() {   // @ 0x1002739e6
    const int count = GetLineCount();
    if (count < 1 || count - 1 <= selection_) return false;
    SetSelection(selection_ + 1, false);
    return true;
}

bool cGZWinListBox::ScrollPageUp() {   // @ 0x100273a38
    if (GetLineCount() > 0) {
        int top = (topIndex_ + 1) - visibleLines_;
        if (top < 0) top = 0;
        SetIndexOfTopLine(top);
        SetSelection(top, false);
    }
    return true;
}

bool cGZWinListBox::ScrollPageDown() {   // @ 0x100273a9e
    const int old = topIndex_;
    const int count = GetLineCount();
    if (count > 0 && topIndex_ < count - visibleLines_) {
        const int top = old - 1 + visibleLines_;
        SetIndexOfTopLine(top);
        SetSelection(top + visibleLines_, false);
    }
    return true;
}

bool cGZWinListBox::ScrollToFirstLetter(char letter) {   // @ 0x100273b14
    int index = 0;
    for (const cListEntry& entry : entries_) {
        if (entry.text.BeginsWith(&letter, 1)) {
            SetSelection(index, true);
            return true;
        }
        ++index;
    }
    return false;
}

void cGZWinListBox::SetShouldSort() {   // @ 0x100273baa
    if (sorted_) return;
    sorted_ = true;
    Sort();
}

void cGZWinListBox::Sort() {   // @ 0x100273c04
    IWinNumberFormatter* language = WinNumberFormatter();
    cRZString selected;
    if (selection_ > -1) GetStringFromIndex(selection_, selected);
    std::vector<cListEntry> sorted(entries_.begin(), entries_.end());
    std::sort(sorted.begin(), sorted.end(), [language](const cListEntry& a, const cListEntry& b) {
        return (language ? language->CompareStrings(a.text, b.text, true) : a.text.CompareToString(b.text, true)) < 0;
    });
    auto it = entries_.begin();
    for (const cListEntry& entry : sorted) {
        it->text = entry.text;
        it->id = entry.id;
        ++it;
    }
    if (selection_ != -1) {
        int index = 0;
        GetIndexFromString(index, selected);
        SetSelection(index, true);
    }
}

bool cGZWinListBox::SetMinimumLineHeight(std::uint8_t height) {   // @ 0x100273dd6
    minLineHeight_ = height;
    CalculateAllMetrics();
    InvalidateSelfAndParents();
    return true;
}

int cGZWinListBox::GetItemIndexFromCursorPosition(int, int y, bool clamp) const {   // @ 0x100273e6c
    const int count = GetLineCount();
    if (count == 0) return -1;
    int v = clamp ? 0 : y;
    if (y > -1) v = y;
    if (GetH() <= v && clamp) v = GetH() - 1;
    int index = (v - textGutterY_) / lineHeight_ + topIndex_;
    if (count <= index) index = clamp ? count - 1 : -1;
    return index;
}

bool cGZWinListBox::ResizeBoxToFitContents() {   // @ 0x100273f28
    if (!drop_) return false;
    const int count = GetLineCount();
    fitHeight_ = true;
    if (count > 0 && visibleLines_ <= count) {
        const int needed = lineHeight_ * count + textGutterY_ * 2;
        cIGZWin* parent = GetParentWin();
        const int parentHeight = parent ? parent->GetH() : 0x7fffffff;
        const int limit = (parentHeight - GetT()) - 2;
        if (limit < needed) {
            SetSize(GetW(), limit);
            visibleLines_ = (limit - textGutterY_ * 2) / lineHeight_;
            AddScrollBar(true, false);
            ResizeWindowForExactLineHeights();
        } else {
            SetSize(GetW(), needed);
            visibleLines_ = count;
            SetIndexOfTopLine(0);
            RemoveScrollBar(true, false);
        }
        SetScrollBarSizes();
        SetScrollBarValue();
        InvalidateSelfAndParents();
        return true;
    }
    return false;
}

bool cGZWinListBox::ResizeBoxToFitContentsHorizontal() {   // @ 0x1002740a2
    if (!drop_) return false;
    fitWidth_ = true;
    std::uint32_t widest = 1;
    if (!entries_.empty()) {
        if (!font_ && Fonts()) SetFont(Fonts()->Font(kStockFontStyle));
        if (font_) {
            for (const cListEntry& entry : entries_) {
                std::uint32_t extra = 0;
                widest = std::max(widest, font_->MeasureTextWidth(entry.text.Data(), entry.text.Strlen(), &extra));
            }
        }
    }
    SetW(static_cast<int>(widest) + textGutterX_ * 2);
    return true;
}

bool cGZWinListBox::SetScrollBarImageVertical(cIGZBuffer* image, const cRZRect& area) {   // @ 0x100274200
    if (image) image->AddRef();
    if (scrollImageV_) scrollImageV_->Release();
    scrollImageV_ = image;
    scrollImageArea_ = area;
    Held<cIGZWinScrollbar> bar;
    if (GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, bar.out()) && bar.p && scrollImageV_) {
        const bool useArea = scrollImageArea_.right > scrollImageArea_.left && scrollImageArea_.bottom > scrollImageArea_.top;
        bar.p->ImageSetup(useArea ? scrollImageArea_ : *scrollImageV_->GetBufferArea(), scrollImageV_, 0);
    }
    return true;
}

bool cGZWinListBox::AddScrollBar(bool vertical, bool) {   // @ 0x10027432a
    if (!vertical || !scrollV_ || ChildExists(kListBoxVScrollId)) return true;
    IWinCtrlFactory* factory = WinCtrlFactory();
    cIGZWinScrollbar* bar = factory ? factory->CreateScrollbar(kListBoxVScrollId, 2, 0, 0, 0) : nullptr;
    if (bar && scrollImageV_) {
        const bool useArea = scrollImageArea_.right > scrollImageArea_.left && scrollImageArea_.bottom > scrollImageArea_.top;
        bar->ImageSetup(useArea ? scrollImageArea_ : *scrollImageV_->GetBufferArea(), scrollImageV_, 0);
    }
    if (!bar) return true;
    bar->AsIGZWin()->SetW(0x10);
    ChildAdd(bar->AsIGZWin());
    ChildToFront(bar->AsIGZWin());
    SetScrollBarSizes();
    SetScrollBarValue();
    bar->Release();
    return true;
}

void cGZWinListBox::RemoveScrollBar(bool vertical, bool horizontal) {   // @ 0x100274478
    if (vertical) {
        if (cIGZWin* bar = GetChildWindowFromID(kListBoxVScrollId)) ChildDelete(bar);
    }
    if (horizontal) {
        if (cIGZWin* bar = GetChildWindowFromID(kListBoxHScrollId)) ChildDelete(bar);
    }
}

void cGZWinListBox::SetScrollBarValue() {   // @ 0x1002744de
    Held<cIGZWinScrollbar> bar;
    if (!GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, bar.out()) || !bar.p || visibleLines_ == 0) return;
    const int range = std::max(GetLineCount() - visibleLines_, 0);
    bar.p->SetMinMax(0, range);
    bar.p->SetLineAndPageCount(static_cast<std::uint32_t>(range), static_cast<std::uint32_t>(GetLineCount() / visibleLines_ + 1));
    bar.p->SetValue(GetIndexOfTopLine());
}

void cGZWinListBox::SetScrollBarSizes() {   // @ 0x1002745aa
    Held<cIGZWinScrollbar> bar;
    if (!GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, bar.out()) || !bar.p) return;
    const int barWidth = bar.p->AsIGZWin()->GetW();
    bar.p->AsIGZWin()->SetArea(GetW() - barWidth, 0, GetW(), GetH());
}

void cGZWinListBox::GetTextArea(cRZRect& out) const {   // @ 0x10027466c
    out = GetArea();
    const int width = out.right - out.left, height = out.bottom - out.top;
    out = cRZRect{textGutterX_, textGutterY_, width - textGutterX_, height - textGutterY_};
    Held<cIGZWinScrollbar> bar;
    if (const_cast<cGZWinListBox*>(this)->GetChildAs(kListBoxVScrollId, kGZIID_cIGZWinScrollbar, bar.out()) && bar.p)
        out.right -= scrollGutterX_ + bar.p->AsIGZWin()->GetW();
}

bool cGZWinListBox::EndDialog(std::uint32_t result) {   // @ 0x100274772
    if (drop_) mgr_->EndDialog(0xf, this, static_cast<std::int64_t>(static_cast<std::int32_t>(result)));
    return drop_;
}

bool cGZWinListBox::PostOrSend(std::uint32_t command, std::uint32_t id) {
    if (postMessages_) PostMsg(notify_, kGZMsgCommand, command, id, GetSelectionID());
    else SendMsg(notify_, kGZMsgCommand, command, id, GetSelectionID());
    return true;
}

bool cGZWinListBox::EmitSelectionChangedMsg() { return PostOrSend(kGZMsgListBoxSelectionChanged, GetID()); }   // @ 0x1002747cc
bool cGZWinListBox::EmitDoubleClickMsg() { return PostOrSend(kGZMsgListBoxDoubleClick, GetID()); }           // @ 0x100274836

}  // namespace gz
