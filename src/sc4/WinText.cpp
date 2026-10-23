// cSC4WinText — see sc4/WinText.h for the recovered behaviour, the offsets and the quirks kept.
#include "sc4/WinText.h"

#include <vector>

#include "gz/RZDraw.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinCtrlMgr.h"
#include "rz/cRZString.h"

namespace sc4 {

namespace {
// The text colours whose 0x8000000 value tells DrawTextBox to use the font's own colours (the same sentinel
// cGZWinText::GZPaint passes as kBtnColorUnset).
constexpr std::uint32_t kFontOwnColor = 0x8000000;
constexpr const char* kFallbackEllipsis = "...";   // _SZ_DEFAULT_ELLIPSIS_TEXT
}  // namespace

cSC4WinText::cSC4WinText(cIGZWinMgr* mgr) : cGZWinText(mgr) {}   // the original runs cGZWinText's ctor
cSC4WinText::~cSC4WinText() = default;                            // @ 0x1005c3a90 (+0x1005c3a9a deleting)

bool cSC4WinText::GZPaint() {   // @ 0x1005c333c
    IWinBuffer* buffer = drawBuffer_;
    if (!buffer) return true;

    // Opaque background: Lock(1|0x8000), Fill(GetFillColor(), drawArea_, null), Unlock (0x1005c3363..0x1005c33b6).
    // The original's Fill third argument is the clip rectangle and is null.
    if ((textFlags_ & gz::kTextOpaque) && buffer->Lock(0x8001)) {
        buffer->Fill(GetFillColor(), drawArea_);
        buffer->Unlock(0x8001);
    }

    if (font_ && caption_.Strlen() != 0) {
        // RZ::WCM() vslot 0x118: DefaultText(0x5377be31), the ellipsis string (0x1005c33e2..0x1005c3438).
        cRZString ellipsis;
        if (gz::cIGZWinCtrlMgr* manager = gz::WinCtrlMgr()) {
            if (const cIGZString* text = manager->DefaultText(kWinCtrlEllipsisTextId)) ellipsis.Copy(*text);
        } else {
            ellipsis.AssignFromCString(kFallbackEllipsis);   // the original dereferences the missing manager here
        }

        std::uint32_t color1 = kFontOwnColor, color2 = kFontOwnColor;
        if (!(textFlags_ & gz::kTextDefaultColors)) {   // 0x10000000: the colours equal the font's own
            color1 = ConvertPackedRGBToNative(colors_[0]);
            color2 = ConvertPackedRGBToNative(colors_[1]);
        }

        if (!(textFlags_ & gz::kTextWrap)) {
            // One line: ellipsize a copy of the caption into the text rectangle and draw it (0x1005c3494..0x1005c359b).
            cRZString text(caption_);
            font_->FormatForEllipsis(text, ellipsis, textRect_, false);
            const cRZRect box{drawArea_.left + textRect_.left, drawArea_.top + textRect_.top,
                              drawArea_.left + textRect_.right, drawArea_.top + textRect_.bottom};
            font_->DrawTextBox(buffer, box, text.Data(), text.Strlen(), horizontalAlign_, color1, color2, nullptr, 0);
        } else {
            // Wrapped: ask the font for the bytes that fit one line, up to as many lines as the area holds
            // (0x1005c35a0..0x1005c36ef).
            const int lineHeight = font_->GlyphHeight();   // cIGZFont [35] 0x118
            // The original divides by the font's line height without a test; a font that is not loaded would trap.
            const int lineCount = lineHeight > 0 ? (area_.bottom - area_.top) / lineHeight : 0;
            const int wrapWidth = (area_.right - area_.left) - gutterX_ * 2;
            std::vector<cTextLine> lines;
            bool cut = false;   // rbp-0x38: the last line does not hold the rest of the text
            if (lineCount >= 1) {
                const char* cursor = caption_.Data();
                const char* end = cursor + caption_.Strlen();
                int left = lineCount;
                while (true) {
                    const std::uint32_t available = static_cast<std::uint32_t>(end - cursor);
                    const std::uint32_t fit = font_->WordsToFitInWidth(cursor, available, wrapWidth, nullptr, 2);
                    cut = true;
                    if (fit == 0) break;
                    lines.push_back(cTextLine{cursor, fit, 0});
                    if ((left == 1 && fit < available) || left < 2) break;
                    cut = false;
                    cursor += fit;
                    --left;
                    if (cursor == end) break;
                }
            }

            // Vertical slack from the window area, the box itself from the draw area (quirk, see the header).
            int slack = ((area_.bottom - area_.top) - gutterY_ * 2) -
                        static_cast<int>(lines.size()) * lineHeight;
            if (alignment_ <= 7) {
                if ((0x49u >> alignment_) & 1) slack = 0;            // top row
                else if ((0x92u >> alignment_) & 1) slack >>= 1;     // middle row
            }
            int left = drawArea_.left + gutterX_;
            int top = drawArea_.top + gutterY_ + slack;
            const int right = drawArea_.right - gutterX_;
            int bottom = top + lineHeight;

            for (std::size_t i = 0; i < lines.size(); ++i) {
                const cRZRect box{left, top, right, bottom};
                const bool lastLine = (i + 1 == lines.size());
                if (cut && lastLine) {
                    // 0x1005c37c5..0x1005c3940: keep what fits next to the ellipsis and append it. A line with no
                    // room for the ellipsis at all is not drawn (0x1005c3817).
                    const int ellipsisWidth = font_->TextWidth(ellipsis.Data(), ellipsis.Strlen());
                    const int available = (right - left) - ellipsisWidth;
                    if (available >= 0) {
                        const std::uint32_t fit = font_->FitLength(lines[i].text, lines[i].length, available, 0);
                        cRZString text;
                        text.AssignFromChars(lines[i].text, fit);
                        text.AppendString(ellipsis);
                        font_->DrawTextBox(buffer, box, text.Data(), text.Strlen(), horizontalAlign_, color1,
                                           color2, nullptr, 0);
                    }
                } else {
                    font_->DrawTextBox(buffer, box, lines[i].text, lines[i].length, horizontalAlign_, color1,
                                       color2, nullptr, 0);
                }
                top += lineHeight;
                bottom += lineHeight;
            }
        }
    }

    // Outline: the original calls DrawRectangleOutline(buffer, drawArea_, outlineColor_, 1) after locking the buffer
    // with 0x8008 (0x1005c39d6..0x1005c3a24); the recoverable port draws through the buffer's draw context, like the
    // other window paints that go through the IWinBuffer seam.
    if ((textFlags_ & gz::kTextOutline) && buffer->Lock(0x8008)) {
        RZDrawRectangleOutline(Context(), drawArea_, outlineColor_, 1);
        buffer->Unlock(0x8008);
    }
    return true;
}

// GZCOM_CreatecSC4WinText @ 0x1005c32dc: 0x198 bytes, cGZWinText's constructor, answers the cIGZWinText view (+0x150).
gz::cIGZWinText* CreateSC4WinText(cIGZWinMgr* mgr) {
    auto* window = new cSC4WinText(mgr);
    return static_cast<gz::cIGZWinText*>(window);
}

}  // namespace sc4
