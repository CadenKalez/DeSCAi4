// cGZWinListBox — a scrolling list of text lines (0x100270950..0x1002748c6, iid 0x4132242b for the list view at
// +0x150). In its drop-down form (constructed with drop = true) it is the popup of a combo box: it captures the
// mouse, ends with a dialog result instead of a selection message, and sizes itself to its contents.
//
// Lines are cListEntry {text, id}. Selection is an index (-1 none). Line texts may start with "#rrggbb" (a custom
// colour for the line, enabled lists only) or "##" (a literal '#'). Colours are native: 0 text, 1 disabled, 2
// selected. The vertical scrollbar (child 0x4377ea2c, horizontal 0x4377ea2d) is created on demand once there are
// more lines than fit.
//
// Layout (0x210 bytes): cGZWin (0x150), +0x150 cIGZWinListBox view, +0x158 line list (+0x168 size), +0x170 highlight
// fill alpha (1.0), +0x174 highlight colour (packed), +0x178 font, +0x180 colours[3], +0x18c selection (-1), +0x190
// visible lines, +0x194 index of the top line, +0x198 line height, +0x19c minimum line height, +0x1a0 sorted,
// +0x1a1 outline, +0x1a2 sunken rectangle, +0x1a3 drop, +0x1a4 fit height, +0x1a5 fit width, +0x1a8 popup
// background, +0x1b0 line background, +0x1b8 double click pending, +0x1bc double click index, +0x1c0 / +0x1c4
// outline colours (packed), +0x1c8 / +0x1c9 scrollbar gutters (3, 0), +0x1ca / +0x1cb text gutters (3, 1), +0x1cc /
// +0x1cd scrollbars enabled (1, 0), +0x1ce highlight drawn (1), +0x1d0 / +0x1d8 scrollbar images, +0x1e0 image
// rectangle, +0x200 click timer, +0x208 post messages, +0x209 mouse forwarded to the scrollbar, +0x20c line count.
#pragma once
#include <cstdint>
#include <list>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/Win.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinFont.h"
#include "gz/WinScrollbar.h"
#include "gz/WinSpinner.h"
#include "rz/cRZString.h"
#include "rz/cRZTimer.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinListBox = 0x4132242b;
constexpr std::uint32_t kGZCLSID_cGZWinListBox = 0x598;
constexpr std::uint32_t kListBoxVScrollId = 0x4377ea2c, kListBoxHScrollId = 0x4377ea2d;
constexpr std::uint32_t kGZMsgListBoxSelectionChanged = 0x08710e0f;   // command parameter 1; p2 window id, p3 selected id
constexpr std::uint32_t kGZMsgListBoxDoubleClick = 0x08710e10;
constexpr int kListBoxEnd = -2;                                       // "append" / "last" in the index arguments

class cIGZWinListBox : public cIGZUnknown {
public:
    enum tFontColorIdx : std::uint32_t { kColorText = 0, kColorDisabled = 1, kColorSelected = 2 };
    virtual cIGZWin* AsIGZWin() = 0;                                                         // [3]
    virtual bool RemoveString(const cIGZString& text) = 0;                                   // [4]
    virtual bool RemoveString(int index) = 0;                                                // [5]
    virtual void RemoveAllStrings() = 0;                                                     // [6]
    virtual bool InsertString(const cIGZString& text, int index, std::uint32_t id) = 0;      // [7]
    virtual bool ModifyString(const cIGZString& text, int index) = 0;                        // [8]
    virtual bool ResizeWindowForExactLineHeights() = 0;                                      // [9]
    virtual bool SetSelection(int index, bool notify) = 0;                                   // [10]
    virtual bool SetSelectionByID(std::uint32_t id, bool notify) = 0;                        // [11]
    virtual int GetSelection() const = 0;                                                    // [12]
    virtual std::uint32_t GetSelectionID() const = 0;                                        // [13]
    virtual void SetShouldSort() = 0;                                                        // [14]
    virtual bool GetShouldSort() const = 0;                                                  // [15]
    virtual void Sort() = 0;                                                                 // [16]
    virtual void SetDrop(bool drop) = 0;                                                     // [17]
    virtual bool GetDrop() const = 0;                                                        // [18]
    virtual void GetTextArea(cRZRect& out) const = 0;                                        // [19]
    virtual bool MoveLineUp() = 0;                                                           // [20]
    virtual bool MoveLineDown() = 0;                                                         // [21]
    virtual bool ScrollToStart() = 0;                                                        // [22]
    virtual bool ScrollToEnd() = 0;                                                          // [23]
    virtual bool ScrollPageUp() = 0;                                                         // [24]
    virtual bool ScrollPageDown() = 0;                                                       // [25]
    virtual bool IsIndexInView(int index) const = 0;                                         // [26]
    virtual void PutIndexInView(int index) = 0;                                              // [27]
    virtual bool ScrollToFirstLetter(char letter) = 0;                                       // [28]
    virtual void SetIndexOfTopLine(int index) = 0;                                           // [29]
    virtual int GetLineCount() const = 0;                                                    // [30]
    virtual int GetLineHeight() const = 0;                                                   // [31]
    virtual int GetVisibleLines() const = 0;                                                 // [32]
    virtual int GetIndexOfTopLine() const = 0;                                               // [33]
    virtual int GetItemIndexFromCursorPosition(int x, int y, bool clamp) const = 0;          // [34]
    virtual bool EnableScrollBar(bool vertical, bool horizontal) = 0;                        // [35]
    virtual bool AreScrollBarsEnabled(bool& vertical, bool& horizontal) const = 0;           // [36]
    virtual void SetLineItemBackgroundBuffer(cIGZBuffer* image) = 0;                         // [37]
    virtual void SetFont(IWinFont* font) = 0;                                                // [38]
    virtual bool SetFontColor(tFontColorIdx index, std::uint32_t native) = 0;                // [39]
    virtual bool GetFontColor(tFontColorIdx index, std::uint32_t& native) = 0;               // [40]
    virtual void ClearFontColor(tFontColorIdx index) = 0;                                    // [41]
    virtual void SetHighlightColor(std::uint32_t native) = 0;                                // [42]
    virtual bool GetHighlightColor(std::uint32_t& native) const = 0;                         // [43]
    virtual void SetHighlightDrawFlag(bool on) = 0;                                          // [44]
    virtual bool GetHighlightDrawFlag() const = 0;                                           // [45]
    virtual void SetHighlightFillAlpha(float alpha) = 0;                                     // [46]
    virtual bool ResizeBoxToFitContents() = 0;                                               // [47]
    virtual bool ResizeBoxToFitContentsHorizontal() = 0;                                     // [48]
    virtual bool GetStringFromIndex(int index, cIGZString& out) const = 0;                   // [49]
    virtual bool GetIndexFromString(int& index, const cIGZString& text) const = 0;           // [50]
    virtual void SetOutlineDrawing(bool on) = 0;                                             // [51]
    virtual bool GetOutlineDrawing() const = 0;                                              // [52]
    virtual void SetDrawSunkenRect(bool on) = 0;                                             // [53]
    virtual bool GetDrawSunkenRect() const = 0;                                              // [54]
    virtual void SetOutlineColors(std::uint32_t first, std::uint32_t second) = 0;            // [55]
    virtual bool GetOutlineColors(std::uint32_t& first, std::uint32_t& second) const = 0;    // [56]
    virtual bool SetScrollbarGutters(std::int8_t x, std::int8_t y) = 0;                      // [57]
    virtual bool GetScrollbarGutters(std::int8_t& x, std::int8_t& y) const = 0;              // [58]
    virtual bool SetTextGutters(std::int8_t x, std::int8_t y) = 0;                           // [59]
    virtual bool GetTextGutters(std::int8_t& x, std::int8_t& y) const = 0;                   // [60]
    virtual bool SetMinimumLineHeight(std::uint8_t height) = 0;                              // [61]
    virtual bool SetScrollBarImageVertical(cIGZBuffer* image, const cRZRect& area) = 0;      // [62]
    virtual bool SetScrollBarImageHorizontal(cIGZBuffer* image, const cRZRect& area) = 0;    // [63]
    virtual bool SetUsePostMsg(bool on) = 0;                                                 // [64]
    virtual bool GetUsePostMsg() const = 0;                                                  // [65]
};

class cGZWinListBox : public cGZWin, public cIGZWinListBox {
public:
    struct cListEntry {
        cRZString text;
        std::uint32_t id = 0;
    };

    explicit cGZWinListBox(cIGZWinMgr* mgr, bool drop = false);
    ~cGZWinListBox() override;
    static cGZWinListBox* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinListBox(mgr, false); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override;
    bool GZPaint() override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;
    virtual bool GZCharacter(std::uint32_t character);            // @ 0x100272d90

    cIGZWin* AsIGZWin() override { return this; }
    bool RemoveString(const cIGZString& text) override;
    bool RemoveString(int index) override;
    void RemoveAllStrings() override;
    bool InsertString(const cIGZString& text, int index, std::uint32_t id) override;
    bool ModifyString(const cIGZString& text, int index) override;
    bool ResizeWindowForExactLineHeights() override;
    bool SetSelection(int index, bool notify) override;
    bool SetSelectionByID(std::uint32_t id, bool notify) override;
    int GetSelection() const override { return selection_; }
    std::uint32_t GetSelectionID() const override;
    void SetShouldSort() override;
    bool GetShouldSort() const override { return sorted_; }
    void Sort() override;
    void SetDrop(bool drop) override { drop_ = drop; }
    bool GetDrop() const override { return drop_; }
    void GetTextArea(cRZRect& out) const override;
    bool MoveLineUp() override;
    bool MoveLineDown() override;
    bool ScrollToStart() override;
    bool ScrollToEnd() override;
    bool ScrollPageUp() override;
    bool ScrollPageDown() override;
    bool IsIndexInView(int index) const override;
    void PutIndexInView(int index) override;
    bool ScrollToFirstLetter(char letter) override;
    void SetIndexOfTopLine(int index) override;
    int GetLineCount() const override { return static_cast<int>(count_); }
    int GetLineHeight() const override { return lineHeight_; }
    int GetVisibleLines() const override { return visibleLines_; }
    int GetIndexOfTopLine() const override { return topIndex_; }
    int GetItemIndexFromCursorPosition(int x, int y, bool clamp) const override;
    bool EnableScrollBar(bool vertical, bool horizontal) override { scrollV_ = vertical; scrollH_ = horizontal; return true; }
    bool AreScrollBarsEnabled(bool& vertical, bool& horizontal) const override { vertical = scrollV_; horizontal = scrollH_; return true; }
    void SetLineItemBackgroundBuffer(cIGZBuffer* image) override;
    virtual void SetPopupBackgroundBuffer(cIGZBuffer* image);       // @ 0x10027159c
    void SetFont(IWinFont* font) override;
    bool SetFontColor(tFontColorIdx index, std::uint32_t native) override;
    bool GetFontColor(tFontColorIdx index, std::uint32_t& native) override;
    void ClearFontColor(tFontColorIdx index) override { SetFontColor(index, kWinUnsetTextColor); }
    void SetHighlightColor(std::uint32_t native) override;
    bool GetHighlightColor(std::uint32_t& native) const override;
    void SetHighlightDrawFlag(bool on) override { highlightDrawn_ = on; }
    bool GetHighlightDrawFlag() const override { return highlightDrawn_; }
    void SetHighlightFillAlpha(float alpha) override;
    bool ResizeBoxToFitContents() override;
    bool ResizeBoxToFitContentsHorizontal() override;
    bool GetStringFromIndex(int index, cIGZString& out) const override;
    bool GetIndexFromString(int& index, const cIGZString& text) const override;
    void SetOutlineDrawing(bool on) override;
    bool GetOutlineDrawing() const override { return outline_; }
    void SetDrawSunkenRect(bool on) override;
    bool GetDrawSunkenRect() const override { return sunken_; }
    void SetOutlineColors(std::uint32_t first, std::uint32_t second) override;
    bool GetOutlineColors(std::uint32_t& first, std::uint32_t& second) const override;
    bool SetScrollbarGutters(std::int8_t x, std::int8_t y) override;
    bool GetScrollbarGutters(std::int8_t& x, std::int8_t& y) const override { x = scrollGutterX_; y = scrollGutterY_; return true; }
    bool SetTextGutters(std::int8_t x, std::int8_t y) override;
    bool GetTextGutters(std::int8_t& x, std::int8_t& y) const override { x = textGutterX_; y = textGutterY_; return true; }
    bool SetMinimumLineHeight(std::uint8_t height) override;
    bool SetScrollBarImageVertical(cIGZBuffer* image, const cRZRect& area) override;
    bool SetScrollBarImageHorizontal(cIGZBuffer*, const cRZRect&) override { return false; }
    bool SetUsePostMsg(bool on) override { postMessages_ = on; return true; }
    bool GetUsePostMsg() const override { return postMessages_; }

    // non-interface members
    virtual bool GetStringFromIndex(int index, cRZString& out) const;                 // @ 0x1002727f4
    virtual bool GetIndexFromString(int& index, const cRZString& text) const;         // @ 0x100272912
    virtual bool RemoveString(const cRZString& text);                                 // @ 0x100272172
    virtual void CalculateAllMetrics();                                               // @ 0x100270fc4
    virtual bool GetStringAddressFromIndex(int index, cRZString*& out);               // @ 0x10027271c
    virtual bool RemoveStringAtIndex(int index);                                      // @ 0x100272a0e
    virtual bool InsertEntryAtIndex(int index, const cListEntry& entry);              // @ 0x100272b4c
    virtual void SetScrollBarValue();                                                 // @ 0x1002744de
    virtual void SetScrollBarSizes();                                                 // @ 0x1002745aa
    virtual bool AddScrollBar(bool vertical, bool horizontal);                        // @ 0x10027432a
    virtual void RemoveScrollBar(bool vertical, bool horizontal);                     // @ 0x100274478
    virtual bool EmitSelectionChangedMsg();                                           // @ 0x1002747cc
    virtual bool EmitDoubleClickMsg();                                                // @ 0x100274836
    virtual bool EndDialog(std::uint32_t result);                                     // @ 0x100274772
    virtual void SendSelectionChangedMsg() { EmitSelectionChangedMsg(); }             // @ 0x1002748aa
    virtual void SendDoubleClickMsg() { EmitDoubleClickMsg(); }                       // @ 0x1002748b8

protected:
    cIGZDrawContext* Context() const { return static_cast<cIGZDrawContext*>(drawContext_); }
    bool PostOrSend(std::uint32_t command, std::uint32_t id);

    std::list<cListEntry> entries_;                    // +0x158
    float highlightAlpha_ = 1.0f;                      // +0x170
    std::uint32_t highlightColor_ = kWinUnsetTextColor;   // +0x174
    IWinFont* font_ = nullptr;                         // +0x178
    std::uint32_t colors_[3] = {kWinUnsetTextColor, kWinUnsetTextColor, kWinUnsetTextColor};   // +0x180
    int selection_ = -1;                               // +0x18c
    int visibleLines_ = 0;                             // +0x190
    int topIndex_ = 0;                                 // +0x194
    int lineHeight_ = 10;                              // +0x198
    int minLineHeight_ = 0;                            // +0x19c
    bool sorted_ = false, outline_ = false, sunken_ = false, drop_ = false, fitHeight_ = true, fitWidth_ = false;   // +0x1a0..0x1a5
    cIGZBuffer* popupBackground_ = nullptr;            // +0x1a8
    cIGZBuffer* lineBackground_ = nullptr;             // +0x1b0
    bool doubleClickPending_ = false;                  // +0x1b8
    int doubleClickIndex_ = -1;                        // +0x1bc
    std::uint32_t outlineColors_[2] = {kWinUnsetTextColor, kWinUnsetTextColor};   // +0x1c0
    std::int8_t scrollGutterX_ = 3, scrollGutterY_ = 0, textGutterX_ = 3, textGutterY_ = 1;   // +0x1c8
    bool scrollV_ = true, scrollH_ = false, highlightDrawn_ = true;   // +0x1cc
    cIGZBuffer* scrollImageV_ = nullptr;               // +0x1d0
    cIGZBuffer* scrollImageH_ = nullptr;               // +0x1d8
    cRZRect scrollImageArea_{0, 0, 0, 0};              // +0x1e0
    cRZTimer clickTimer_{cRZTimer::kMilliseconds};     // +0x200
    bool postMessages_ = false;                        // +0x208
    bool forwarding_ = false;                          // +0x209
    std::uint32_t count_ = 0;                          // +0x20c
};

}  // namespace gz
