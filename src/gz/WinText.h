// cGZWinText — a static text label (0x10027e5e0..0x10027fa12, iid 0x212cdc1f for the text view at +0x150).
// Layout: the 0x150-byte cGZWin, +0x150 the cIGZWinText view, +0x158 font, +0x160 font style, +0x164 gutters
// (x, y; signed bytes, default 2), +0x166 text offsets, +0x168 text rectangle (inside the window, recomputed by
// calc_text_pos), +0x178 text flags (6), +0x17c outline colour, +0x184 alignment (0..8), +0x188 text colours
// (packed RGB, 0xdeadbeef = use the font's), +0x190 horizontal alignment handed to the font.
// Alignment index a: a / 3 is the horizontal alignment (0 left, 1 centre, 2 right), a % 3 the vertical one
// (0 top, 1 middle, 2 bottom).
// Text flags: 1 pass mouse input to the parent, 2 wrap, 4 opaque background, 8 outline, 0x10000000 the colours
// equal the font's.
#pragma once
#include <cstdint>

#include "gz/DrawContext.h"
#include "gz/Win.h"
#include "gz/WinBtn.h"
#include "gz/WinFont.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinText = 0x212cdc1f;
constexpr std::uint32_t kTextPassThrough = 1, kTextWrap = 2, kTextOpaque = 4, kTextOutline = 8, kTextDefaultColors = 0x10000000;

class cIGZWinText : public cIGZUnknown {
public:
    enum tFontColorIdx : std::uint32_t { kColorText = 0, kColorShadow = 1 };
    virtual cIGZWin* AsIGZWin() = 0;                                                    // [3]
    virtual void SetCaption(const cIGZString& caption) = 0;                             // [4]
    virtual bool FitWindowToText(bool a, bool b) = 0;                                   // [5]
    virtual bool GetWinTextFlag(int flag) = 0;                                          // [6]
    virtual bool SetWinTextFlag(int flag, bool on) = 0;                                 // [7]
    virtual void SetTextColor(std::uint32_t native) = 0;                                // [8]
    virtual void GetTextColor(std::uint32_t& native) = 0;                               // [9]
    virtual void ClearTextColor() = 0;                                                  // [10]
    virtual bool SetTextColor(tFontColorIdx index, std::uint32_t native) = 0;           // [11]
    virtual bool GetTextColor(tFontColorIdx index, std::uint32_t& native) const = 0;    // [12]
    virtual bool SetTextColor(tFontColorIdx index, cRZColor color) = 0;                 // [13]
    virtual bool GetTextColor(tFontColorIdx index, cRZColor& color) const = 0;          // [14]
    virtual bool ClearTextColor(tFontColorIdx index) = 0;                               // [15]
    virtual void SetBackgroundOpaque(bool opaque) = 0;                                  // [16]
    virtual bool IsBackgroundOpaque() = 0;                                              // [17]
    virtual bool SetFont(IWinFont* font) = 0;                                           // [18]
    virtual bool SetFontStyle(std::uint32_t styleId) = 0;                               // [19]
    virtual bool GetFontStyle(std::uint32_t& styleId) = 0;                              // [20]
    virtual bool SetAlignment(std::uint32_t alignment) = 0;                             // [21]
    virtual bool GetAlignment(std::uint32_t& alignment) const = 0;                      // [22]
    virtual bool SetGutters(std::int8_t x, std::int8_t y) = 0;                          // [23]
    virtual bool GetGutters(std::int8_t& x, std::int8_t& y) = 0;                        // [24]
    virtual bool SetTextOffsets(std::int8_t x, std::int8_t y) = 0;                      // [25]
    virtual bool GetTextOffsets(std::int8_t& x, std::int8_t& y) = 0;                    // [26]
    virtual void SetOutlineUse(bool on, std::uint32_t color) = 0;                       // [27]
    virtual void GetOutlineUse(bool& on, std::uint32_t& color) const = 0;               // [28]
};

class cGZWinText : public cGZWin, public cIGZWinText {
public:
    explicit cGZWinText(cIGZWinMgr* mgr);
    ~cGZWinText() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    bool DoMessage(cGZMessage& message) override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    void SetCaption(const cIGZString& caption) override;
    bool GZPaint() override;
    void SetFillColor(std::uint32_t nativeColor) override;
    void SetFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { cGZWin::SetFillColor(r, g, b); }
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;

    cIGZWin* AsIGZWin() override { return this; }
    bool FitWindowToText(bool a, bool b) override;
    bool GetWinTextFlag(int flag) override { return (textFlags_ & flag) != 0; }
    bool SetWinTextFlag(int flag, bool on) override;
    void SetTextColor(std::uint32_t native) override { SetTextColor(kColorText, native); }
    void GetTextColor(std::uint32_t& native) override { GetTextColor(kColorText, native); }
    void ClearTextColor() override { ClearTextColor(kColorText); }
    bool SetTextColor(tFontColorIdx index, std::uint32_t native) override;
    bool GetTextColor(tFontColorIdx index, std::uint32_t& native) const override;
    bool SetTextColor(tFontColorIdx index, cRZColor color) override;
    bool GetTextColor(tFontColorIdx index, cRZColor& color) const override;
    bool ClearTextColor(tFontColorIdx index) override;
    void SetBackgroundOpaque(bool opaque) override { SetWinTextFlag(kTextOpaque, opaque); }
    bool IsBackgroundOpaque() override { return GetWinTextFlag(kTextOpaque); }
    bool SetFont(IWinFont* font) override;
    bool SetFontStyle(std::uint32_t styleId) override;
    bool GetFontStyle(std::uint32_t& styleId) override;
    bool SetAlignment(std::uint32_t alignment) override;
    bool GetAlignment(std::uint32_t& alignment) const override { alignment = alignment_; return true; }
    bool SetGutters(std::int8_t x, std::int8_t y) override;
    bool GetGutters(std::int8_t& x, std::int8_t& y) override { x = gutterX_; y = gutterY_; return true; }
    bool SetTextOffsets(std::int8_t x, std::int8_t y) override { offsetX_ = x; offsetY_ = y; return true; }
    bool GetTextOffsets(std::int8_t& x, std::int8_t& y) override { x = offsetX_; y = offsetY_; return true; }
    void SetOutlineUse(bool on, std::uint32_t color) override;
    void GetOutlineUse(bool& on, std::uint32_t& color) const override { on = (textFlags_ & kTextOutline) != 0; color = outlineColor_; }

    const cRZRect& TextRect() const { return textRect_; }
    void CalcTextPos();
    void GetMinTextArea(cRZRect& out, bool a, bool b);

protected:
    // The members are protected for the derived cSC4WinText (sc4/WinText.h): its GZPaint is a full rewrite of this one
    // and reaches the same state (the original has them private and the two classes live in the same translation unit).
    cIGZDrawContext* Context() const { return static_cast<cIGZDrawContext*>(drawContext_); }
    IWinFont* font_ = nullptr;                  // +0x158
    std::uint32_t fontStyle_ = 0;               // +0x160
    std::int8_t gutterX_ = 2, gutterY_ = 2;     // +0x164
    std::int8_t offsetX_ = 0, offsetY_ = 0;     // +0x166
    cRZRect textRect_{0, 0, 8, 8};              // +0x168
    std::uint32_t textFlags_ = 6;               // +0x178
    std::uint32_t outlineColor_ = 0;            // +0x17c
    std::uint32_t alignment_ = 0;               // +0x184
    std::uint32_t colors_[2] = {kWinUnsetTextColor, kWinUnsetTextColor};   // +0x188
    std::uint32_t horizontalAlign_ = 0;         // +0x190
};

}  // namespace gz
