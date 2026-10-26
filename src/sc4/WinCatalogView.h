// cSC4WinCatalogView (38 functions at 0x1002fcf76..0x1002ff086 + the flash timer callback 0x1002fd7e0): the vertical list of build
// icons in the tool palettes. Items (cISC4CatalogItem, counted) are drawn as a column of icons that each have four frames side by
// side in their image (disabled, normal, selected, highlighted); the mouse highlights the row under it and shows a callout box
// with the item's name (plus " - $cost"), the click posts a command (0x49d993d4) with the item's type and id to the notification
// target. A scroll position (rows) is driven by the mouse wheel (one row per 300 units) or the 0xca35e4ba message.
// Layout: the object is the interface (cISC4WinCatalogView, iid 0x49d98264) followed by the cGZWin at +8.
#pragma once
#include <string>
#include <vector>

#include "gz/WinFont.h"
#include "rz/cRZCallbackTimer.h"
#include "sc4/CatalogItem.h"
#include "sc4/ISC4WinCalloutBox.h"

namespace sc4 {

constexpr std::uint32_t kGZIID_cISC4WinCatalogView = 0x49d98264;
constexpr std::uint32_t kCatalogViewScrollTargetIID = 0x8a25ce2c;   // the parent's scroll buttons interface
constexpr std::uint32_t kCatalogMsgFlashTick = 0xaa5187a4;          // posted by the flash timer
constexpr std::uint32_t kCatalogMsgScroll = 0xca35e4ba;             // param1 = rows to scroll
constexpr std::uint32_t kCatalogMsgItemChosen = 0x49d993d4;         // command: type, id
constexpr std::uint32_t kCatalogSoundScroll = 0x2a5c322b, kCatalogSoundDisabled = 0xaa62c9d5;
constexpr std::uint32_t kCatalogDefaultFontStyle = 0x68963c4c;

// The parent window's scroll arrows (QueryInterface(0x8a25ce2c) on the parent; vslot 0x38).
class ICatalogScrollTarget : public cIGZUnknown {
public:
    virtual void SetScrollButtonsEnabled(bool up, bool down) = 0;   // vslot 0x38
};

class ICatalogViewHost {
public:
    virtual ~ICatalogViewHost() = default;
    virtual std::string MakeMoneyString(std::int64_t amount) = 0;   // SC4MakeMoneyString @ 0x1000392b0
    virtual void PlaySound(std::uint32_t id) = 0;                   // SL::spSS vslot 0x50 (1, id, 0, 0, 0)
    // draw::Blt of an item's icon (a cIGZBuffer) into the window's draw buffer; false when the host has no such buffers and
    // the window draws the icon as its own buffer type
    virtual bool BlitIcon(IWinBuffer* target, cIGZUnknown* icon, const cRZRect& source, const cRZRect& destination) { return false; }
};
ICatalogViewHost* CatalogViewHost();
void SetCatalogViewHost(ICatalogViewHost* host);

class cISC4WinCatalogView : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                    // [3]  0x18
    virtual void AddItem(cISC4CatalogItem* item) = 0;                   // [4]
    virtual cISC4CatalogItem* GetItem(int index) = 0;                   // [5]
    virtual cISC4CatalogItem* GetItemByID(int id) = 0;                  // [6]
    virtual cISC4CatalogItem* GetHighlightedItem() = 0;                 // [7]
    virtual cISC4CatalogItem* GetSelectedItem() = 0;                    // [8]
    virtual std::uint32_t GetSelectedItemID() = 0;                      // [9]
    virtual void SetSelectedItemByID(std::uint32_t id) = 0;             // [10]
    virtual void Clear() = 0;                                           // [11]
    virtual void SetItemSize(std::uint32_t width, std::uint32_t height, std::uint32_t spacing) = 0;   // [12]
    virtual cRZPoint CalcPreferredSize(std::uint32_t itemCount) = 0;    // [13]
    virtual int GetItemCount() = 0;                                     // [14]
    virtual void SetTextFontStyle(std::uint32_t style) = 0;             // [15]
    virtual void SetScrollPosition(int position) = 0;                   // [16]
    virtual int GetScrollPosition() const = 0;                          // [17]
    virtual bool SetEnabledAllItemsExceptItemWithID(std::uint32_t id, bool enabled) = 0;   // [18]
    virtual bool GetItemPositionByID(std::uint32_t id, int& x, int& y) = 0;                // [19]
    virtual bool MakeItemVisible(std::uint32_t id) = 0;                 // [20]
    virtual bool FlashItemByID(std::uint32_t id, std::uint32_t count, std::uint32_t period) = 0;   // [21]
    virtual void SetCalloutBox(cISC4WinCalloutBox* box) = 0;            // [22]
};

class cSC4WinCatalogView : public cISC4WinCatalogView, public cGZWin {
public:
    explicit cSC4WinCatalogView(cIGZWinMgr* mgr);      // @ 0x1002fcf76 (+ 0x1002fd092)
    ~cSC4WinCatalogView() override;                    // @ 0x1002fd09c (+ 0x1002fd1c8, 0x1002fd1d2, 0x1002fd1e0, 0x1002fd20e)

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1002fd24c (+ thunk 0x1002fd26e)
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }   // @ 0x1002fd21c (+ thunk 0x1002fd22a)
    std::uint32_t Release() override { return cGZWin::Release(); } // @ 0x1002fd234 (+ thunk 0x1002fd242)
    cIGZWin* AsIGZWin() override { return static_cast<cGZWin*>(this); }   // @ 0x1002fd294

    bool Init() override;                              // @ 0x1002fd29e (+ thunk 0x1002fd31e)
    bool Shutdown() override;                          // @ 0x1002fd330 (+ thunk 0x1002fd40c)
    bool DoMessage(cGZMessage& message) override;      // @ 0x1002fd9ba (+ thunk 0x1002fda10)
    bool GZPaint() override;                           // @ 0x1002fe15c (+ thunk 0x1002fe2fe)
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;   // @ 0x1002fdf16 (+ thunk 0x1002fdf5c)
    bool GZOnMouseExit(cIGZWin* to) override;          // @ 0x1002fdf6e (+ thunk 0x1002fdf80)
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;    // @ 0x1002fdf96 (+ thunk 0x1002fe092)
    bool GZOnMouseWheel(int x, int y, std::uint32_t modifiers, int delta) override;   // @ 0x1002fe0a4 (+ thunk 0x1002fe0f8)

    void AddItem(cISC4CatalogItem* item) override;     // @ 0x1002fd518
    cISC4CatalogItem* GetItem(int) override { return nullptr; }   // @ 0x1002fd574: quirk, always null
    cISC4CatalogItem* GetItemByID(int id) override;    // @ 0x1002fd57c
    cISC4CatalogItem* GetHighlightedItem() override;   // @ 0x1002fd806
    cISC4CatalogItem* GetSelectedItem() override;      // @ 0x1002fd830
    std::uint32_t GetSelectedItemID() override;        // @ 0x1002fddc4
    void SetSelectedItemByID(std::uint32_t id) override;   // @ 0x1002fddf2
    void Clear() override;                             // @ 0x1002fd4c2
    void SetItemSize(std::uint32_t width, std::uint32_t height, std::uint32_t spacing) override;   // @ 0x1002fd85a
    cRZPoint CalcPreferredSize(std::uint32_t itemCount) override;   // @ 0x1002fd872
    int GetItemCount() override { return count_; }     // @ 0x1002ff07a
    void SetTextFontStyle(std::uint32_t style) override;   // @ 0x1002fda1e
    void SetScrollPosition(int position) override;     // @ 0x1002fd8a0
    int GetScrollPosition() const override { return scroll_; }   // @ 0x1002ff086
    bool SetEnabledAllItemsExceptItemWithID(std::uint32_t id, bool enabled) override;   // @ 0x1002fd5c8
    bool GetItemPositionByID(std::uint32_t id, int& x, int& y) override;                // @ 0x1002fd636
    bool MakeItemVisible(std::uint32_t id) override;   // @ 0x1002fde54
    bool FlashItemByID(std::uint32_t id, std::uint32_t count, std::uint32_t period) override;   // @ 0x1002fd6f8
    void SetCalloutBox(cISC4WinCalloutBox* box) override;   // @ 0x1002fd41a

    void SetHighlightedItem(int index);                // @ 0x1002fda84
    void ChangeScrollPos(int delta);                   // @ 0x1002fd968

    // The flash timer's callback (catalog_flash_timer_callback @ 0x1002fd7e0): posts the tick message to the window itself.
    static void FlashTimerCallback(void* view);

    cRZCallbackTimer& FlashTimer() { return flashTimer_; }
    int FlashCountdown() const { return flashCount_; }
    int HighlightedIndex() const { return highlighted_; }
    int SelectedIndex() const { return selected_; }

private:
    int VisibleRows() const;

    std::vector<cISC4CatalogItem*> items_;   // +0x158 (each holds a reference)
    int count_ = 0;                          // +0x170
    int scroll_ = 0;                         // +0x174
    int highlighted_ = -1;                   // +0x178
    int selected_ = -1;                      // +0x17c
    std::uint32_t itemWidth_ = 0, itemHeight_ = 0, itemSpacing_ = 0;   // +0x180 / +0x184 / +0x188
    gz::IWinFont* font_ = nullptr;           // +0x190
    cISC4WinCalloutBox* callout_ = nullptr;  // +0x198
    cIGZUnknown* unknown1a0_ = nullptr;      // +0x1a0: released with the window, never set by recovered code
    cRZCallbackTimer flashTimer_;            // +0x1a8
    int flashCount_ = 0;                     // +0x1e0: flash ticks left (odd = lit)
    int flashIndex_ = 0;                     // +0x1e4
};

}  // namespace sc4
