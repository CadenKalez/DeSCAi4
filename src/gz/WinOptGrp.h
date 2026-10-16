// cGZWinOptGrp — a group of option (radio) buttons with an optional caption label (0x10027c790..0x10027e4f2, 57
// functions; iid 0xa1336cc0 for the group view at +0x150). Buttons are cIGZWinBtn children; the group value is the id
// of the selected one. Buttons flow down the group and wrap into new columns (auto fill) or sit where the owner
// put them (manual fill, type 1).
//
// Flags: 1 caption label child (id 0x2e3a3e5), 2 buttons are toggles, 4 opaque background, 8 outline,
// 0x100000 / 0x400000 label colours differ from the label font's, 0x1000000 / 0x2000000 / 0x4000000 button colours
// (normal / hover-pressed / disabled) differ from the button font's.
// Font colour indices: 0 label enabled, 1 label disabled, 2..4 button colours.
//
// Layout (0x1a8 bytes): cGZWin (0x150), +0x150 cIGZWinOptGrp view, +0x158 flags (0xb), +0x15c button class id,
// +0x160 fill type (0 auto) and +0x164 its height limit (0x80), +0x168 / +0x169 gutters (4, 4), +0x16a..0x170
// outline colours (four 16-bit values: left, top, right, bottom), +0x174 value, +0x178 background bitmap, +0x180
// label font, +0x188 button font, +0x190 five font colours (0xffffffff = the font's own).
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "gz/Win.h"
#include "gz/WinBtn.h"
#include "gz/WinCtrlFactory.h"
#include "gz/WinFont.h"
#include "gz/WinText.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinOptGrp = 0xa1336cc0;
constexpr std::uint32_t kGZCLSID_cGZWinOptGrp = 0xa1336cc1;
constexpr std::uint32_t kOptGrpLabelId = 0x2e3a3e5;
constexpr std::uint32_t kOptGrpButtonClass = 0xa8963e4b;          // what OptBtnAdd asks the control manager for
constexpr std::uint32_t kOptGrpLabelClass = 0x48963e33;
constexpr std::uint32_t kGZCmdOptGrpSelect = 0x287259f6;           // command that selects a button (parameter = id)
constexpr std::uint32_t kGZMsgOptGrpChanged = 0x88710f1c;          // command posted to the notification target
constexpr std::uint32_t kOptGrpFlagLabel = 1, kOptGrpFlagToggle = 2, kOptGrpFlagOpaque = 4, kOptGrpFlagOutline = 8;
constexpr std::uint32_t kOptGrpFontButtonSystemId = 0xc8963c5c;    // bold button font, falling back to kWinDefaultFontStyle

class cIGZWinOptGrp : public cIGZUnknown {
public:
    enum tOptGrpFontColorIdx : std::uint32_t { kLabelEnabled = 0, kLabelDisabled = 1, kButtonNormal = 2, kButtonHover = 3, kButtonDisabled = 4 };
    virtual cIGZWin* AsIGZWin() = 0;                                                          // [3]
    virtual bool GetOptGrpFlag(int flag) const = 0;                                           // [4]
    virtual bool SetOptGrpFlag(int flag, bool on) = 0;                                        // [5]
    virtual bool SetGutters(std::uint8_t x, std::uint8_t y) = 0;                              // [6]
    virtual bool GetGutters(int& x, int& y) const = 0;                                        // [7]
    virtual bool SetLabelFont(IWinFont* font) = 0;                                            // [8]
    virtual bool SetBtnFont(IWinFont* font) = 0;                                              // [9]
    virtual bool SetFontColor(tOptGrpFontColorIdx index, std::uint32_t color) = 0;            // [10]
    virtual bool GetFontColor(tOptGrpFontColorIdx index, std::uint32_t& color) const = 0;     // [11]
    virtual bool ClearFontColor(tOptGrpFontColorIdx index) = 0;                               // [12]
    virtual std::uint32_t GetValue() const = 0;                                               // [13]
    virtual bool SetValue(std::uint32_t id) = 0;                                              // [14]
    virtual bool OptBtnAdd(std::uint32_t id, const cIGZString& caption, std::uint32_t unused, cIGZBuffer* image, const cRZRect* area) = 0;   // [15]
    virtual bool OptBtnDelete(std::uint32_t id) = 0;                                          // [16]
    virtual bool OptBtnMoveTo(std::uint32_t id, int x, int y) = 0;                            // [17]
    virtual bool OptBtnSize(std::uint32_t id, int w, int h) = 0;                              // [18]
    virtual bool GetOptBtn(std::uint32_t id, cIGZWinBtn** out) = 0;                           // [19]
    virtual bool SetBackBMP(cIGZBuffer* image) = 0;                                           // [20]
    virtual bool SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) = 0;   // [21]
    virtual bool GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) const = 0;   // [22]
    virtual bool SetBtnCLSID(std::uint32_t classId) = 0;                                      // [23]
    virtual bool AutoSize() = 0;                                                              // [24]
    virtual bool LayoutW(int width) = 0;                                                      // [25]
    virtual bool LayoutH(int height) = 0;                                                     // [26]
    virtual bool GetMinMaxInfo(cRZRect& out) = 0;                                             // [27]
    virtual bool SetAutoFillType(int type, int heightLimit) = 0;                              // [28]
    virtual bool GetAutoFillType(int& type, int& heightLimit) const = 0;                      // [29]
};

class cGZWinOptGrp : public cGZWin, public cIGZWinOptGrp {
public:
    explicit cGZWinOptGrp(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    ~cGZWinOptGrp() override;
    static cGZWinOptGrp* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinOptGrp(mgr); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override;
    void SetCaption(const cIGZString& caption) override;
    bool GZPaint() override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnCommand(std::uint32_t command, std::uint64_t parameter) override;

    cIGZWin* AsIGZWin() override { return this; }
    bool GetOptGrpFlag(int flag) const override { return (flags_ & static_cast<std::uint32_t>(flag)) != 0; }
    bool SetOptGrpFlag(int flag, bool on) override { SetFlagInternal(flag, on); return true; }
    bool SetGutters(std::uint8_t x, std::uint8_t y) override;
    bool GetGutters(int& x, int& y) const override { x = gutterX_; y = gutterY_; return true; }
    bool SetLabelFont(IWinFont* font) override;
    bool SetBtnFont(IWinFont* font) override;
    bool SetFontColor(tOptGrpFontColorIdx index, std::uint32_t color) override;
    bool GetFontColor(tOptGrpFontColorIdx index, std::uint32_t& color) const override;
    bool ClearFontColor(tOptGrpFontColorIdx index) override;
    std::uint32_t GetValue() const override { return value_; }
    bool SetValue(std::uint32_t id) override;
    bool OptBtnAdd(std::uint32_t id, const cIGZString& caption, std::uint32_t unused, cIGZBuffer* image, const cRZRect* area) override;
    bool OptBtnDelete(std::uint32_t id) override;
    bool OptBtnMoveTo(std::uint32_t id, int x, int y) override;
    bool OptBtnSize(std::uint32_t id, int w, int h) override;
    bool GetOptBtn(std::uint32_t id, cIGZWinBtn** out) override { return GetChildAs(id, kGZIID_cIGZWinBtn, reinterpret_cast<void**>(out)); }
    bool SetBackBMP(cIGZBuffer* image) override;
    bool SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) override;
    bool GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) const override;
    bool SetBtnCLSID(std::uint32_t classId) override { buttonClass_ = classId; return true; }
    bool AutoSize() override;
    bool LayoutW(int) override { return false; }
    bool LayoutH(int) override { return false; }
    bool GetMinMaxInfo(cRZRect& out) override;
    bool SetAutoFillType(int type, int heightLimit) override { fillType_ = type; heightLimit_ = heightLimit > 0 ? heightLimit : 1; return true; }
    bool GetAutoFillType(int& type, int& heightLimit) const override { type = fillType_; heightLimit = heightLimit_; return true; }

protected:
    cIGZDrawContext* Context() const { return static_cast<cIGZDrawContext*>(drawContext_); }
    bool CheckBtnFont();                       // @ 0x10027caa2
    bool CheckLabelFont();                     // @ 0x10027cbb4
    bool CreateMainCaption();                  // @ 0x10027ccb0
    bool OnLayout(int unused, int height);     // @ 0x10027ce42
    void SetFlagInternal(int flag, bool on);   // @ 0x10027d934
    bool UpdateFontFlagsLabel();               // @ 0x10027d1da
    bool UpdateFontFlagsButton();              // @ 0x10027e2b6
    bool UpdateLabelColor();                   // @ 0x10027d24e
    void GetMinMaxAuto(cRZRect& out);          // @ 0x10027ddec
    void GetMinMaxManual(cRZRect& out);        // @ 0x10027e05e
    bool QueryLabel(void** out) { return GetChildAs(kOptGrpLabelId, kGZIID_cIGZWinText, out); }
    static bool EnumEnable(cIGZWin* parent, std::uint32_t id, void* button, void* enable);     // @ 0x10027d1aa
    static bool EnumButtonFont(cIGZWin* parent, std::uint32_t id, void* button, void* font);   // @ 0x10027e29e
    static bool EnumButtonColors(cIGZWin* parent, std::uint32_t id, void* button, void* colors);   // @ 0x10027e476

    std::uint32_t flags_ = 0xb;                 // +0x158
    std::uint32_t buttonClass_ = 0;             // +0x15c
    int fillType_ = 0;                          // +0x160
    int heightLimit_ = 0x80;                    // +0x164
    std::uint8_t gutterX_ = 4, gutterY_ = 4;    // +0x168
    std::uint16_t outline_[4] = {0, 0, 0, 0};   // +0x16a
    std::uint32_t value_ = 0;                   // +0x174
    cIGZBuffer* background_ = nullptr;          // +0x178
    IWinFont* labelFont_ = nullptr;             // +0x180
    IWinFont* buttonFont_ = nullptr;            // +0x188
    std::uint32_t colors_[5] = {0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu, 0xffffffffu};   // +0x190
};

}  // namespace gz
