// cGZWinLineInput — a single-line text edit control (0x100275ea0..0x10027852a, 100 functions including thunks).
// Object layout (0x1f0 bytes): the 0x150-byte cGZWin, +0x150 the cIGZWinLineInput view (iid 0x21335c59), +0x158
// code page, +0x15c cursor, +0x160 / +0x164 selection anchor and end (equal when nothing is selected), +0x168 first
// visible byte, +0x170 text (a cRZString), +0x198 "text change notification in progress", +0x1a0 font, +0x1a8
// line height, +0x1ac..+0x1b4 font colours (text, 1, selected; 0xdeadbeef = the font's own), +0x1b8 outline type
// (0 none, 1 flat, 2 raised, 3 double), +0x1bc..+0x1c8 outline colours (packed RGB), +0x1cc highlight fill colour
// (packed), +0x1d0 highlight alpha, +0x1d4 filled, +0x1d5 editable, +0x1d8 character limit, +0x1dc..+0x1df gutters
// (left, top, right, bottom), +0x1e0 three sound ids.
// Notifications (command messages sent to the notification target): 0xa871112f text changed, 0xa8711130 return.
// Quirks kept: Backspace passes cursor - charLength as DeleteChar's 1-based index (off by one, so it erases the
// character before the previous one and nothing at all at cursor 1); the highlight is only drawn when its alpha is exactly 1; the flat outline converts its already packed
// colour once more; a non-editable line inside a parent that answers iid 0x412ce496 forwards mouse-down to its
// notification target. Deliberate deviation: the original hands GetPreviousCharLength the START of the text (it
// inspects the byte before the buffer); this port passes the end of the preceding run, which is what was meant.
#pragma once
#include <cstdint>

#include "gz/Font.h"
#include "gz/Win.h"
#include "rz/cRZString.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinLineInput = 0x21335c59;
constexpr std::uint32_t kLineInputTextChanged = 0xa871112f, kLineInputReturn = 0xa8711130;
constexpr std::uint32_t kLineInputDefaultFont = 0x68963c4c;
// window colour manager ids the control asks for
constexpr std::uint32_t kLineInputColorFill = 0xa8963fb2, kLineInputColorSelectedBack = 0x88963fb4,
                        kLineInputColorSecond = 0x08963fb9, kLineInputColorSelected = 0x28963fbc;

class cIGZWinLineInput : public cIGZUnknown {
public:
    enum tFontColorIdx : std::uint32_t { kColorText = 0, kColorSecond = 1, kColorSelected = 2 };
    enum tOutlineType : std::uint32_t { kOutlineNone = 0, kOutlineFlat = 1, kOutlineRaised = 2, kOutlineDouble = 3 };
    enum tOutlineColorIdx : std::uint32_t { kOutlineColor0 = 0, kOutlineColor1, kOutlineColor2, kOutlineColor3 };

    virtual cIGZWin* AsIGZWin() = 0;                                                         // [3]
    virtual bool SetFont(cIGZFont* font) = 0;                                                // [4]
    virtual bool SetFontColor(std::uint32_t native) = 0;                                     // [5]
    virtual bool SetFontColor(tFontColorIdx index, std::uint32_t native) = 0;                // [6]
    virtual bool GetFontColor(tFontColorIdx index, std::uint32_t& native) const = 0;         // [7]
    virtual bool ClearFontColor(tFontColorIdx index) = 0;                                    // [8]
    virtual bool SetFilled(bool filled) = 0;                                                 // [9]
    virtual bool IsFilled() const = 0;                                                       // [10]
    virtual bool SetOutlineType(tOutlineType type) = 0;                                      // [11]
    virtual bool GetOutlineType(tOutlineType& type) const = 0;                               // [12]
    virtual bool SetOutlineColor(tOutlineColorIdx index, std::uint32_t native) = 0;          // [13]
    virtual bool GetOutlineColor(tOutlineColorIdx index, std::uint32_t& native) const = 0;   // [14]
    virtual bool SetOutline(bool on) = 0;                                                    // [15]
    virtual bool SetOutlineColor(std::uint32_t native) = 0;                                  // [16]
    virtual bool SetHighlightFillColor(std::uint32_t native) = 0;                            // [17]
    virtual bool GetHighlightFillColor(std::uint32_t& native) const = 0;                     // [18]
    virtual bool SetHighlightFillAlpha(float alpha) = 0;                                     // [19]
    virtual bool GetHighlightFillAlpha(float& alpha) const = 0;                              // [20]
    virtual std::uint32_t GetText(cIGZString& out) const = 0;                                // [21] returns the length
    virtual bool SetText(const cIGZString& text) = 0;                                        // [22]
    virtual bool SetText(const cIGZString* text) = 0;                                        // [23]
    virtual bool SetText(const char* text) = 0;                                              // [24]
    virtual bool SetCursorLoc(std::uint32_t location) = 0;                                   // [25]
    virtual bool GetCursorLoc(std::uint32_t& location) const = 0;                            // [26]
    virtual bool IsEditable() const = 0;                                                     // [27]
    virtual bool SetEditable(bool editable) = 0;                                             // [28]
    virtual bool SetCharacterLimit(std::uint32_t limit) = 0;                                 // [29]
    virtual bool GetCharacterLimit(std::uint32_t& limit) const = 0;                          // [30]
    virtual bool SetSelection(std::uint32_t start, std::uint32_t count) = 0;                 // [31]
    virtual bool GetSelection(std::uint32_t& start, std::uint32_t& count) const = 0;         // [32]
    virtual bool SetGutters(std::int8_t left, std::int8_t top, std::int8_t right, std::int8_t bottom) = 0;   // [33]
    virtual bool GetGutters(std::int8_t& left, std::int8_t& top, std::int8_t& right, std::int8_t& bottom) const = 0;   // [34]
    virtual bool GetMinMaxInfo(cRZRect& info) = 0;                                           // [35] (min w, min h, max w, max h)
    virtual bool SetSoundID(int which, int soundId) = 0;                                     // [36]
    virtual bool PlaySoundA(int which) = 0;                                                  // [37]
};

class cGZWinLineInput : public cGZWin, public cIGZWinLineInput {
public:
    explicit cGZWinLineInput(cIGZWinMgr* mgr, bool editable = true);
    ~cGZWinLineInput() override;
    static cIGZWinLineInput* CreateNewObject(cIGZWinMgr* mgr);

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    // cGZWin overrides
    bool Init() override;
    bool Shutdown() override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override;
    bool GZPaint() override;
    bool GZOnCharacter(char c) override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnSetFocus(cIGZWin* previous) override;
    bool GZOnKillFocus(cIGZWin* next) override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;

    // cIGZWinLineInput
    cIGZWin* AsIGZWin() override { return this; }
    bool SetFont(cIGZFont* font) override;
    bool SetFontColor(std::uint32_t native) override { return SetFontColor(kColorText, native); }
    bool SetFontColor(tFontColorIdx index, std::uint32_t native) override;
    bool GetFontColor(tFontColorIdx index, std::uint32_t& native) const override;
    bool ClearFontColor(tFontColorIdx index) override { return SetFontColor(index, kWinUnsetColor); }
    bool SetFilled(bool filled) override;
    bool IsFilled() const override { return filled_; }
    bool SetOutlineType(tOutlineType type) override { outlineType_ = type; return true; }
    bool GetOutlineType(tOutlineType& type) const override { type = outlineType_; return true; }
    bool SetOutlineColor(tOutlineColorIdx index, std::uint32_t native) override;
    bool GetOutlineColor(tOutlineColorIdx index, std::uint32_t& native) const override;
    bool SetOutline(bool on) override { return SetOutlineType(static_cast<tOutlineType>(on)); }
    bool SetOutlineColor(std::uint32_t native) override { return SetOutlineColor(kOutlineColor0, native); }
    bool SetHighlightFillColor(std::uint32_t native) override;
    bool GetHighlightFillColor(std::uint32_t& native) const override;
    bool SetHighlightFillAlpha(float alpha) override;
    bool GetHighlightFillAlpha(float& alpha) const override { alpha = highlightAlpha_; return true; }
    std::uint32_t GetText(cIGZString& out) const override;
    bool SetText(const cIGZString& text) override { return SetText(&text); }
    bool SetText(const cIGZString* text) override;
    bool SetText(const char* text) override;
    bool SetCursorLoc(std::uint32_t location) override;
    bool GetCursorLoc(std::uint32_t& location) const override { location = cursor_; return true; }
    bool IsEditable() const override { return editable_; }
    bool SetEditable(bool editable) override;
    bool SetCharacterLimit(std::uint32_t limit) override { characterLimit_ = limit; return true; }
    bool GetCharacterLimit(std::uint32_t& limit) const override { limit = characterLimit_; return true; }
    bool SetSelection(std::uint32_t start, std::uint32_t count) override;
    bool GetSelection(std::uint32_t& start, std::uint32_t& count) const override;
    bool SetGutters(std::int8_t left, std::int8_t top, std::int8_t right, std::int8_t bottom) override;
    bool GetGutters(std::int8_t& left, std::int8_t& top, std::int8_t& right, std::int8_t& bottom) const override;
    bool GetMinMaxInfo(cRZRect& info) override;
    bool SetSoundID(int which, int soundId) override;
    bool PlaySoundA(int which) override;

    // internals (public for tests)
    std::uint32_t FindTextIndexFromCursorLocation(int x);
    bool UpdateViewLocation();
    bool InsertChar(char c, std::uint32_t position);
    bool DeleteChar(std::uint32_t oneBasedIndex, std::uint32_t count);
    void draw_text(int x, int y, std::uint32_t offset, std::uint32_t count, int colorIndex);
    void paint_outline();
    std::uint32_t ViewStart() const { return viewStart_; }
    const cRZString& Text() const { return text_; }

    static constexpr std::uint32_t kWinUnsetColor = 0xdeadbeef;

private:
    std::uint32_t Length() const { return text_.Strlen(); }
    void NotifyTextChanged();

    int codePage_ = 8;                     // +0x158
    std::uint32_t cursor_ = 0;             // +0x15c
    std::uint32_t selectionStart_ = 0;     // +0x160 (anchor)
    std::uint32_t selectionEnd_ = 0;       // +0x164
    std::uint32_t viewStart_ = 0;          // +0x168
    cRZString text_;                       // +0x170
    bool changing_ = false;                // +0x198
    cIGZFont* font_ = nullptr;             // +0x1a0
    int lineHeight_ = 1;                   // +0x1a8
    std::uint32_t fontColors_[3] = {0, 0, 0xffffffff};   // +0x1ac
    tOutlineType outlineType_ = kOutlineFlat;            // +0x1b8
    std::uint32_t outlineColors_[4] = {0, 0, 0, 0};      // +0x1bc
    std::uint32_t highlightColor_ = kWinUnsetColor;      // +0x1cc
    float highlightAlpha_ = 1.0f;          // +0x1d0
    bool filled_ = true;                   // +0x1d4
    bool editable_ = true;                 // +0x1d5
    std::uint32_t characterLimit_ = 0xffffffff;          // +0x1d8
    std::int8_t gutterLeft_ = 2, gutterTop_ = 0, gutterRight_ = 2, gutterBottom_ = 0;   // +0x1dc
    int soundIds_[3] = {0, 0, 0};          // +0x1e0
};

}  // namespace gz
