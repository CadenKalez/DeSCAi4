#include "gz/WinText.h"

#include <algorithm>
#include <string>

#include "gz/RZDraw.h"

namespace gz {

static IWinFontSource* g_fontSource = nullptr;
IWinFontSource* WinFontSource() { return g_fontSource; }
void SetWinFontSource(IWinFontSource* source) { g_fontSource = source; }

cGZWinText::cGZWinText(cIGZWinMgr* mgr) : cGZWin(mgr) {
    flags_ &= ~0x8000u;
}

cGZWinText::~cGZWinText() {
    if (font_) font_->Release();
}

bool cGZWinText::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinText) {
        *out = static_cast<cIGZWinText*>(this);
        AddRef();
        return true;
    }
    return cGZWin::QueryInterface(iid, out);
}

bool cGZWinText::Init() {
    if (get_init_flag()) return true;
    cGZWin::Init();
    if (!font_) {
        if (IWinFontSource* source = WinFontSource(); source && source->Available()) {
            IWinFont* font = source->Font(fontStyle_ != 0 ? fontStyle_ : kWinDefaultFontStyle);
            if (!font) font = source->Font(kWinDefaultFontStyle);
            SetFont(font);
        }
    }
    return true;
}

bool cGZWinText::Shutdown() {
    if (!get_init_flag()) return true;
    if (font_) {
        IWinFont* font = font_;
        font_ = nullptr;
        font->Release();
    }
    return cGZWin::Shutdown();
}

// Everything except key messages is handed to the parent (with the window's position added to mouse
// coordinates) when the pass-through flag is set.
bool cGZWinText::DoMessage(cGZMessage& message) {
    if (message.type - 5u < 2u) return cGZWin::DoMessage(message);
    if (!GetWinTextFlag(kTextPassThrough) || !parent_) return false;
    if (message.type - 7u < 8u) {
        message.param1 = static_cast<std::uint32_t>(GetL() + static_cast<int>(message.param1));
        message.param2 = static_cast<std::uint32_t>(GetT() + static_cast<int>(message.param2));
    }
    return parent_->DoMessage(message);
}

bool cGZWinText::GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) {
    SendMsg(notify_, kGZMsgKeyDown, key, modifiers, 0);
    return true;
}

bool cGZWinText::GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) {
    SendMsg(notify_, kGZMsgKeyUp, key, modifiers, 0);
    return true;
}

bool cGZWinText::SetWinTextFlag(int flag, bool on) {
    textFlags_ = on ? (textFlags_ | flag) : (textFlags_ & ~static_cast<std::uint32_t>(flag));
    if (flag == kTextOpaque) {
        SetFlag(kWinPrivateBufferTransparent, !on);
        SetFlag(0x40000, GetFlag(kWinPrivateBufferTransparent));
    } else if (flag == kTextWrap) {
        CalcTextPos();
    } else {
        return true;
    }
    InvalidateSelfAndParents();
    return true;
}

// Places the text rectangle for the current alignment: the left/right column uses the whole width, the vertical
// position comes from the measured height.
void cGZWinText::CalcTextPos() {
    if (caption_.Strlen() == 0 || alignment_ >= 9) return;
    if ((0x49u >> alignment_) & 1) {         // top row: the window's own area, turned into local coordinates
        cRZRect area;
        GetArea(area);
        textRect_ = area;
        const int left = textRect_.left, top = textRect_.top;
        textRect_.left = gutterX_;
        textRect_.right = (textRect_.right - gutterX_) - left;
        textRect_.top = gutterY_;
        textRect_.bottom = (textRect_.bottom - gutterY_) - top;
        return;
    }
    cRZRect min;
    GetMinTextArea(min, false, false);
    int y;
    if ((0x92u >> alignment_) & 1) y = ((GetH() - min.bottom) + min.top) / 2;     // middle
    else y = ((GetH() - min.bottom) + min.top) - gutterY_;                         // bottom
    const int x = gutterX_;
    textRect_.left = x;
    textRect_.top = y;
    textRect_.right = GetW() - gutterX_;
    textRect_.bottom = (min.bottom + y) - min.top;
}

// The smallest rectangle the caption needs: single line, or wrapped to the window (a), to a long limit (a) or to
// the widest explicit line (a and b).
void cGZWinText::GetMinTextArea(cRZRect& out, bool a, bool b) {
    if (!font_) {
        out = {0, 0, 1, 1};
        return;
    }
    const char* text = caption_.Data();
    const std::uint32_t length = caption_.Strlen();
    if (!(textFlags_ & kTextWrap)) {
        font_->MeasureText(text, length, out);
        return;
    }
    if (!a) {
        const int w = GetW();
        const int available = w - gutterX_ * 2;
        const int width = (available != 0 && gutterX_ * 2 <= w) ? available : 1;
        const int lines = font_->CountLines(text, length, width);
        out = {0, 0, width, font_->LineHeight(0, 0) * lines};
        return;
    }
    if (b) {
        const std::string str(text, length);
        int widest = 1;
        std::size_t start = 0, newline = str.find('\n');
        if (newline < str.size()) {
            while (true) {
                const std::size_t at = newline;
                widest = std::max(widest, font_->TextWidth(text + start, static_cast<std::uint32_t>(at - start)));
                newline = str.find('\n', at + 1);
                start = at;
                if (newline >= str.size()) {
                    widest = std::max(widest, font_->TextWidth(text + at, static_cast<std::uint32_t>(str.size() - at)));
                    break;
                }
            }
        } else {
            widest = font_->TextWidth(text, length);
        }
        const int lines = font_->CountLines(text, length, widest);
        out = {0, 0, widest, font_->LineHeight(0, 0) * lines};
        return;
    }
    const int lineHeight = font_->LineHeight(0, 0);
    const int limit = lineHeight * 60;
    const int lines = font_->CountLines(text, length, limit);
    int width = font_->TextWidth(text, length);
    if (limit <= width) width = limit;
    if (lines > 1) width = limit;
    out = {0, 0, width, lineHeight * lines};
}

bool cGZWinText::FitWindowToText(bool a, bool b) {
    cRZRect min;
    GetMinTextArea(min, a, b);
    min.right += gutterX_ * 2;
    min.bottom += gutterY_ * 2;
    SetSize(min.right - min.left, min.bottom - min.top);
    return true;
}

void cGZWinText::SetFillColor(std::uint32_t nativeColor) {
    cGZWin::SetFillColor(nativeColor);
    InvalidateSelfAndParents();
}

bool cGZWinText::SetTextColor(tFontColorIdx index, std::uint32_t native) {
    return SetTextColor(index, cRZColor{ConvertNativeToPackedRGB(native)});
}

bool cGZWinText::SetTextColor(tFontColorIdx index, cRZColor color) {
    if (index != kColorText && index != kColorShadow) return false;
    colors_[index] = color.rgb;
    bool defaults = false;
    if (font_) {
        defaults = colors_[0] == ConvertNativeToPackedRGB(font_->DefaultTextColor()) &&
                   colors_[1] == ConvertNativeToPackedRGB(font_->DefaultShadowColor());
    }
    SetWinTextFlag(kTextDefaultColors, defaults);
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinText::GetTextColor(tFontColorIdx index, std::uint32_t& native) const {
    if (index >= 2 || colors_[index] == kWinUnsetTextColor) return false;
    native = ConvertPackedRGBToNative(colors_[index]);
    return true;
}

bool cGZWinText::GetTextColor(tFontColorIdx index, cRZColor& color) const {
    if (index >= 2 || colors_[index] == kWinUnsetTextColor) return false;
    color.rgb = colors_[index];
    return true;
}

bool cGZWinText::ClearTextColor(tFontColorIdx index) {
    if (index == kColorShadow) return SetTextColor(kColorShadow, font_->DefaultShadowColor());
    if (index == kColorText) return SetTextColor(kColorText, font_->DefaultTextColor());
    return false;
}

bool cGZWinText::SetFont(IWinFont* font) {
    if (font_ == font) return true;
    if (font) font->AddRef();
    IWinFont* old = font_;
    font_ = font;
    if (old) old->Release();
    if (font_) {
        if (colors_[0] == kWinUnsetTextColor) SetTextColor(kColorText, font_->DefaultTextColor());
        if (colors_[1] == kWinUnsetTextColor) SetTextColor(kColorShadow, font_->DefaultShadowColor());
    }
    SetTextColor(kColorText, cRZColor{colors_[0]});
    SetTextColor(kColorShadow, cRZColor{colors_[1]});
    CalcTextPos();
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinText::SetFontStyle(std::uint32_t styleId) {
    fontStyle_ = styleId;
    if (get_init_flag()) {
        if (IWinFontSource* source = WinFontSource()) {
            if (IWinFont* font = source->Font(fontStyle_ != 0 ? fontStyle_ : kWinDefaultFontStyle)) SetFont(font);
        }
    }
    return true;
}

bool cGZWinText::GetFontStyle(std::uint32_t& styleId) {
    if (fontStyle_ == 0) return false;
    styleId = fontStyle_;
    return true;
}

bool cGZWinText::SetGutters(std::int8_t x, std::int8_t y) {
    gutterX_ = x;
    gutterY_ = y;
    CalcTextPos();
    return true;
}

void cGZWinText::SetOutlineUse(bool on, std::uint32_t color) {
    textFlags_ = on ? (textFlags_ | kTextOutline) : (textFlags_ & ~static_cast<std::uint32_t>(kTextOutline));
    outlineColor_ = color;
}

void cGZWinText::SetCaption(const cIGZString& caption) {
    cGZWin::SetCaption(caption);
    CalcTextPos();
    InvalidateSelfAndParents();
}

void cGZWinText::SetArea(int l, int t, int r, int b) {
    const int w = std::max(0, r - l), h = std::max(0, b - t);
    const int oldW = GetW(), oldH = GetH();
    cGZWin::SetArea(l, t, r, b);
    if (w == oldW && h == oldH) return;
    CalcTextPos();
}

bool cGZWinText::SetAlignment(std::uint32_t alignment) {
    if (alignment_ == alignment) return true;
    if (alignment >= 9) return false;
    const std::uint32_t horizontal[9] = {0, 0, 0, 1, 1, 1, 2, 2, 2};
    horizontalAlign_ = horizontal[alignment];
    alignment_ = alignment;
    CalcTextPos();
    InvalidateSelfAndParents();
    return true;
}

bool cGZWinText::GZPaint() {
    if (textFlags_ & kTextOpaque) {
        Context()->SetColor(GetFillColorRGB());
        Context()->Fill(drawArea_);
    }
    if (font_ && caption_.Strlen() != 0) {
        const cRZRect box{drawArea_.left + textRect_.left, drawArea_.top + textRect_.top,
                          drawArea_.left + textRect_.right, drawArea_.top + textRect_.bottom};
        std::uint32_t c1 = kBtnColorUnset, c2 = kBtnColorUnset;
        if (!(textFlags_ & kTextDefaultColors)) {
            c1 = ConvertPackedRGBToNative(colors_[0]);
            c2 = ConvertPackedRGBToNative(colors_[1]);
        }
        font_->DrawTextBox(drawBuffer_, box, caption_.Data(), caption_.Strlen(), horizontalAlign_, c1, c2, nullptr, 0);
    }
    if (textFlags_ & kTextOutline) RZDrawRectangleOutline(Context(), drawArea_, outlineColor_, 1);
    return true;
}

}  // namespace gz
