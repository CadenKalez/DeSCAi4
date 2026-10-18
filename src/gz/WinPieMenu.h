// cGZWinPieMenu — a pop-up pie menu (0x1005165f0..0x100519400, 74 functions, iid 0x1aad4a4a for the view at +0x150). Items
// (a label or a button window each) are laid out around the click point: up to eight "spokes" (1, 2, 4 or 8 depending on
// the item count, starting at the first direction, clockwise from the top), further items stack above (even) or below
// (odd) the first ring. Moving the mouse past the inactive radius selects the item in that direction; releasing (or clicking
// again in click mode) commits it. An owner-draw object can measure and draw the title and items; without one labels are
// drawn in a framed box coloured lowlight / highlight / selected.
//
// Notifications (command messages, type 3, sent to the notification target; p2 = the window): 0x1aad67d4 mouse moved,
// d5 selection changed, d6 item chosen, d7 cancelled (selection -1), d8 character (p2 = char), d9 key down (p2 = key).
// Quirks kept: DrawLabelFrame draws two nested frames; Escape cancels through EndPopup(); the mouse-up test only commits a
// drag release when the pointer moved beyond the inactive radius.
#pragma once
#include <cstdint>
#include <vector>

#include "gz/Font.h"
#include "gz/Win.h"
#include "gz/WinBtn.h"
#include "rz/cRZString.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinPieMenu = 0x1aad4a4a;
constexpr std::uint32_t kPieMsgMouseMoved = 0x1aad67d4, kPieMsgSelectionChanged = 0x1aad67d5, kPieMsgChosen = 0x1aad67d6,
                        kPieMsgCancelled = 0x1aad67d7, kPieMsgCharacter = 0x1aad67d8, kPieMsgKeyDown = 0x1aad67d9;
constexpr std::uint32_t kPieDefaultFontStyle = 0x68963c4c, kPieDefaultBackground = 0xa8963ee8;

class cIGZWinPieMenu;

struct PieMenuItem {
    cRZString text;                 // +8
    cIGZWinBtn* button = nullptr;   // +0x28 (a reference is held)
    bool enabled = true;            // +0x30
    float angle = 0;                // +0x34
    int x = 0, y = 0, w = 0, h = 0; // +0x38..+0x44 position (menu coordinates) and size
};

// Lets a client measure and paint the title and the items itself; every Measure/Draw answers true when it handled the job.
class cIGZWinPieMenuOwnerDraw : public cIGZUnknown {
public:
    virtual bool MeasureItem(cIGZWinPieMenu* menu, int index, const PieMenuItem* item, cRZRect& out) = 0;            // 0x18
    virtual bool MeasureTitle(cIGZWinPieMenu* menu, const cRZString* title, cRZRect& out) = 0;                         // 0x20
    virtual bool DrawItem(cIGZWinPieMenu* menu, int index, const cRZString* title, int x, int y, int w, int h,
                          std::uint32_t color, std::uint32_t fill) = 0;                                                // 0x28
    virtual void GetItemTextOffset(cIGZWinPieMenu* menu, int index, const PieMenuItem* item, int& x, int& y) = 0;      // 0x30
    virtual void AfterDrawItem(cIGZWinPieMenu* menu, int index, const cRZString* title, int x, int y, int w, int h,
                               std::uint32_t color, std::uint32_t fill) = 0;                                           // 0x38
    virtual bool DrawTitle(cIGZWinPieMenu* menu, const cRZString* title, int x, int y, int w, int h, std::uint32_t color,
                           std::uint32_t fill) = 0;                                                                    // 0x40
    virtual void GetTitleTextOffset(cIGZWinPieMenu* menu, const PieMenuItem* item, int& x, int& y) = 0;                // 0x48
    virtual void AfterDrawTitle(cIGZWinPieMenu* menu, const cRZString* title, int x, int y, int w, int h, std::uint32_t color,
                                std::uint32_t fill) = 0;                                                               // 0x50
};

class cIGZWinPieMenu : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                   // [3]
    virtual float Pi() const = 0;                                                      // [4]
    virtual void Clear() = 0;                                                          // [5]
    virtual bool InsertString(const cIGZString& text, cIGZWinBtn* button) = 0;         // [6]
    virtual bool SetSelection(int index, bool commit) = 0;                             // [7]
    virtual int GetSelection() const = 0;                                              // [8]
    virtual bool Layout() = 0;                                                         // [9]
    virtual bool ReLayout() = 0;                                                       // [10]
    virtual int GetItemCount() const = 0;                                              // [11]
    virtual int CalcItem(int x, int y) = 0;                                            // [12]
    virtual void EnableItem(int index, bool enabled) = 0;                              // [13]
    virtual float GetDistance() const = 0;                                             // [14]
    virtual float GetDirection() const = 0;                                            // [15]
    virtual int GetInactiveRadius() const = 0;                                         // [16]
    virtual void SetInactiveRadius(int radius) = 0;                                    // [17]
    virtual int GetCurrentItem() const = 0;                                            // [18]
    virtual int GetMaxRadius() const = 0;                                              // [19]
    virtual void SetMaxRadius(int radius) = 0;                                         // [20]
    virtual int GetAutoCancel() const = 0;                                             // [21]
    virtual void SetAutoCancel(int on) = 0;                                            // [22]
    virtual bool GetPoppedUp() const = 0;                                              // [23]
    virtual int GetPopupX() const = 0;                                                 // [24]
    virtual int GetPopupY() const = 0;                                                 // [25]
    virtual int GetPopupDownX() const = 0;                                             // [26]
    virtual int GetPopupDownY() const = 0;                                             // [27]
    virtual int GetPopupCenterX() const = 0;                                           // [28]
    virtual int GetPopupCenterY() const = 0;                                           // [29]
    virtual int GetCurItemCenterX() const = 0;                                         // [30]
    virtual int GetCurItemCenterY() const = 0;                                         // [31]
    virtual void SetTitle(const cIGZString& title) = 0;                                // [32]
    virtual bool GetTitle(cIGZString& out) const = 0;                                  // [33]
    virtual void SetPopupBackgroundBuffer(cIGZBuffer* buffer) = 0;                     // [34]
    virtual void SetFont(cIGZFont* font) = 0;                                          // [35]
    virtual cIGZFont* GetFont() const = 0;                                             // [36]
    virtual void SetLowlightColor(std::uint32_t native) = 0;                           // [37]
    virtual void SetHighlightColor(std::uint32_t native) = 0;                          // [38]
    virtual void SetSelectedColor(std::uint32_t native) = 0;                           // [39]
    virtual void SetLowlightColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0; // [40]
    virtual void SetHighlightColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;   // [41]
    virtual void SetSelectedColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0; // [42]
    virtual void SetFirstDirection(std::uint32_t direction) = 0;                       // [43]
    virtual void SetSpokeRadius(std::uint32_t radius) = 0;                             // [44]
    virtual void SetAbbreviated(std::uint32_t on) = 0;                                 // [45]
    virtual void SetArea(int l, int t, int r, int b) = 0;                              // [46]
    virtual int Popup(int x, int y, int clickMode, int endMode) = 0;                   // [47]
    virtual void PopupBegin(int x, int y, int clickMode, int endMode) = 0;             // [48]
    virtual int PopupEventLoop() = 0;                                                  // [49]
    virtual void PopupEnd() = 0;                                                       // [50]
    virtual void BeginPopupWindow(int x, int y, int clickMode, int endMode) = 0;       // [51]
    virtual void EndPopupWindow() = 0;                                                 // [52]
    virtual void SetRadii(int x, int y) = 0;                                           // [53]
    virtual void EndPopup() = 0;                                                       // [54]
    virtual void EndPopup(int selection) = 0;                                          // [55]
    virtual bool SetOwnerDraw(cIGZWinPieMenuOwnerDraw* owner) = 0;                     // [56]
};

class cGZWinPieMenu : public cGZWin, public cIGZWinPieMenu {
public:
    explicit cGZWinPieMenu(cIGZWinMgr* mgr);                       // @ 0x1005165f0
    ~cGZWinPieMenu() override;                                     // @ 0x100516746
    static cGZWinPieMenu* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinPieMenu(mgr); }

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1005168b2
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool Init() override;                                          // @ 0x10051693a
    bool Shutdown() override;                                      // @ 0x100516afa
    void SetArea(int l, int t, int r, int b) override { cGZWin::SetArea(l, t, r, b); }   // @ 0x100516b4c
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    bool GZPaint() override;                                       // @ 0x100517b20
    bool GZOnCharacter(char c) override;                           // @ 0x10051827e
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x1005182c4
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;     // @ 0x10051834e
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;       // @ 0x10051839a
    bool GZOnMouseDownR(int x, int y, std::uint32_t modifiers) override { return cGZWin::GZOnMouseDownR(x, y, modifiers); }   // @ 0x100518416
    bool GZOnMouseUpR(int x, int y, std::uint32_t modifiers) override { return cGZWin::GZOnMouseUpR(x, y, modifiers); }       // @ 0x100518420
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;      // @ 0x10051842a

    cIGZWin* AsIGZWin() override { return this; }
    float Pi() const override { return 3.1415927f; }               // @ 0x100516896
    void Clear() override;                                         // @ 0x1005180a6
    bool InsertString(const cIGZString& text, cIGZWinBtn* button) override;   // @ 0x1005181b0
    bool SetSelection(int index, bool commit) override;            // @ 0x100518528
    int GetSelection() const override { return selection_; }       // @ 0x1005186da
    bool Layout() override;                                        // @ 0x100517048
    bool ReLayout() override;                                      // @ 0x1005176c8
    int GetItemCount() const override { return static_cast<int>(items_.size()); }   // @ 0x1005186f0
    int CalcItem(int x, int y) override;                           // @ 0x100518744
    void EnableItem(int index, bool enabled) override;             // @ 0x100518924
    float GetDistance() const override { return distance_; }       // @ 0x10051898c
    float GetDirection() const override { return direction_; }     // @ 0x1005189a8
    int GetInactiveRadius() const override { return inactiveRadius_; }   // @ 0x1005189c4
    void SetInactiveRadius(int radius) override { inactiveRadius_ = radius; }   // @ 0x1005189dc
    int GetCurrentItem() const override { return selection_; }     // @ 0x1005189f4
    int GetMaxRadius() const override { return maxRadius_; }       // @ 0x100516e26
    void SetMaxRadius(int radius) override { maxRadius_ = radius; }   // @ 0x100516e3e
    int GetAutoCancel() const override { return autoCancel_; }     // @ 0x100516e56
    void SetAutoCancel(int on) override { autoCancel_ = on; }      // @ 0x100516e6e
    bool GetPoppedUp() const override { return poppedUp_; }       // @ 0x10051872c
    int GetPopupX() const override { return popupX_; }             // @ 0x100516e86
    int GetPopupY() const override { return popupY_; }             // @ 0x100516e9e
    int GetPopupDownX() const override { return downX_; }          // @ 0x100516eb6
    int GetPopupDownY() const override { return downY_; }          // @ 0x100516ecc
    int GetPopupCenterX() const override { return centerX_; }      // @ 0x100516ee2
    int GetPopupCenterY() const override { return centerY_; }      // @ 0x100516ef8
    int GetCurItemCenterX() const override;                        // @ 0x100516f0e
    int GetCurItemCenterY() const override;                        // @ 0x100516f7e
    void SetTitle(const cIGZString& title) override { title_.Copy(title); }   // @ 0x100517028
    bool GetTitle(cIGZString& out) const override { out.Copy(title_); return true; }   // @ 0x100516fee
    void SetPopupBackgroundBuffer(cIGZBuffer* buffer) override;    // @ 0x100516c22
    void SetFont(cIGZFont* font) override;                         // @ 0x100516b68
    cIGZFont* GetFont() const override { return font_; }           // @ 0x100516c0a
    void SetLowlightColor(std::uint32_t native) override { lowlight_ = native; }       // @ 0x100516cc4
    void SetHighlightColor(std::uint32_t native) override { highlight_ = native; }     // @ 0x100516cda
    void SetSelectedColor(std::uint32_t native) override { selected_ = native; }       // @ 0x100516cf0
    void SetLowlightColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { lowlight_ = MakeFillColor(r, g, b); }   // @ 0x100516d06
    void SetHighlightColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { highlight_ = MakeFillColor(r, g, b); } // @ 0x100516d4e
    void SetSelectedColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { selected_ = MakeFillColor(r, g, b); }   // @ 0x100516d96
    void SetFirstDirection(std::uint32_t direction) override { firstDirection_ = direction; }   // @ 0x100516dde
    void SetSpokeRadius(std::uint32_t radius) override { spokeRadius_ = radius; }     // @ 0x100516df6
    void SetAbbreviated(std::uint32_t on) override { abbreviated_ = on; }             // @ 0x100516e0e
    int Popup(int x, int y, int clickMode, int endMode) override;                     // @ 0x100518a0a
    void PopupBegin(int x, int y, int clickMode, int endMode) override;               // @ 0x100518a96
    int PopupEventLoop() override;                                                    // @ 0x100518b34
    void PopupEnd() override;                                                         // @ 0x100518b80
    void BeginPopupWindow(int x, int y, int clickMode, int endMode) override;         // @ 0x100518bba
    void EndPopupWindow() override;                                                   // @ 0x100518ea0
    void SetRadii(int x, int y) override { radiusX_ = x; radiusY_ = y; }              // @ 0x100518f50
    void EndPopup() override { EndPopup(selection_); }                                // @ 0x1005186a2
    void EndPopup(int selection) override;                                            // @ 0x100518646
    bool SetOwnerDraw(cIGZWinPieMenuOwnerDraw* owner) override;                       // @ 0x100518f74

    // internals
    void DrawLabelFrame(std::uint32_t color, int x, int y, int w, int h);             // @ 0x10051773a
    void DrawFramedLabel(const char* text, int index, int length, cIGZFont* font, std::uint32_t textColor,
                         std::uint32_t frameColor, int x, int y, int w, int h);       // @ 0x1005179de
    const PieMenuItem& Item(int index) const { return items_[static_cast<std::size_t>(index)]; }
    int SpokeCount() const { return spokes_; }
    int EventLoopActive() const { return eventLoop_; }
    bool ClickMode() const { return clickMode_; }

private:
    void Send(std::uint32_t type, std::uint64_t p2) { SendMsg(notify_, kGZMsgCommand, type, p2, 0); }

    int selection_ = -1;                   // +0x158
    std::uint32_t lowlight_ = 0, highlight_ = 0, selected_ = 0;   // +0x15c..+0x164
    cIGZFont* font_ = nullptr;             // +0x168
    cIGZBuffer* background_ = nullptr;     // +0x170
    cRZString title_;                      // +0x178
    int titleOriginX_ = 0, titleOriginY_ = 0;   // +0x1a0 / +0x1a4 the background's top-left (menu coordinates)
    int backgroundW_ = 0, backgroundH_ = 0;     // +0x1a8 / +0x1ac
    int titleX_ = 0, titleY_ = 0, titleW_ = 0, titleH_ = 0;   // +0x1b0..+0x1bc
    int downX_ = 0, downY_ = 0;            // +0x1c0 / +0x1c4 where the menu was opened
    int centerX_ = 0, centerY_ = 0;        // +0x1c8 / +0x1cc offset of the opening point inside the window
    int popupX_ = 0, popupY_ = 0;          // +0x1d0 / +0x1d4 window position
    int windowW_ = 0, windowH_ = 0;        // +0x1d8 / +0x1dc
    bool poppedUp_ = false, active_ = false;   // +0x1e0 / +0x1e1
    bool clickMode_ = false;               // +0x1e2
    bool moved_ = false;                   // +0x1e8 the window was pushed back inside its parent
    bool mouseDown_ = false;               // +0x1e9
    int spokes_ = 0;                       // +0x1ec
    std::uint32_t firstDirection_ = 0;     // +0x1f0
    int inactiveRadius_ = 0;               // +0x1f4
    float distance_ = 0, direction_ = 0;   // +0x1f8 / +0x1fc
    std::uint32_t spokeRadius_ = 8, abbreviated_ = 1;   // +0x200 / +0x204
    int maxRadius_ = 0;                    // +0x208
    int autoCancel_ = 0;                   // +0x20c
    int endMode_ = 1;                      // +0x210
    int radiusX_ = 0x28, radiusY_ = 0x14;  // +0x214 / +0x218
    int eventLoop_ = 0;                    // +0x21c
    cIGZWinPieMenuOwnerDraw* ownerDraw_ = nullptr;   // +0x220
    std::vector<PieMenuItem> items_;       // +0x228
};

}  // namespace gz
