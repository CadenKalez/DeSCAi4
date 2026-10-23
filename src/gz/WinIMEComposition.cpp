#include "gz/WinIMEComposition.h"

#include <algorithm>
#include <string>

#include "gz/DrawContext.h"
#include "gz/WinIMEServer.h"
#include "gz/WinStyle.h"
#include "gz/cGZMessageServer.h"
#include "rz/CharLength.h"
#include "rz/StringConversion.h"
#include "rz/cRZSysServPtr.h"

namespace gz {

namespace {

constexpr std::uint32_t kCompositionWindowID = 0x0ac57ece;
constexpr int kChineseLanguage = 0x14;

// The original hands the cGZWin* to cIGZMessageServer::AddNotification directly. cIGZWin's third slot is
// DoMessage, exactly like cIGZMessageTarget's (0x18), so the message server can use the window through that
// layout-compatible target view; this cast is that call site's implicit conversion.
cIGZMessageTarget* AsMessageTarget(cGZWin* window) {
    return reinterpret_cast<cIGZMessageTarget*>(window);
}

}  // namespace

cGZWinIMEComposition::cGZWinIMEComposition(cIGZWinMgr* mgr) : cGZWinTextEdit(mgr, 0x11ff) {}

cGZWinIMEComposition::~cGZWinIMEComposition() {
    if (targetTextEdit_) {
        targetTextEdit_->Release();
        targetTextEdit_ = nullptr;
    }
    if (focusTarget_) {
        focusTarget_->Release();
        focusTarget_ = nullptr;
    }
}

bool cGZWinIMEComposition::QueryInterface(std::uint32_t iid, void** out) {
    if (iid == kGZIID_cIGZWinIMEComposition) {
        *out = static_cast<cIGZWinIMEComposition*>(this);
        AddRef();
        return true;
    }
    return cGZWinTextEdit::QueryInterface(iid, out);
}

std::uint32_t cGZWinIMEComposition::AddRef() { return cGZWinTextEdit::AddRef(); }

std::uint32_t cGZWinIMEComposition::Release() { return cGZWinTextEdit::Release(); }

cIGZWin* cGZWinIMEComposition::AsIGZWin() { return static_cast<cIGZWin*>(static_cast<cGZWin*>(this)); }

bool cGZWinIMEComposition::Init() {   // 0x1001f8e4a
    if (get_init_flag()) return true;
    cGZWin* self = static_cast<cGZWin*>(this);
    self->SetFlag(kWinPrivateBuffer, true);
    SetID(kCompositionWindowID);
    ClearFlag(kTESingleLine);
    ClearFlag(kTEOutline);
    ClearFlag(kTEEditable);
    SetCaretVisible(true);
    ClearFlag(kTEClipboard);
    SetTextGutterX(0);
    SetTextGutterY(0);
    if (!GetFont()) {
        // Original: RZGetFramework()->GetSystemService(0xc2c2eb0f, 0x22c2eb1f) -> cIGZWinCtrlMgr vslot 0x28
        // SystemFont(0x68963c4c). The recovered font seam is IWinStyleManager's GetFontStyle, the same style id
        // and the lookup cGZWinTextEdit::Init already uses.
        if (IWinStyleManager* styles = WinStyleManager())
            SetFont(static_cast<cIGZFont*>(styles->GetFontStyle(kTextEditDefaultFontStyle)));
    }
    cGZWinTextEdit::Init();

    // Pick up the window that currently has focus (cIGZWinMgr vslot 0x120 GZGetFocus) and, when it is a text edit,
    // the cIGZWinTextEdit view (iid 0x231a1c57) this composition window mirrors.
    if (cIGZWinMgr* mgr = GetWindowManager()) {
        cIGZWin* focus = mgr->GZGetFocus();
        if (focus != focusTarget_) {
            if (focus) focus->AddRef();
            cIGZWin* old = focusTarget_;
            focusTarget_ = focus;
            if (old) old->Release();
        }
        if (focus) {
            if (targetTextEdit_) {
                targetTextEdit_->Release();
                targetTextEdit_ = nullptr;
            }
            void* out = nullptr;
            if (focus->QueryInterface(kGZIID_cIGZWinTextEdit, &out))
                targetTextEdit_ = static_cast<cIGZWinTextEdit*>(out);
        }
    }

    SetWindowColors();
    SetWindowPosition();
    GZSetCapture();

    // Register for the three IME messages (message server 0x64063387 / iid 0xc1085722, vslot 0x28).
    cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
    if (server) {
        cIGZMessageTarget* target = AsMessageTarget(self);
        server->AddNotification(target, kGZMsgIMECompositionData);
        server->AddNotification(target, kGZMsgIMECompositionCaret);
        server->AddNotification(target, kGZMsgIMECompositionEnd);
    }
    return true;
}

bool cGZWinIMEComposition::Shutdown() {   // 0x1001f90e0
    if (!get_init_flag()) return true;
    cIGZWinMgr* mgr = GetWindowManager();
    if (mgr && GZAmCapturing()) GZReleaseCapture();
    if (focusTarget_) {
        // The original restores focus to the mirrored window before dropping its reference (vslot 0x128).
        if (mgr) mgr->GZSetFocus(focusTarget_);
        focusTarget_->Release();
        focusTarget_ = nullptr;
    }
    if (targetTextEdit_) {
        targetTextEdit_->Release();
        targetTextEdit_ = nullptr;
    }
    cRZSysServPtr<cIGZMessageServer, kGZSID_cGZMessageServer, kGZIID_cIGZMessageServer> server;
    if (server) {
        cIGZMessageTarget* target = AsMessageTarget(static_cast<cGZWin*>(this));
        server->RemoveNotification(target, kGZMsgIMECompositionData);
        server->RemoveNotification(target, kGZMsgIMECompositionCaret);
        server->RemoveNotification(target, kGZMsgIMECompositionEnd);
    }
    return cGZWinTextEdit::Shutdown();
}

bool cGZWinIMEComposition::DoMessage(cGZMessage& message) {   // 0x1001f9226
    if (message.type == kGZMsgCharacter) {
        if (focusTarget_) return focusTarget_->DoMessage(message);
    } else if (message.type == kGZMsgIMECompositionData) {
        auto* data = reinterpret_cast<cGZIMECompositionStringData*>(message.param1);
        SetCompositionString(data);
        if (data && language_ != data->language) {
            language_ = data->language;
            SetWindowColors();
        }
    } else if (message.type == kGZMsgIMECompositionCaret) {
        SetCompositionCaretPosition(reinterpret_cast<cGZIMECompositionStringData*>(message.param1));
    } else if (message.type == kGZMsgIMECompositionEnd) {
        SetWindowPosition();
    }
    return cGZWin::DoMessage(message);
}

bool cGZWinIMEComposition::GZPaint() {   // 0x1001f9b12
    cGZWinTextEdit::GZPaint();
    if (language_ == kChineseLanguage) return true;   // the Chinese language has no composition underlines

    cIGZFont* font = GetFont();
    if (!font) return true;   // the original dereferences the font without a check
    const int fontHeight = font->CharHeight(nullptr, 0);       // cIGZFont vslot 0x128
    const int aboveBaseline = font->GetSizeAboveBaseline();    // cIGZFont vslot 0x130
    const int underlineOffset = aboveBaseline + fontHeight / 5;

    auto* context = static_cast<cIGZDrawContext*>(GetDrawContext());
    const cRZRect& area = GetAreaToDrawTo();
    const char* text = GetText();
    const std::uint32_t textLength = GetTextLength();
    std::uint32_t offset = 0;
    for (const std::uint8_t attribute : attributes_) {
        const int charLength = GetCharLength(text + offset, textLength - offset, -2);
        if (attribute != 0 && context) {
            std::uint32_t line = 0, column = 0;
            cGZWinTextEdit::GetLineAndColumnFromIndex(line, column, offset);   // vslot 0x680
            int x = 0, y = 0;
            cGZWinTextEdit::GetPositionFromLineAndColumn(line, column, x, y);  // vslot 0x678
            // The binary compares the offset as unsigned against drawArea.bottom and otherwise falls back to
            // bottom - 1 (which wraps for a zero-height area); keep that arithmetic.
            int offsetY = underlineOffset;
            if (static_cast<std::uint32_t>(underlineOffset) < static_cast<std::uint32_t>(area.bottom))
                offsetY = underlineOffset;
            else
                offsetY = area.bottom - 1;
            y += offsetY;
            const int x1 = x + area.left;
            const int x2 = x1 + font->GetAdvanceWidth(text + offset, static_cast<std::uint32_t>(charLength));   // vslot 0x140
            int y1 = y + area.top;
            int y2 = y1;
            const std::uint32_t index = attribute < 4 ? attribute : 0;
            context->SetColor(ConvertNativeToPackedRGB(attributeColors_[index]));   // vslot 0xa0
            int thickness = (attribute == 2) ? 2 : 1;
            while (thickness-- > 0) context->Line(x1, y1++, x2, y2++);               // vslot 0x110
        }
        offset += static_cast<std::uint32_t>(charLength);
    }
    return true;
}

cIGZFont* cGZWinIMEComposition::GetFont() const { return cGZWinTextEdit::GetFont(); }

void cGZWinIMEComposition::SetFont(cIGZFont* font) { cGZWinTextEdit::SetFont(font); }

bool cGZWinIMEComposition::GetCompositionFontColor(int index, std::uint32_t& native) const {   // 0x1001f9dc4
    if (index < 8) return cGZWinTextEdit::GetFontColor(static_cast<cIGZWinTextEdit::ColorIndex>(index), native);
    return false;
}

bool cGZWinIMEComposition::SetCompositionFontColor(int index, std::uint32_t native) {   // 0x1001f9ddc
    if (index < 8) {
        cGZWinTextEdit::SetFontColor(static_cast<cIGZWinTextEdit::ColorIndex>(index), native);
        compositionColors_[index] = native;
        return true;
    }
    return false;
}

bool cGZWinIMEComposition::GetCaretColor(std::uint32_t& native) const { return cGZWinTextEdit::GetCaretColor(native); }

void cGZWinIMEComposition::SetCaretColor(std::uint32_t native) {   // 0x1001f9e48, quirk: never sets it
    std::uint32_t value = native;
    cGZWinTextEdit::GetCaretColor(value);
}

bool cGZWinIMEComposition::GetCaretPeriod(std::uint32_t& ms) const { return cGZWinTextEdit::GetCaretPeriod(ms); }

bool cGZWinIMEComposition::SetCaretPeriod(std::uint32_t ms) { return cGZWinTextEdit::SetCaretPeriod(ms); }

void cGZWinIMEComposition::GetOutlineUse(bool& on, std::uint32_t& native) const {
    cGZWinTextEdit::GetOutlineUse(on, native);
}

void cGZWinIMEComposition::SetOutlineUse(bool on, std::uint32_t native) { cGZWinTextEdit::SetOutlineUse(on, native); }

std::uint32_t cGZWinIMEComposition::GetTextGutterX() { return cGZWinTextEdit::GetTextGutterX(); }

void cGZWinIMEComposition::SetTextGutterX(std::uint32_t gutter) { cGZWinTextEdit::SetTextGutterX(gutter); }

std::uint32_t cGZWinIMEComposition::GetTextGutterY() { return cGZWinTextEdit::GetTextGutterY(); }

void cGZWinIMEComposition::SetTextGutterY(std::uint32_t gutter) { cGZWinTextEdit::SetTextGutterY(gutter); }

bool cGZWinIMEComposition::ResizeWindowForExactLineHeights() {
    return cGZWinTextEdit::ResizeWindowForExactLineHeights();
}

const char* cGZWinIMEComposition::GetText() { return cGZWinTextEdit::GetText(); }

std::uint32_t cGZWinIMEComposition::GetTextLength() { return cGZWinTextEdit::GetTextLength(); }

void cGZWinIMEComposition::GetText(cIGZString& out) { cGZWinTextEdit::GetText(out); }

void cGZWinIMEComposition::GetText(char* buffer, std::uint32_t& length) {
    cGZWinTextEdit::GetText(buffer, length);
}

void cGZWinIMEComposition::SetCompositionFlag(int flag) {   // 0x1001fa1d2
    if ((compositionFlags_ & static_cast<std::uint32_t>(flag)) != 0) return;
    // Quirk: the OR uses the composition flag word, not the current text-edit flags, and +0x3d0 is not updated.
    cGZWinTextEdit::SetFlags(compositionFlags_ | static_cast<std::uint32_t>(flag));
}

void cGZWinIMEComposition::SetCompositionFlags(int flags) {   // 0x1001fa1f8
    const std::uint32_t value = static_cast<std::uint32_t>(flags);
    const std::uint32_t old = compositionFlags_;
    if (old == value) return;
    compositionFlags_ = value;
    if (get_init_flag() && ((old ^ value) & 1) != 0) {
        if ((compositionFlags_ & 1) == 0)
            ClearFlag(kTEOutline);
        else
            SetFlag(kTEOutline);
    }
}

void cGZWinIMEComposition::ClearCompositionFlag(int flag) {   // 0x1001fa272
    if ((compositionFlags_ & static_cast<std::uint32_t>(flag)) == 0) return;
    // Quirk: same as SetCompositionFlag; +0x3d0 keeps its old value.
    cGZWinTextEdit::SetFlags(compositionFlags_ & ~static_cast<std::uint32_t>(flag));
}

bool cGZWinIMEComposition::IsCompositionFlagSet(int flag) {
    return (compositionFlags_ & static_cast<std::uint32_t>(flag)) != 0;
}

std::uint32_t cGZWinIMEComposition::GetCompositionFlags() { return compositionFlags_; }

void cGZWinIMEComposition::SetWindowPosition() {   // 0x1001f92f8
    int x = 2, y = 2;
    if (!focusTarget_) {
        // No target: the original moves to (2, 2).
        GZWinMoveTo(x, y);
    } else if (!targetTextEdit_) {
        // A focus window without a text edit: (2, 2) in its coordinates, expressed in the composition's parent.
        focusTarget_->WindowToWindowCoordinates(GetParentWin(), x, y);
        GZWinMoveTo(x, y);
    } else {
        if (cIGZFont* font = targetTextEdit_->GetFont()) SetFont(font);
        std::uint32_t line = 0, column = 0;
        targetTextEdit_->GetInsertionLineAndColumn(line, column);   // vslot 0x228
        int px = 0, py = 0;
        targetTextEdit_->GetPositionFromLineAndColumn(line, column, px, py);   // vslot 0x1e0
        if (px > focusTarget_->GetW()) px = focusTarget_->GetW();
        if (py > focusTarget_->GetH()) py = focusTarget_->GetH();
        focusTarget_->WindowToWindowCoordinates(GetParentWin(), px, py);       // vslot 0x1e8
        GZWinMoveTo(px, py);
    }
    ResizeWindowForExactLineHeights();
    if (language_ != kChineseLanguage) SetH(GetH() + 2);   // quirk: the composition window is two pixels taller
}

void cGZWinIMEComposition::SetWindowColors() {   // 0x1001f94a2
    SetCompositionFontColor(0, MakeFillColor(0, 0, 0));
    SetCompositionFontColor(1, MakeFillColor(0xff, 0xff, 0xff));
    SetCompositionFontColor(2, MakeFillColor(0, 0, 0));
    SetCompositionFontColor(3, MakeFillColor(0xff, 0xff, 0xff));
    SetCompositionFontColor(4, MakeFillColor(0, 0, 0));
    SetCompositionFontColor(5, MakeFillColor(0xff, 0xff, 0xff));
    SetCompositionFontColor(6, MakeFillColor(0xff, 0, 0));
    SetCompositionFontColor(7, MakeFillColor(0x80, 0x80, 0x80));
    attributeColors_[0] = MakeFillColor(0xff, 0xff, 0xff);
    attributeColors_[1] = MakeFillColor(0, 0, 0);
    attributeColors_[2] = MakeFillColor(0, 0, 0);
    attributeColors_[3] = MakeFillColor(0xc0, 0, 0);
    cGZWinTextEdit::SetFillColor(MakeFillColor(0xff, 0xff, 0xff));

    if (targetTextEdit_) {
        // Mirror the target's first six font colours. Quirk: colour 0 lands in attribute colours 1 and 2,
        // colour 1 in attribute colour 0.
        std::uint32_t color = 0;
        targetTextEdit_->GetFontColor(cIGZWinTextEdit::kColorText, color);
        cGZWinTextEdit::SetFontColor(cIGZWinTextEdit::kColorText, color);
        attributeColors_[1] = color;
        attributeColors_[2] = color;
        targetTextEdit_->GetFontColor(cIGZWinTextEdit::kColorTextBack, color);
        cGZWinTextEdit::SetFontColor(cIGZWinTextEdit::kColorTextBack, color);
        attributeColors_[0] = color;
        for (int i = 2; i < 6; ++i) {
            targetTextEdit_->GetFontColor(static_cast<cIGZWinTextEdit::ColorIndex>(i), color);
            cGZWinTextEdit::SetFontColor(static_cast<cIGZWinTextEdit::ColorIndex>(i), color);
        }
        cGZWinTextEdit::SetFillColor(focusTarget_->GetFillColor());
    }

    if (language_ == kChineseLanguage) {
        std::uint32_t color = 0;
        cGZWinTextEdit::GetFontColor(cIGZWinTextEdit::kColorSelectedText, color);
        cGZWinTextEdit::SetFontColor(cIGZWinTextEdit::kColorText, color);
        color = cGZWinTextEdit::GetHighlightFillColor();
        cGZWinTextEdit::SetFontColor(cIGZWinTextEdit::kColorTextBack, color);
        cGZWinTextEdit::SetFillColor(color);
    }
}

void cGZWinIMEComposition::SetCompositionString(cGZIMECompositionStringData* data) {   // 0x1001f985c
    if (!data) return;
    if (data->text) {
        const std::u16string wide(reinterpret_cast<const char16_t*>(data->text), data->length);
        cRZString text;
        ConvertStringEncoding(wide.data(), data->length, 0x10, text);
        const int gutter = static_cast<int>(GetTextGutterX());
        const int width = GetFont() ? GetFont()->GetStringWidth(text.Data(), text.Strlen()) : 0;   // vslot 0x180
        cGZWinTextEdit::SetW(width + 4 + gutter * 2);
        cGZWinTextEdit::SetText(text, false);
    }
    if (data->attributes) {
        attributes_.resize(data->length);
        for (std::uint32_t i = 0; i < data->length; ++i) {
            const std::uint8_t attribute = data->attributes[i];
            attributes_[i] = attribute < 4 ? attribute : 0;   // the original zeroes attribute bytes above 3
        }
    }
    SetCompositionCaretPosition(data);
}

void cGZWinIMEComposition::SetCompositionCaretPosition(cGZIMECompositionStringData* data) {   // 0x1001f9a7c
    if (!data) return;
    std::uint32_t index = 0;
    if (data->caret != 0) {
        const char* text = GetText();
        const std::uint32_t length = GetTextLength();
        std::uint32_t count = 0;
        // The original loops while count < caret; GetCharLength answers 0 at the end of the text, which would
        // spin forever. Stop at the end (the only reachable difference for a valid caret).
        while (count < data->caret && index < length) {
            const int charLength = GetCharLength(text + index, length - index, -2);
            if (charLength <= 0) break;
            index += static_cast<std::uint32_t>(charLength);
            ++count;
        }
    }
    cGZWinTextEdit::SetInsertionIndex(index);
}

}  // namespace gz
