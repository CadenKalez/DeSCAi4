// cSC4WinAdviceList — the advisor panel's list of advice, rendered as an HTML table inside a cGZWinHTML child
// (0x1005c7918..0x1005c8b6c, 32 symbols, class id 0xca1492ac, object 0x180 bytes).
//
// The list is not a listbox: every entry is stored in a std::list<cAdviceEntry>, and RebuildHTML re-generates the
// whole document as an HTML table with three cells per row (the advice title as an "item<N>" anchor with a toggle
// link, the description plus the extra detail line when the entry is enabled, and a close link) and hands it to the
// cGZWinHTML child. Clicks come back as a kGZMsgLinkJump notification (0xe4a85d91) whose param2 is the clicked URL:
// "sc4://action/toggle?item=N" flips the entry's enabled bit and tells the advice (cISC4Advice::SetFlags(0x20, on)),
// "sc4://action/close?item=N" marks the advice closed (SetFlags(8, true)) and posts the advice-removed message.
//
// Object layout (0x180; the cGZWin base is the original's 0x150 bytes):
//   +0x00 cGZWin                    +0x150 cIGZWinAdviceList view (iid 0xca1492a2)
//   +0x158 cIGZWinHTML* html child (owned; created by Init, never released by Shutdown)
//   +0x160 std::list<cAdviceEntry> entries   +0x178 u32 background image id
//
// cAdviceEntry (0xe0 bytes; the copy ctor 0x1005c8a1c and the dtor 0x1005c893c):
//   +0x00 cRZString e (the close link's text)   +0x28 a (title)   +0x50 b (title when disabled)
//   +0x78 c (the extra line shown when enabled) +0xa0 d (the toggle link's text)
//   +0xc8 bool enabled   +0xd0 cISC4Advice* advice (one reference)   +0xd8 bool noLink (no anchors at all)
//
// Quirks kept:
//  * QueryInterface answers iid 1 with the *advice-list* view (this+0x150), not with the primary window; iid
//    0x22ba0121 is the primary and 0xca1492a2 the list view (0x1005c79e2..0x1005c7a01).
//  * Both link handlers return false (the message counts as unhandled) even though they did the work; a link that
//    is neither toggle nor close is passed to cSC4HTMLLinkHandler::DoURL and the notification's answer inverted.
//    The UI click sound 0x2a5c322b is played for every link message, before the URL is even looked at.
//  * Shutdown clears the entries and stops at cGZWin::Shutdown: the html child's reference is not released.
//  * RebuildHTML calls the html window even when Init failed (the original dereferences +0x158 without a check);
//    the port does nothing in that case.
//  * A toggle on an advice-less entry would dereference null in the original; the port posts nothing for it.
//  * The entry index parsed out of a link is used without a bounds check in the original (a negative or too large
//    index walks off the list and writes into the list object); the port refuses such a link instead.
//  * The empty background path writes the fixed "#dee8e3" document; an image id makes the header use
//    "sc4://HTML/46a006b0/%x". The column width is the html window's width minus 0x3d.
#pragma once
#include <cstdint>
#include <list>

#include "gz/Win.h"
#include "rz/cRZString.h"

class cIGZString;
class cIGZMessageServer2;
namespace sc4 { class cISC4Advice; }

namespace sc4 {

constexpr std::uint32_t kGZCLSID_cSC4WinAdviceList = 0xca1492ac;
constexpr std::uint32_t kGZIID_cIGZWinAdviceList = 0xca1492a2;   // the +0x150 view (also answered for iid 1)
constexpr std::uint32_t kGZIID_cSC4WinAdviceList = 0x22ba0121;   // the primary window view
constexpr std::uint32_t kWinAdviceListSize = 0x180;

// cGZMessage type the cGZWinHTML child sends when a link is clicked (gz/WinHTML.h kWinHtmlMsgLinkJump). param2 is
// the clicked URL string.
constexpr std::uint32_t kAdviceListMsgLinkJump = 0xe4a85d91;
// The two URL prefixes RebuildHTML writes and DoMessage parses (0x100a252e0 / 0x100a25300).
constexpr char kAdviceListUrlToggle[] = "sc4://action/toggle?";
constexpr char kAdviceListUrlClose[] = "sc4://action/close?";
constexpr std::size_t kAdviceListUrlToggleLength = 20;
constexpr std::size_t kAdviceListUrlCloseLength = 19;
// The click sound every link notification plays (SL::spSS vslot 0x50 with (1, id, 0, 0, 0)); the same id
// cSC4WinCatalogView uses for its scroll tick.
constexpr std::uint32_t kAdviceListClickSound = 0x2a5c322b;
// The messages the two link handlers post: the toggle notification (advisor id, mood) and the advice-removed one
// (the advice as its unknown payload, type as data1).
constexpr std::uint32_t kAdviceListMsgAdviceToggled = 0x426841a1;
constexpr std::uint32_t kAdviceListMsgAdviceRemoved = 0xca09fa6c;
// cISC4Advice flag bits the handlers touch (the no-links bit 0x8 is Advice.h kAdviceFlagNoLinks; 0x20 is not one
// of the documented flags but the toggle handler sets it).
constexpr int kAdviceListToggleAdviceFlag = 0x20;
constexpr int kAdviceListNoLinksAdviceFlag = 0x8;
// The column separator width the header subtracts from the html window's width (0x1005c832b).
constexpr int kAdviceListColumnBias = 0x3d;
// The html child flag Init clears (0x40, the same bit cGZWinHTML::AddScrollBar's caller drops).
constexpr int kAdviceListHtmlFlag = 0x40;
// The scrollbar permission DisableVerticalScrollbar clears (gz/WinHTML.h kHtmlVScroll).
constexpr int kAdviceListHtmlVScrollFlag = 0x8;
// The base URL every rebuilt document is given (the inline "sc4://" cRZString at 0x1005c86fe).
constexpr char kAdviceListBaseUrl[] = "sc4://";

// -------------------------------------------------------------------------------------------------
// cIGZWinAdviceList (iid 0xca1492a2): the +0x150 view, slots 3..10 in binary order.
// -------------------------------------------------------------------------------------------------
class cIGZWinAdviceList : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                              // [3]  0x18 @ 0x1005c88b2
    virtual void AddEntry(const cIGZString& a, const cIGZString& b, const cIGZString& c, const cIGZString& d,
                          bool enabled, cISC4Advice* advice, const cIGZString& e, bool noLink,
                          std::uint32_t image) = 0;               // [4]  0x20 @ 0x1005c806e
    virtual void RebuildHTML(bool force) = 0;                     // [5]  0x28 @ 0x1005c82a0
    virtual void SetbgImage(std::uint32_t image) = 0;             // [6]  0x30 @ 0x1005c828a
    virtual void ClearList() = 0;                                 // [7]  0x38 @ 0x1005c8828
    virtual void SetGutters(int left, int top, int right, int bottom) = 0;   // [8]  0x40 @ 0x1005c7c0c
    virtual void ScrollToAnchor(const cIGZString& anchor) = 0;    // [9]  0x48 @ 0x1005c8848
    virtual void DisableVerticalScrollbar() = 0;                  // [10] 0x50 @ 0x1005c8870
};

// The cGZWinHTML child as the advice list drives it (every method is one original call; the slot numbers are the
// original cIGZWinHTML / cGZWin vslots).
class IAdviceListHtml {
public:
    virtual ~IAdviceListHtml() = default;
    virtual void Release() = 0;                                             // cGZWinHTML::Release
    virtual cIGZWin* AsIGZWin() = 0;                                        // cIGZWinHTML [3] 0x18
    virtual int WindowWidth() = 0;                                          // AsIGZWin()->GetW()        [41] 0x148
    virtual void SetArea(int left, int top, int right, int bottom) = 0;     // AsIGZWin()->SetArea       [54] 0x1b0
    virtual void GetScrollPosition(int& x, int& y) = 0;                     // cIGZWinHTML               [6]  0x30
    virtual void SetScrollPosition(int x, int y) = 0;                       //                           [7]  0x38
    virtual bool ScrollToAnchor(const cIGZString& anchor) = 0;              //                           [9]  0x48
    virtual bool SetDocument(const std::string& html, const cIGZString& baseUrl) = 0;   //          [22] 0xb0
    virtual void ClearFlag(int flag) = 0;                                   //                           [28] 0xe0
    virtual void SetGutters(int left, int top, int right, int bottom) = 0;  //                           [32] 0x100
};

// One row of the list.
struct cAdviceEntry {
    cAdviceEntry() = default;
    cAdviceEntry(const cAdviceEntry& other);                 // @ 0x1005c8a1c (AddRefs the advice)
    cAdviceEntry& operator=(const cAdviceEntry& other) = delete;
    ~cAdviceEntry();                                         // @ 0x1005c893c (Releases the advice)
    void SetAdvice(cISC4Advice* value);                      // the AddRef/Release pair of AddEntry 0x1005c8194

    cRZString e;                        // +0x00 the close link's text
    cRZString a;                        // +0x28 the title
    cRZString b;                        // +0x50 the title shown when the entry is disabled
    cRZString c;                        // +0x78 the extra line shown when enabled
    cRZString d;                        // +0xa0 the toggle link's text
    bool enabled = false;               // +0xc8
    cISC4Advice* advice = nullptr;      // +0xd0 (one reference)
    bool noLink = false;                // +0xd8
};

// The engine services of the original, one method per call site.
class IWinAdviceListHost {
public:
    virtual ~IWinAdviceListHost() = default;
    // GZCOM()->vtable 0x18 CreateInstance(0xaa12e5f5 = cGZWinHTML, 0x4a11fd4a = cIGZWinHTML, &out): one reference.
    virtual IAdviceListHtml* CreateHtmlWindow() = 0;
    // SL::spMS2: cIGZMessageServer2 (MessagePost 0x20 / MessageSend 0x18).
    virtual cIGZMessageServer2* MessageServer() = 0;
    // SL::spSS-vslot 0x50 PostEventIds(1, id, 0, 0, 0).
    virtual void PlaySound(std::uint32_t id) = 0;
    // cSC4HTMLLinkHandler::DoURL @ 0x1001f22b6 (static; true when the link was followed).
    virtual bool HandleUrl(const cIGZString& link) = 0;
};
IWinAdviceListHost* WinAdviceListHost();
void SetWinAdviceListHost(IWinAdviceListHost* host);

// -------------------------------------------------------------------------------------------------
// The object.
// -------------------------------------------------------------------------------------------------
class cSC4WinAdviceList final : public cGZWin, public cIGZWinAdviceList {
public:
    explicit cSC4WinAdviceList(cIGZWinMgr* mgr);    // the GZCOM creator allocates 0x180 bytes
    ~cSC4WinAdviceList() override;                  // @ 0x1005c7918 (D2), 0x1005c7954 (D0)

    // cIGZUnknown / cGZWin
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x1005c79e2
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }   // @ 0x1005c79aa
    std::uint32_t Release() override { return cGZWin::Release(); } // @ 0x1005c79c6

    bool Init() override;                                        // @ 0x1005c7a54
    bool Shutdown() override;                                    // @ 0x1005c7b7a
    void SetArea(int left, int top, int right, int bottom) override;   // @ 0x1005c7b9a
    bool DoMessage(cGZMessage& message) override;                // @ 0x1005c7c48

    // cIGZWinAdviceList
    cIGZWin* AsIGZWin() override;                                // @ 0x1005c88b2
    void AddEntry(const cIGZString& a, const cIGZString& b, const cIGZString& c, const cIGZString& d, bool enabled,
                  cISC4Advice* advice, const cIGZString& e, bool noLink, std::uint32_t image) override;   // 0x1005c806e
    void RebuildHTML(bool force) override;                       // @ 0x1005c82a0
    void SetbgImage(std::uint32_t image) override;               // @ 0x1005c828a
    void ClearList() override;                                   // @ 0x1005c8828
    void SetGutters(int left, int top, int right, int bottom) override;   // @ 0x1005c7c0c
    void ScrollToAnchor(const cIGZString& anchor) override;      // @ 0x1005c8848
    void DisableVerticalScrollbar() override;                    // @ 0x1005c8870

    // what the game side reads back
    IAdviceListHtml* HtmlWindow() const { return html_; }        // +0x158
    std::uint32_t BackgroundImage() const { return bgImage_; }   // +0x178
    const std::list<cAdviceEntry>& Entries() const { return entries_; }   // +0x160

private:
    IAdviceListHtml* html_ = nullptr;    // +0x158 (one reference)
    std::list<cAdviceEntry> entries_;    // +0x160
    std::uint32_t bgImage_ = 0;          // +0x178
};

}  // namespace sc4
