// cGZWinGen — the generic window / dialog frame (0x10024dae6..0x100250700, 63 functions, iid 0x5386d516 for the cIGZWinGen
// view at +0x150, class id 0x4386d516) and cGZWinGenTitleBar (0x10024c880..0x10024dad0, 32 functions, iids 0x37c86704 for the
// title-bar view at +0x170 and 0xc2afa76f for its flat-rect base). A Gen window draws a bevelled or bitmap frame, owns an
// optional title bar and side bar, lets the user drag and resize it, keeps the "user area" inside the gutters and forwards
// everything to an attached window procedure (cIGZWinProc) through a fixed set of messages:
//   0xa2bf8ad0 window created, ad1 destroyed, ad2 size limits (p1 = {min w, min h, max w, max h}*, p2 = user area*),
//   ad3 laid out (p1 = user area*, p2 = window area*), ad4 hit test (p1 = x << 16 | y, p2 = bool*), acd / ace / acf paint
//   before / after (p1 = draw area*), ad5 / ad6 proc attached / detached (sent by SetWinProc), ad8 layout requested.
// Flags (0x158): 1 title bar moves the window, 2 resizable by the edges, 4 Escape / Ctrl+F4 closes, 0x10 / 0x20 / 0x40 close /
// zoom / minimise button shown (title bar flags 1 / 4 / 2), 0x100 / 0x200 / 0x400 the matching button disabled, 0x1000 the window
// is a modal dialog without a title bar (closing it ends the dialog), 0x2000 no background, 0x4000 flat fill, 0x8000 do not
// paint, 0x10000 side bar.
// Size modes (calc_size_mode): 0 none, 1 move (title bar), 2..9 the four edges / corners; the cursor follows the mode.
#pragma once
#include <cstdint>
#include <map>

#include "gz/Font.h"
#include "gz/Win.h"
#include "gz/WinBtn.h"
#include "gz/WinFlatRect.h"
#include "gz/WinProc.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinGen = 0x5386d516, kGZCLSID_cGZWinGen = 0x4386d516;
constexpr std::uint32_t kGZIID_cIGZWinGenTitleBar = 0x37c86704;
constexpr std::uint32_t kWinGenTitleBarId = 0x42b7c351, kWinGenSideBarId = 0x42b7c352, kWinGenCloseId = 0x42b7c353,
                        kWinGenMinimizeId = 0x42b7c354, kWinGenZoomId = 0x42b7c355, kWinGenCloseCommand = 0x42b7c356;
constexpr std::uint32_t kWinProcCreated = 0xa2bf8ad0, kWinProcDestroyed = 0xa2bf8ad1, kWinProcSizeLimits = 0xa2bf8ad2,
                        kWinProcLaidOut = 0xa2bf8ad3, kWinProcHitTest = 0xa2bf8ad4, kWinProcPaintBefore = 0xa2bf8acd,
                        kWinProcPaintAfter = 0xa2bf8ace, kWinProcPaintDone = 0xa2bf8acf, kWinProcLayoutRequest = 0xa2bf8ad8;
enum WinGenFlag : std::uint32_t {
    kGenMovable = 1, kGenResizable = 2, kGenEscapeCloses = 4, kGenCloseButton = 0x10, kGenZoomButton = 0x20, kGenMinimizeButton = 0x40,
    kGenCloseDisabled = 0x100, kGenMinimizeDisabled = 0x200, kGenZoomDisabled = 0x400, kGenDialog = 0x1000, kGenNoBackground = 0x2000,
    kGenFlatFill = 0x4000, kGenNoPaint = 0x8000, kGenSideBar = 0x10000,
};
enum BGBltType : std::uint32_t { kBGTiled = 0, kBGStretched = 1, kBGEdges = 2 };

// What the Gen window needs from the message server: it asks to be told about 0xa2bf8ad8 (relayout requests).
class IWinGenHost {
public:
    virtual ~IWinGenHost() = default;
    virtual void AddLayoutListener(cIGZWin* window) = 0;
    virtual void RemoveLayoutListener(cIGZWin* window) = 0;
};
IWinGenHost* WinGenHost();
void SetWinGenHost(IWinGenHost* host);

class cIGZWinGenTitleBar : public cIGZUnknown {
public:
    virtual bool GetDisplayCaption(cIGZString** out) const = 0;                          // [3] (adds a reference)
    virtual bool GetGutters(int& l, int& t, int& r, int& b) const = 0;                   // [4]
    virtual bool GetTitleFont(std::uint32_t iid, void** out) const = 0;                  // [5]
};

class cGZWinGenTitleBar : public cGZWinFlatRect, public cIGZWinGenTitleBar {
public:
    explicit cGZWinGenTitleBar(cIGZWinMgr* mgr);                   // @ 0x10024c880
    ~cGZWinGenTitleBar() override;                                 // @ 0x10024c976
    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10024ca34
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool Init() override;                                          // @ 0x10024cb0c
    bool Shutdown() override;                                      // @ 0x10024d0c6
    void SetCaption(const cIGZString& caption) override;           // @ 0x10024d196
    void SetArea(int l, int t, int r, int b) override;             // @ 0x10024d44c
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    bool GZPaint() override;                                       // @ 0x10024d78e
    bool AutoSize(const cRZRect& parentArea) override;             // @ 0x10024d82a
    using cGZWin::GetFillColor;

    bool SetTitleFont(cIGZFont* font);                             // @ 0x10024cf02
    bool GetMinSize(int& width, int& height);                      // @ 0x10024d8d8
    bool SetTitleBarFlag(int flag, bool on);                       // @ 0x10024d9fc
    bool calc_display_caption();                                   // @ 0x10024d1b2
    bool place_controls();                                         // @ 0x10024d4de
    bool create_and_add_sys_btn(std::uint32_t id, cIGZBuffer* image, const cRZRect& area, cIGZWinBtn*& out);   // @ 0x10024cf56
    bool GetDisplayCaption(cIGZString** out) const override;       // @ 0x10024d958
    bool GetGutters(int& l, int& t, int& r, int& b) const override { l = t = r = b = 4; return true; }   // @ 0x10024d998
    bool GetTitleFont(std::uint32_t iid, void** out) const override;   // @ 0x10024d9c4
    const cRZString& DisplayCaption() const { return *displayCaption_; }
    int TitleX() const { return titleX_; }
    int TitleY() const { return titleY_; }

private:
    std::uint32_t barFlags_ = 0;           // +0x178
    cIGZFont* font_ = nullptr;             // +0x180
    std::uint32_t textColor_ = 0;          // +0x188
    cRZString* displayCaption_ = nullptr;  // +0x190
    int titleX_ = 0, titleY_ = 0;          // +0x198 / +0x19c
    int rightLimit_ = 0;                   // +0x1a0 where the buttons start
    cIGZWinBtn* closeButton_ = nullptr;    // +0x1a8
    cIGZWinBtn* minimizeButton_ = nullptr; // +0x1b0
    cIGZWinBtn* zoomButton_ = nullptr;     // +0x1b8
};

class cIGZWinGen : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                       // [3]
    virtual std::uint32_t GetCLSID() const = 0;                                            // [4]
    virtual bool GetWinGenFlag(int flag) const = 0;                                        // [5]
    virtual bool SetWinGenFlag(int flag, bool on) = 0;                                     // [6]
    virtual bool SetWinProc(cIGZWinProc* proc) = 0;                                        // [7]
    virtual cIGZWinProc* GetWinProc() const = 0;                                           // [8]
    virtual bool GetWinProcAs(std::uint32_t iid, void** out) const = 0;                    // [9]
    virtual bool GetUserArea(cRZRect& out) const = 0;                                      // [10]
    virtual bool SetUserArea(const cRZRect& area) = 0;                                     // [11]
    virtual bool SetBackgroundBitmap(BGBltType type, cIGZBuffer* image) = 0;               // [12]
    virtual bool GetBackgroundBitmap(BGBltType& type, cIGZBuffer*& image) const = 0;       // [13]
    virtual bool SetBackgroundBitmap(cIGZBuffer* image) = 0;                               // [14]
    virtual bool GetBackgroundBitmap(cIGZBuffer** out) const = 0;                          // [15]
    virtual bool SetBackgroundBlitType(BGBltType type) = 0;                                // [16]
    virtual bool GetBackgroundBlitType(BGBltType& type) const = 0;                         // [17]
    virtual bool SetUserData(std::uint32_t slot, std::uint32_t value) = 0;                 // [18]
    virtual bool GetUserData(std::uint32_t slot, std::uint32_t& value) const = 0;          // [19]
    virtual bool SetUserData(std::uint32_t value) = 0;                                     // [20]
    virtual std::uint32_t GetUserData() const = 0;                                         // [21]
    virtual void SetUserData(std::uint32_t id, cIGZUnknown* object) = 0;                   // [22]
    virtual bool GetUserData(std::uint32_t id, std::uint32_t iid, void** out) = 0;         // [23]
    virtual void RemoveUserData(std::uint32_t id) = 0;                                     // [24]
    virtual bool SetGutters(std::uint8_t x, std::uint8_t y) = 0;                           // [25]
    virtual bool GetGutters(int& x, int& y) = 0;                                           // [26]
    virtual void SetGutters(std::int8_t l, std::int8_t t, std::int8_t r, std::int8_t b) = 0;   // [27]
    virtual bool GetGutters(int& l, int& t, int& r, int& b) const = 0;                     // [28]
    virtual bool AddColorBar(int id, int sizeMode, int outlineFlags, const cRZRect& area) = 0;        // [29]
    virtual void AddDividerH(int id, int y) = 0;                                           // [30]
    virtual bool RemoveColorBar(int id) = 0;                                               // [31]
};

class cGZWinGen : public cGZWin, public cIGZWinGen {
public:
    explicit cGZWinGen(cIGZWinMgr* mgr);                           // @ 0x10024dbae
    ~cGZWinGen() override;                                         // @ 0x10024dcea
    static cGZWin* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinGen(mgr); }   // @ 0x10024dae6

    bool QueryInterface(std::uint32_t iid, void** out) override;   // @ 0x10024ddc0
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }
    bool DoMessage(cGZMessage& message) override;                  // @ 0x10024f01c
    bool Init() override;                                          // @ 0x10024de48
    bool Shutdown() override;                                      // @ 0x10024e74a
    void SetArea(int l, int t, int r, int b) override;             // @ 0x10024e902
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    void SetCaption(const cIGZString& caption) override;           // @ 0x10024ec0c
    bool IsPointInWindowScreenCoordinates(int x, int y) const override;   // @ 0x10024ec42
    bool IsPointInWindowWindowCoordinates(int x, int y) const override;   // @ 0x10024ec8a
    void SetCursor(cIGZCursor* cursor, bool updateNow) override;   // @ 0x1002505c0
    bool GZPaint() override;                                       // @ 0x10024ed30
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;   // @ 0x10024f14e
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;     // @ 0x10024f260
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;     // @ 0x10024f2be
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;       // @ 0x10024f47c
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;      // @ 0x10024f4a0
    bool GZOnCommand(std::uint32_t sender, std::uint64_t id) override;       // @ 0x10024f6c8
    using cGZWin::GetCaption;

    cIGZWin* AsIGZWin() override { return this; }
    std::uint32_t GetCLSID() const override { return kGZCLSID_cGZWinGen; }   // @ 0x10024fb5e
    bool GetWinGenFlag(int flag) const override { return (genFlags_ & static_cast<std::uint32_t>(flag)) != 0; }   // @ 0x10024f700
    bool SetWinGenFlag(int flag, bool on) override;                // @ 0x10024f71c
    bool SetWinProc(cIGZWinProc* proc) override;                   // @ 0x10024fb76
    bool SetUserTarget(cIGZWinProc* proc) { return SetWinProc(proc); }   // @ 0x10024fd14 (tail call to vslot 0x500)
    cIGZWinProc* GetWinProc() const override { return proc_; }     // @ 0x10024fcc4
    bool GetWinProcAs(std::uint32_t iid, void** out) const override { return proc_ && proc_->QueryInterface(iid, out); }   // @ 0x10024fcdc
    bool GetUserArea(cRZRect& out) const override { out = userArea_; return true; }   // @ 0x10024fada
    bool SetUserArea(const cRZRect& area) override { userArea_ = area; return true; }   // @ 0x10024fb10
    bool SetBackgroundBitmap(BGBltType type, cIGZBuffer* image) override;   // @ 0x10024fd26
    bool GetBackgroundBitmap(BGBltType& type, cIGZBuffer*& image) const override { type = bgType_; image = bg_; return true; }   // @ 0x10024fe24
    bool SetBackgroundBitmap(cIGZBuffer* image) override;          // @ 0x10024fd9e
    bool GetBackgroundBitmap(cIGZBuffer** out) const override;     // @ 0x10024fe52
    bool SetBackgroundBlitType(BGBltType type) override { bgType_ = type; return true; }   // @ 0x10024feb2
    bool GetBackgroundBlitType(BGBltType& type) const override { type = bgType_; return true; }   // @ 0x10024fe94
    bool SetUserData(std::uint32_t slot, std::uint32_t value) override { return slot == 0 && SetUserData(value); }   // @ 0x10024fefc
    bool GetUserData(std::uint32_t slot, std::uint32_t& value) const override { if (slot == 0) value = userData_; return slot == 0; }   // @ 0x10024ff3a
    bool SetUserData(std::uint32_t value) override { userData_ = value; return true; }   // @ 0x10024fecc
    std::uint32_t GetUserData() const override { return userData_; }   // @ 0x10024fee6
    void SetUserData(std::uint32_t id, cIGZUnknown* object) override;   // @ 0x10024ff8c
    bool GetUserData(std::uint32_t id, std::uint32_t iid, void** out) override;   // @ 0x10025010e
    void RemoveUserData(std::uint32_t id) override { objects_.erase(id); }   // @ 0x100250172
    bool SetGutters(std::uint8_t x, std::uint8_t y) override { gutterX_ = x; gutterY_ = y; return true; }   // @ 0x1002501d2
    bool GetGutters(int& x, int& y) override { x = gutterX_; y = gutterY_; return true; }   // @ 0x1002501f8
    void SetGutters(std::int8_t l, std::int8_t t, std::int8_t, std::int8_t) override { SetGutters(static_cast<std::uint8_t>(l), static_cast<std::uint8_t>(t)); }   // @ 0x100250226
    bool GetGutters(int& l, int& t, int& r, int& b) const override { l = r = gutterX_; t = b = gutterY_; return true; }   // @ 0x100250262
    bool AddColorBar(int id, int sizeMode, int outlineFlags, const cRZRect& area) override;   // @ 0x1002502b2
    void AddDividerH(int id, int y) override;                      // @ 0x1002504b0
    bool RemoveColorBar(int id) override;                          // @ 0x10025054a

    // internals
    bool create_title_bar();                                       // @ 0x10024e066
    bool destroy_title_bar();                                      // @ 0x10024f898
    bool create_side_bar();                                        // @ 0x10024f96c
    bool calc_user_area(int width, int height, cRZRect& out);      // @ 0x10024e500
    bool calc_win_area(const cRZRect& user, cRZRect& out);         // @ 0x10024eb76
    bool on_layout();                                              // @ 0x10024e598
    bool kill_self(int code);                                      // @ 0x10024f1da
    int calc_size_mode(int x, int y);                              // @ 0x10024f314
    static bool enum_flat_rects(cIGZWin* parent, std::uint32_t id, void* child, void* context);   // @ 0x10025062e
    cGZWinGenTitleBar* TitleBar() const { return titleBar_; }
    int MinWidth() const { return minW_; }
    int MinHeight() const { return minH_; }
    int SizeMode() const { return sizeMode_; }
    const cRZRect& UserArea() const { return userArea_; }

private:
    bool Dispatch(std::uint32_t type, std::uint64_t p1 = 0, std::uint64_t p2 = 0, std::uint64_t p3 = 0);
    void ApplySizeLimits(cRZRect& user, const cRZRect& limits);

    std::uint32_t genFlags_ = 0x15;        // +0x158
    int sizeMode_ = 0;                     // +0x15c
    int cursorMode_ = 0;                   // +0x160
    int minW_ = 0, minH_ = 0;              // +0x164 / +0x168
    cGZWinGenTitleBar* titleBar_ = nullptr;   // +0x170
    BGBltType bgType_ = kBGEdges;          // +0x178
    cIGZBuffer* bg_ = nullptr;             // +0x180
    cRZRect userArea_{};                   // +0x188
    cIGZWinProc* proc_ = nullptr;          // +0x198
    std::uint32_t userData_ = 0;           // +0x1a0
    std::uint8_t gutterX_ = 4, gutterY_ = 4;   // +0x1a4 / +0x1a5
    cIGZCursor* cursor_gen_ = nullptr;     // +0x1a8
    std::map<std::uint32_t, cIGZUnknown*> objects_;   // +0x1b0 (a reference each)
};

}  // namespace gz
