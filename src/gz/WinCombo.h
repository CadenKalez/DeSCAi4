// cGZWinCombo — a combo box (0x1002f3440..0x1002f5534, 63 functions, iid 0x412ce496 for the cIGZWinCombo view at +0x150):
// a text edit child (id 0x53430d98) plus a drop-down arrow button on the right. Clicking the arrow (or the whole box when it
// is not editable, or Alt+Down) pops up a list box (id 0x53430d99) beneath it showing the strings; the chosen row becomes the
// selection and the text. Notifications: command 3 with p1 0xe8711255 and p2 = window id.
//
// Flags (0x158): bit 0 = the pop-up is as wide as the widest string (+8 pixels, but not wider than the main window - 16).
// Arrow image: a strip of four frames, width/4 each (0 disabled, 1 enabled, 2 pressed); without an image a raised rectangle
// 16x8 is drawn instead.
// Deviation: the pop-up list box takes its font from the font source by style id (the combo keeps a cIGZFont for the text
// edit and a cIGZFont is not an IWinFont), so the drop-down is marked partial in the ledger.
#pragma once
#include <cstdint>
#include <list>

#include "gz/Font.h"
#include "gz/Win.h"
#include "gz/WinTextEdit.h"
#include "rz/cRZString.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinCombo = 0x412ce496;
constexpr std::uint32_t kComboTextEditId = 0x53430d98, kComboListBoxId = 0x53430d99, kComboClassId = 0x231a1c58;
constexpr std::uint32_t kComboChanged = 0xe8711255;
constexpr std::uint32_t kComboUnsetColor = 0xdeadbeef;

class cIGZWinCombo : public cIGZUnknown {
public:
    enum tFontColorIdx : std::uint32_t { kColorText = 0, kColorDisabled = 1, kColorSelected = 2 };
    virtual cIGZWin* AsIGZWin() = 0;                                                         // [3]
    virtual cIGZWinTextEdit* GetLineInput() = 0;                                             // [4]
    virtual cIGZWinTextEdit* GetTextEdit() = 0;                                              // [5]
    virtual void SetFlag(std::uint32_t flag) = 0;                                            // [6]
    virtual void SetFlags(std::uint32_t flags) = 0;                                          // [7]
    virtual void ClearFlag(std::uint32_t flag) = 0;                                          // [8]
    virtual bool IsFlagSet(std::uint32_t flag) = 0;                                          // [9]
    virtual std::uint32_t GetFlags() = 0;                                                    // [10]
    virtual void SetFont(cIGZFont* font) = 0;                                                // [11]
    virtual bool SetFontColor(tFontColorIdx index, std::uint32_t native) = 0;                // [12]
    virtual std::uint32_t GetFontColor(tFontColorIdx index) const = 0;                       // [13]
    virtual bool GetFontColor(tFontColorIdx index, std::uint32_t& native) const = 0;         // [14]
    virtual bool ClearFontColor(tFontColorIdx index) = 0;                                    // [15]
    virtual bool SetHighlightColor(std::uint32_t native) = 0;                                // [16]
    virtual bool GetHighlightColor(std::uint32_t& native) const = 0;                         // [17]
    virtual bool SetHighlightFillAlpha(float alpha) = 0;                                     // [18]
    virtual bool GetHighlightFillAlpha(float& alpha) const = 0;                              // [19]
    virtual bool GetEditable() const = 0;                                                    // [20]
    virtual bool SetEditable(bool editable) = 0;                                             // [21]
    virtual bool SetDropDownOutlineColor(std::uint32_t native) = 0;                          // [22]
    virtual bool GetDropDownOutlineColor(std::uint32_t& native) const = 0;                   // [23]
    virtual bool RemoveString(const cIGZString& text) = 0;                                   // [24]
    virtual bool RemoveString(int index) = 0;                                                // [25]
    virtual void RemoveAllStrings() = 0;                                                     // [26]
    virtual bool InsertString(const cIGZString& text, int index) = 0;                        // [27]
    virtual bool SetSelection(int index, bool notify) = 0;                                   // [28]
    virtual int GetSelection() const = 0;                                                    // [29]
    virtual bool GetSelection(int& index, cIGZString& text) const = 0;                       // [30]
    virtual bool GetStringFromIndex(int index, cIGZString& out) const = 0;                   // [31]
    virtual bool GetIndexFromString(int& index, const cIGZString& text) const = 0;           // [32]
    virtual bool SetComboDownArrow(cIGZBuffer* image, const cRZRect* area) = 0;              // [33]
    virtual bool GetComboDownArrow(cIGZBuffer*& image, cRZRect& area) const = 0;             // [34]
    virtual bool SetComboDownColor(std::uint32_t native) = 0;                                // [35]
    virtual bool GetComboDownColor(std::uint32_t& native) const = 0;                         // [36]
    virtual bool SetBtnGutter(std::int8_t gutter) = 0;                                       // [37]
    virtual bool GetBtnGutter(std::int8_t& gutter) const = 0;                                // [38]
    virtual bool SetTextGutters(std::int8_t x, std::int8_t y) = 0;                           // [39]
    virtual bool GetTextGutters(std::int8_t& x, std::int8_t& y) const = 0;                   // [40]
    virtual bool GetMinMaxInfo(cRZRect& info) = 0;                                           // [41]
};

class cGZWinCombo : public cGZWin, public cIGZWinCombo {
public:
    explicit cGZWinCombo(cIGZWinMgr* mgr, bool editable = true);   // @ 0x1002f3440
    ~cGZWinCombo() override;
    static cGZWinCombo* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinCombo(mgr, true); }

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1002f3670
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool DoMessage(cGZMessage& message) override;                  // @ 0x1002f44fe
    bool Init() override;                                          // @ 0x1002f37a4
    bool Shutdown() override;                                      // @ 0x1002f399a
    void SetArea(int l, int t, int r, int b) override;             // @ 0x1002f3afc
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override;   // @ 0x1002f3bbc
    void SetFillColor(std::uint32_t nativeColor) override;         // @ 0x1002f3c76
    using cGZWin::SetFillColor;
    bool GZPaint() override;                                       // @ 0x1002f417a
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x1002f4470
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;     // @ 0x1002f44d8
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;     // @ 0x1002f445e

    cIGZWin* AsIGZWin() override { return this; }
    cIGZWinTextEdit* GetLineInput() override { return GetTextEdit(); }
    cIGZWinTextEdit* GetTextEdit() override;
    void SetFlag(std::uint32_t flag) override { if (!(comboFlags_ & flag)) SetFlags(comboFlags_ | flag); }
    void SetFlags(std::uint32_t flags) override;
    void ClearFlag(std::uint32_t flag) override { if (comboFlags_ & flag) SetFlags(comboFlags_ & ~flag); }
    bool IsFlagSet(std::uint32_t flag) override { return (comboFlags_ & flag) != 0; }
    std::uint32_t GetFlags() override { return comboFlags_; }
    void SetFont(cIGZFont* font) override;
    bool SetFontColor(tFontColorIdx index, std::uint32_t native) override;
    std::uint32_t GetFontColor(tFontColorIdx index) const override { return fontColors_[index]; }
    bool GetFontColor(tFontColorIdx index, std::uint32_t& native) const override;
    bool ClearFontColor(tFontColorIdx index) override { return SetFontColor(index, kComboUnsetColor); }
    bool SetHighlightColor(std::uint32_t native) override;
    bool GetHighlightColor(std::uint32_t& native) const override;
    bool SetHighlightFillAlpha(float alpha) override;
    bool GetHighlightFillAlpha(float& alpha) const override { alpha = highlightAlpha_; return true; }
    bool GetEditable() const override { return editable_; }
    bool SetEditable(bool editable) override;
    bool SetDropDownOutlineColor(std::uint32_t native) override { outlineColor_ = native; return true; }
    bool GetDropDownOutlineColor(std::uint32_t& native) const override { native = outlineColor_; return true; }
    bool RemoveString(const cIGZString& text) override;
    bool RemoveString(int index) override;
    void RemoveAllStrings() override;
    bool InsertString(const cIGZString& text, int index) override;
    bool SetSelection(int index, bool notify) override;
    int GetSelection() const override { return selection_; }
    bool GetSelection(int& index, cIGZString& text) const override;
    bool GetStringFromIndex(int index, cIGZString& out) const override;
    bool GetIndexFromString(int& index, const cIGZString& text) const override;
    bool SetComboDownArrow(cIGZBuffer* image, const cRZRect* area) override;
    bool GetComboDownArrow(cIGZBuffer*& image, cRZRect& area) const override;
    bool SetComboDownColor(std::uint32_t native) override;
    bool GetComboDownColor(std::uint32_t& native) const override { native = arrowColor_; return true; }
    bool SetBtnGutter(std::int8_t gutter) override;
    bool GetBtnGutter(std::int8_t& gutter) const override { gutter = buttonGutter_; return true; }
    bool SetTextGutters(std::int8_t x, std::int8_t y) override;
    bool GetTextGutters(std::int8_t& x, std::int8_t& y) const override { x = textGutterX_; y = textGutterY_; return true; }
    bool GetMinMaxInfo(cRZRect& info) override;

    // internals (public for tests)
    cIGZWinTextEdit* get_text_edit();                       // @ 0x1002f4e74 the child, or null
    bool create_text_edit();                                // @ 0x1002f4eb6
    bool layout(int width, int height);                     // @ 0x1002f5080
    bool do_drop_down();                                    // @ 0x1002f5114
    bool send_combo_changed_msg();                          // @ 0x1002f5534
    bool get_string_address_from_index(int index, const cRZString*& out) const;   // @ 0x1002f4d84
    int StringCount() const { return count_; }
    bool Dropped() const { return dropped_; }
    int ArrowWidth() const { return arrowWidth_; }

private:
    std::uint32_t comboFlags_ = 0;                // +0x158
    std::list<cRZString> strings_;                // +0x160
    int selection_ = -1;                          // +0x178
    bool dropped_ = false;                        // +0x17c
    int count_ = 0;                               // +0x180
    cIGZFont* font_ = nullptr;                    // +0x188
    std::uint32_t fontColors_[3] = {kComboUnsetColor, kComboUnsetColor, kComboUnsetColor};   // +0x190
    std::uint32_t highlightColor_ = kComboUnsetColor;   // +0x19c
    float highlightAlpha_ = 1.0f;                 // +0x1a0
    std::uint32_t outlineColor_ = 0;              // +0x1a4
    bool editable_ = true;                        // +0x1a8
    cIGZBuffer* arrowImage_ = nullptr;            // +0x1b0
    int arrowOriginX_ = 0, arrowOriginY_ = 0;     // +0x1b8 / +0x1bc
    int arrowWidth_ = 16, arrowHeight_ = 8;       // +0x1c0 / +0x1c4
    int arrowX_ = 0, arrowY_ = 0;                 // +0x1c8 / +0x1cc
    int arrowFrameX_ = 0;                         // +0x1d0 source x of the frame in use
    std::uint32_t arrowColor_ = kComboUnsetColor; // +0x1d4
    std::int8_t buttonGutter_ = 1, textGutterX_ = 3, textGutterY_ = 0;   // +0x1d8..+0x1da
    bool widthDirty_ = false;                     // +0x1db
    int widestString_ = 0;                        // +0x1dc
};

}  // namespace gz
