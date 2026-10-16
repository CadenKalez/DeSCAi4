// cGZWinSpinner — a number box with up/down buttons (0x100278540..0x10027a1a6, 57 functions, iid 0x612ce0c3 for the
// spinner view at +0x150). The value lives in [min, max], changes by the step size, and is shown as the window's
// caption. Holding a button auto-repeats with a delay that starts at 400 ms and shrinks by 50 ms per repeat.
//
// Spinner type: 0 buttons only (two stacked buttons filling the window), 1 text with the buttons on the right,
// 2 text with the buttons on the left. Mouse positions: 0 none, 1 upper button, 2 lower button.
// Spinner flags: 1 outline, 2 opaque background, 0x1000 caption follows the value, 0x2000 / 0x4000 format the value
// with the language manager (they exclude each other), 0x8000 the button image area was set explicitly.
//
// Layout (0x1f8 bytes): cGZWin (0x150), +0x150 cIGZWinSpinner view, +0x158 flags (0x1000), +0x15c type, +0x160 repeat
// timer, +0x178 repeat delay (400), +0x17c pressed position, +0x180 hovered position, +0x184 min, +0x188 max, +0x18c
// value, +0x190 step (1), +0x198 font, +0x1a0 font style, +0x1a4 font line height, +0x1a6 text alignment (2),
// +0x1a8 text colours (normal, hovered, disabled; packed RGB, 0xdeadbeef = the font's own), +0x1b8 button image,
// +0x1c0 image area, +0x1d0 image cell origin, +0x1d4 / +0x1d8 button width / height, +0x1dc / +0x1dd gutters (4, 1),
// +0x1e0 outline colours (left, top, right, bottom), +0x1f0 / +0x1f2 flat button width limits (16 / 8), +0x1f4
// flat button colour.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "gz/DrawContext.h"
#include "gz/Win.h"
#include "gz/WinBtn.h"
#include "gz/WinFont.h"
#include "gz/WinScrollbar2.h"
#include "rz/cRZTimer.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinSpinner = 0x612ce0c3;
constexpr std::uint32_t kGZMsgSpinnerChanged = 0x682876c9;        // sent to the notification target: p1 window id, p2 value
constexpr std::uint32_t kSpinnerDefaultImageId = 0x68963ed6;      // the button skin Init asks the colour manager for
constexpr std::uint32_t kSpinnerFlagOutline = 1, kSpinnerFlagOpaque = 2, kSpinnerFlagShowValue = 0x1000,
                        kSpinnerFlagFormatA = 0x2000, kSpinnerFlagFormatB = 0x4000, kSpinnerFlagImageArea = 0x8000;

// RZ::LM()'s number formatting (vslots 0x90 / 0x80 of its first language object): writes the localised text of `value`.
class IWinNumberFormatter {
public:
    virtual ~IWinNumberFormatter() = default;
    virtual bool Available() = 0;
    virtual void FormatA(int value, cRZString& out) = 0;     // vslot 0x90, used by flag 0x2000
    virtual void FormatB(int value, cRZString& out) = 0;     // vslot 0x80, used by flag 0x4000
    virtual int CompareStrings(const cIGZString& a, const cIGZString& b, bool flag) { return a.CompareToString(b, flag); }   // vslot 0xb0 (list sorting)
};
IWinNumberFormatter* WinNumberFormatter();
void SetWinNumberFormatter(IWinNumberFormatter* formatter);

class cIGZWinSpinner : public cIGZUnknown {
public:
    enum tSpinnerType : std::uint32_t { kButtonsOnly = 0, kButtonsRight = 1, kButtonsLeft = 2 };
    enum tFontColorIdx : std::uint32_t { kColorNormal = 0, kColorHover = 1, kColorDisabled = 2 };
    enum tOutlineIndex : std::uint32_t { kOutlineLeft = 0, kOutlineTop = 1, kOutlineRight = 2, kOutlineBottom = 3 };
    virtual cIGZWin* AsIGZWin() = 0;                                                         // [3]
    virtual bool SetSpinnerType(tSpinnerType type) = 0;                                      // [4]
    virtual bool GetSpinnerType(tSpinnerType& type) const = 0;                               // [5]
    virtual bool SetSpinnerFlag(std::uint32_t flag, bool on) = 0;                            // [6]
    virtual bool GetSpinnerFlag(std::uint32_t flag) const = 0;                               // [7]
    virtual int GetValue() const = 0;                                                        // [8]
    virtual bool SetValue(int value) = 0;                                                    // [9]
    virtual bool SetMinMax(int low, int high) = 0;                                           // [10]
    virtual bool GetMinMax(int& low, int& high) const = 0;                                   // [11]
    virtual bool SetStepSize(int step) = 0;                                                  // [12]
    virtual bool GetStepSize(int& step) const = 0;                                           // [13]
    virtual bool SetImage(cIGZBuffer* image, const cRZRect* area) = 0;                       // [14]
    virtual bool SetButtonImage(cIGZBuffer* image) = 0;                                      // [15]
    virtual bool GetButtonImage(cIGZBuffer*& image) = 0;                                     // [16]
    virtual bool SetButtonImageArea(const cRZRect& area) = 0;                                // [17]
    virtual bool GetButtonImageArea(cRZRect& area) = 0;                                      // [18]
    virtual bool SetFont(IWinFont* font) = 0;                                                // [19]
    virtual bool SetFontStyle(std::uint32_t styleId) = 0;                                    // [20]
    virtual bool GetFontStyle(std::uint32_t& styleId) = 0;                                   // [21]
    virtual bool SetFontColor(tFontColorIdx index, std::uint32_t native) = 0;                // [22]
    virtual bool GetFontColor(tFontColorIdx index, std::uint32_t& native) const = 0;         // [23]
    virtual bool SetFontColor(tFontColorIdx index, cRZColor color) = 0;                      // [24]
    virtual bool GetFontColor(tFontColorIdx index, cRZColor& color) const = 0;               // [25]
    virtual bool ClearFontColor(tFontColorIdx index) = 0;                                    // [26]
    virtual bool SetTextAlignment(std::uint32_t alignment) = 0;                              // [27]
    virtual bool GetTextAlignment(std::uint32_t& alignment) const = 0;                       // [28]
    virtual bool SetGutters(std::uint8_t x, std::uint8_t y) = 0;                             // [29]
    virtual bool GetGutters(std::uint8_t& x, std::uint8_t& y) const = 0;                     // [30]
    virtual bool SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) = 0;   // [31]
    virtual bool GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) const = 0;   // [32]
    virtual bool SetOutlineColor(tOutlineIndex index, const cRZColor& color) = 0;            // [33]
    virtual bool GetOutlineColor(tOutlineIndex index, cRZColor& color) const = 0;            // [34]
};

class cGZWinSpinner : public cGZWin, public cIGZWinSpinner {
public:
    explicit cGZWinSpinner(cIGZWinMgr* mgr);
    ~cGZWinSpinner() override;
    static cGZWinSpinner* CreateNewObject(cIGZWinMgr* mgr) { return new cGZWinSpinner(mgr); }

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    bool GZPaint() override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t modifiers) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t modifiers) override;
    bool GZOnCaptureChanged(cIGZWin* previous, cIGZWin* next) override;

    cIGZWin* AsIGZWin() override { return this; }
    bool SetSpinnerType(tSpinnerType type) override;
    bool GetSpinnerType(tSpinnerType& type) const override { type = static_cast<tSpinnerType>(type_); return true; }
    bool SetSpinnerFlag(std::uint32_t flag, bool on) override { SetFlagInternal(flag, on); return true; }
    bool GetSpinnerFlag(std::uint32_t flag) const override { return (flags_spinner_ & flag) != 0; }
    int GetValue() const override { return value_; }
    bool SetValue(int value) override;
    bool SetMinMax(int low, int high) override { min_ = low; max_ = high; return true; }
    bool GetMinMax(int& low, int& high) const override { low = min_; high = max_; return true; }
    bool SetStepSize(int step) override { step_ = step; return true; }
    bool GetStepSize(int& step) const override { step = step_; return true; }
    bool SetImage(cIGZBuffer* image, const cRZRect* area) override;
    bool SetButtonImage(cIGZBuffer* image) override;
    bool GetButtonImage(cIGZBuffer*& image) override { image = image_; return true; }
    bool SetButtonImageArea(const cRZRect& area) override;
    bool GetButtonImageArea(cRZRect& area) override { area = imageArea_; return true; }
    bool SetFont(IWinFont* font) override;
    bool SetFontStyle(std::uint32_t styleId) override;
    bool GetFontStyle(std::uint32_t& styleId) override { styleId = fontStyle_; return true; }
    bool SetFontColor(tFontColorIdx index, std::uint32_t native) override;
    bool GetFontColor(tFontColorIdx index, std::uint32_t& native) const override;
    bool SetFontColor(tFontColorIdx index, cRZColor color) override;
    bool GetFontColor(tFontColorIdx index, cRZColor& color) const override;
    // Goes through the native-colour overload like the original, so the stored value is the converted 0xdeadbeef.
    bool ClearFontColor(tFontColorIdx index) override { SetFontColor(index, kWinUnsetTextColor); return true; }
    bool SetTextAlignment(std::uint32_t alignment) override;
    bool GetTextAlignment(std::uint32_t& alignment) const override;
    bool SetGutters(std::uint8_t x, std::uint8_t y) override;
    bool GetGutters(std::uint8_t& x, std::uint8_t& y) const override { x = gutterX_; y = gutterY_; return true; }
    bool SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) override;
    bool GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) const override;
    bool SetOutlineColor(tOutlineIndex index, const cRZColor& color) override;
    bool GetOutlineColor(tOutlineIndex index, cRZColor& color) const override;

protected:
    enum tMousePos : std::uint32_t { kPosNone = 0, kPosUp = 1, kPosDown = 2 };
    cIGZDrawContext* Context() const { return static_cast<cIGZDrawContext*>(drawContext_); }
    bool SetCaptionFromValue();                                  // @ 0x100278972
    void CalcImageArea();                                        // @ 0x100278c06
    bool CalcMousePos(std::uint32_t& out, int x, int y) const;   // @ 0x1002796e0
    void SetFlagInternal(std::uint32_t flag, bool on);           // @ 0x1002799e6
    void PaintButtonsFlat(int offset);                           // @ 0x100279374
    void PaintButtonsImage(int offset);                          // @ 0x100279444
    void ResizeToMinimum() { const cRZRect a = GetArea(); SetArea(a.left, a.top, a.right, a.bottom); }

    std::uint32_t flags_spinner_ = kSpinnerFlagShowValue;        // +0x158
    std::uint32_t type_ = 0;                                     // +0x15c
    cRZTimer timer_{cRZTimer::kMilliseconds};                    // +0x160
    std::uint32_t repeatDelay_ = 400;                            // +0x178
    std::uint32_t pressed_ = 0;                                  // +0x17c
    std::uint32_t hovered_ = 0;                                  // +0x180
    int min_ = 0, max_ = 0;                                      // +0x184 / +0x188
    int value_ = 0;                                              // +0x18c
    int step_ = 1;                                               // +0x190
    IWinFont* font_ = nullptr;                                   // +0x198
    std::uint32_t fontStyle_ = kWinDefaultFontStyle;             // +0x1a0
    std::int16_t fontHeight_ = 0;                                // +0x1a4
    std::uint8_t alignment_ = 2;                                 // +0x1a6
    std::uint32_t textColors_[3] = {kWinUnsetTextColor, kWinUnsetTextColor, kWinUnsetTextColor};   // +0x1a8
    cIGZBuffer* image_ = nullptr;                                // +0x1b8
    cRZRect imageArea_{0, 0, 0, 0};                              // +0x1c0
    std::uint16_t cellX_ = 0, cellY_ = 0;                        // +0x1d0 / +0x1d2
    int buttonW_ = 0, buttonH_ = 0;                              // +0x1d4 / +0x1d8
    std::uint8_t gutterX_ = 4, gutterY_ = 1;                     // +0x1dc / +0x1dd
    std::uint32_t outline_[4] = {0, 0, 0, 0};                    // +0x1e0
    std::uint16_t maxButton_ = 0x10, minButton_ = 8;             // +0x1f0 / +0x1f2
    std::uint32_t buttonColor_ = 0;                              // +0x1f4
};

}  // namespace gz
