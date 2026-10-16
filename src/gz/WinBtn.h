// cGZWinBtn — the push / toggle button control (0x10027fa30..0x100282426, iid 0x8810 for the button view).
// Layout (0x280 bytes): +0 the cIGZWinBtn view, +8 the 0x150-byte cGZWin, then +0x158 button flags, +0x188 text
// position (x, y) and size (w, h) as shorts, +0x190 caption style, +0x194 caption alignment, +0x196 gutters
// (left, top, right, bottom bytes), +0x1a0 font, +0x1a8 font style id, +0x1ac six font colours (0x8000000 = unset),
// +0x1c4 three sound ids, +0x1d0 style, +0x1d4 layout mode, +0x1d8 image area, +0x1e8 source x of the current
// frame, +0x1ec image offset, +0x1f8 bitmap, +0x200 tip text, +0x228 tip handle, +0x22c tip placement, +0x230
// tip offsets, +0x234 tip colours, +0x240 flash counter, +0x248 flash timer.
//
// Button flags: 1 on / checked, 2 act on mouse-down, 4 has a caption, 0x20 size to content, 0x40 wrapped caption,
// 0x80 caption shifts when pressed, 0x100 tip enabled, 0x200/0x400 tip placement bits, 0x800 right button acts like
// the left, 0x1000 pressed, 0x2000 hovered, 0x4000 pressed from the keyboard, 0x1000000 repaint only.
// Styles: 1 caption only, 2 three-frame image, 3 four-frame image, 4 eight-frame image; the layout mode (+0x1d4)
// derived from it is 0 / 1 / 1 / 2.
//
// Rendering of the frames goes through RZDrawBltEdge / RZDrawRaisedRect (RZDraw.h); fonts, tool tips and sounds are
// reached through IWinButtonHost.
#pragma once
#include <cstdint>

#include "gz/Buffer.h"
#include "gz/Win.h"
#include "gz/WinFont.h"
#include "rz/cRZCallbackTimer.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinBtn = 0x8810;
// Command messages the button sends to its notification target (type 3 = kGZMsgCommand).
constexpr std::uint32_t kBtnCmdActivated = 0x287259f6, kBtnCmdOnChanged = 0x287259f7, kBtnCmdPressedChanged = 0x287259f8,
                        kBtnCmdHoverChanged = 0x287259f9, kBtnCmdRightClicked = 0x287259f0;
constexpr std::uint32_t kBtnMsgFlashTick = 0xc7ec278e;

constexpr std::uint32_t kBtnOn = 1, kBtnActOnDown = 2, kBtnHasCaption = 4, kBtnFitToContent = 0x20, kBtnWrap = 0x40,
                        kBtnShiftWhenPressed = 0x80, kBtnTipEnabled = 0x100, kBtnRightActsAsLeft = 0x800,
                        kBtnPressed = 0x1000, kBtnHover = 0x2000, kBtnKeyPressed = 0x4000, kBtnRepaint = 0x1000000;
constexpr std::uint32_t kBtnDefaultFlags = 0x6a0;
constexpr std::uint32_t kBtnColorUnset = 0x8000000;
constexpr std::uint32_t kBtnFontDefaultStyle = 0x68963c4c, kBtnFontFallbackStyle = 0x68963c54;
constexpr std::uint32_t kBtnDefaultImageId = 0xe8963ef1;

struct cRZColor { std::uint32_t rgb = 0; };

using IWinButtonFont = IWinFont;   // the font as the button uses it (see WinFont.h)

// Tool tip request the button posts to the message server when the pointer rests on it (message 0x22c010d0).
struct BtnTipRequest {
    const cRZString* text = nullptr;
    std::uint32_t placement = 0;
    cRZRect anchor;
    std::uint32_t colors[3] = {};
};

// Window colour manager / sound / tip services of the button (RZ::WCM, RZ::SS, RZ::MS).
class IWinButtonHost {
public:
    virtual ~IWinButtonHost() = default;
    virtual IWinButtonFont* Font(std::uint32_t styleId) = 0;            // WCM vslot 0x28 (borrowed)
    virtual cIGZBuffer* Image(std::uint32_t imageId) = 0;               // WCM vslot 0x38 (borrowed)
    virtual std::uint32_t DefaultSoundId(int which) = 0;                // WCM vslot 0xe8
    virtual void PlaySound(std::uint32_t soundId) = 0;                  // RZ::SS(): create + play the effect
    virtual void ShowTip(const BtnTipRequest& request) = 0;
    virtual void HideTip() = 0;
    virtual bool HasWindowColorManager() = 0;
};
IWinButtonHost* WinButtonHost();
void SetWinButtonHost(IWinButtonHost* host);

class cIGZWinBtn : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                       // [3]
    virtual bool GetBtnFlag(int flag) const = 0;                                           // [4]
    virtual bool SetBtnFlag(int flag, bool on) = 0;                                        // [5]
    virtual bool ToggleOn() = 0;                                                           // [6]
    virtual bool ToggleOff() = 0;                                                          // [7]
    virtual bool IsOn() const = 0;                                                         // [8]
    virtual bool SetChecked(bool checked) = 0;                                             // [9]
    virtual bool IsChecked() const = 0;                                                    // [10]
    virtual int GetStyle() = 0;                                                            // [11]
    virtual bool SetStyle(int style, cIGZBuffer* image, const cRZRect* area) = 0;          // [12]
    virtual bool SetButtonStyle(int style) = 0;                                            // [13]
    virtual int GetImageW() const = 0;                                                     // [14]
    virtual int GetImageH() const = 0;                                                     // [15]
    virtual bool GetImageArea(cRZRect& out) const = 0;                                     // [16]
    virtual bool SetBuffer(cIGZBuffer* image) = 0;                                         // [17]
    virtual bool GetBuffer(cIGZBuffer*& out) const = 0;                                    // [18]
    virtual bool SetFont(IWinButtonFont* font) = 0;                                        // [19]
    virtual bool SetFontStyle(std::uint32_t styleId) = 0;                                  // [20]
    virtual bool GetFontStyle(std::uint32_t& styleId) = 0;                                 // [21]
    virtual bool SetFontColor(int index, std::uint32_t native) = 0;                        // [22]
    virtual bool GetFontColor(int index, std::uint32_t& native) const = 0;                 // [23]
    virtual bool SetFontColor(int index, cRZColor color) = 0;                              // [24]
    virtual bool GetFontColor(int index, cRZColor& color) const = 0;                       // [25]
    virtual bool ClearFontColor(int index) = 0;                                            // [26]
    virtual bool SetCaptionStyle(int style) = 0;                                           // [27]
    virtual bool GetCaptionStyle(int& style) const = 0;                                    // [28]
    virtual bool SetGutters(std::uint8_t x, std::uint8_t y) = 0;                           // [29]
    virtual bool GetGutters(int& x, int& y) const = 0;                                     // [30]
    virtual bool SetGutters(std::uint8_t l, std::uint8_t t, std::uint8_t r, std::uint8_t b) = 0;   // [31]
    virtual bool GetGutters(int& l, int& t, int& r, int& b) const = 0;                     // [32]
    virtual bool FitSizeToContent() = 0;                                                   // [33]
    virtual bool GetMinMaxInfo(cRZRect& out) = 0;                                          // [34]
    virtual bool SetTipText(const cIGZString& text) = 0;                                   // [35]
    virtual bool GetTipText(cIGZString& out) const = 0;                                    // [36]
    virtual bool ClearTipText() = 0;                                                       // [37]
    virtual bool SetTipPlacementType(int type) = 0;                                        // [38]
    virtual bool GetTipPlacementType(int& type) const = 0;                                 // [39]
    virtual bool SetTipPlacementOffsets(int x, int y) = 0;                                 // [40]
    virtual bool GetTipPlacementOffsets(int& x, int& y) const = 0;                         // [41]
    virtual bool SetTipColor(int index, std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;   // [42]
    virtual bool GetTipColor(int index, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const = 0;   // [43]
    virtual void Flash(std::uint32_t count, std::uint32_t periodMs) = 0;                   // [44]
    virtual bool SetSoundID(int which, std::uint32_t id) = 0;                              // [45]
    virtual std::uint32_t GetSoundID(int which) const = 0;                                 // [46]
};

class cGZWinBtn : public cIGZWinBtn, public cGZWin {
public:
    explicit cGZWinBtn(cIGZWinMgr* mgr);
    ~cGZWinBtn() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    // cGZWin overrides
    bool Init() override;
    bool Shutdown() override;
    bool DoMessage(cGZMessage& message) override;
    void SetArea(int l, int t, int r, int b) override;
    void SetArea(const cRZRect& area) override { SetArea(area.left, area.top, area.right, area.bottom); }
    std::uint32_t SetFlag(std::uint32_t flag, bool on) override;
    void SetKeyEquivalent(std::uint32_t key) override;
    void SetID(std::uint32_t id) override { cGZWin::SetID(id); }
    std::uint32_t GetID() const override { return cGZWin::GetID(); }
    void SetCaption(const cIGZString& caption) override;
    const cIGZString& GetCaption() const override { return cGZWin::GetCaption(); }
    bool GZPaint() override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t modifiers) override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseDownR(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpR(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseMove(int, int, std::uint32_t) override { return true; }
    bool GZOnMouseEnter(cIGZWin*) override;
    bool GZOnMouseExit(cIGZWin*) override;
    bool GZOnCaptureChanged(cIGZWin* previous, cIGZWin* current) override;

    // cIGZWinBtn
    cIGZWin* AsIGZWin() override { return this; }
    bool GetBtnFlag(int flag) const override { return (btnFlags_ & flag) != 0; }
    bool SetBtnFlag(int flag, bool on) override { SetBtnFlagInternal(flag, on); return true; }
    bool ToggleOn() override { SetBtnFlagInternal(kBtnOn, true); return true; }
    bool ToggleOff() override { SetBtnFlagInternal(kBtnOn, false); return true; }
    bool IsOn() const override { return btnFlags_ & kBtnOn; }
    bool SetChecked(bool checked) override { SetBtnFlagInternal(kBtnOn, checked); return true; }
    bool IsChecked() const override { return btnFlags_ & kBtnOn; }
    int GetStyle() override { return style_; }
    bool SetStyle(int style, cIGZBuffer* image, const cRZRect* area) override;
    bool SetButtonStyle(int style) override;
    int GetImageW() const override { return imageArea_.right - imageArea_.left; }
    int GetImageH() const override { return imageArea_.bottom - imageArea_.top; }
    bool GetImageArea(cRZRect& out) const override { out = imageArea_; return true; }
    bool SetBuffer(cIGZBuffer* image) override;
    bool GetBuffer(cIGZBuffer*& out) const override { out = image_; return true; }
    bool SetFont(IWinButtonFont* font) override;
    bool SetFontStyle(std::uint32_t styleId) override;
    bool GetFontStyle(std::uint32_t& styleId) override;
    bool SetFontColor(int index, std::uint32_t native) override;
    bool GetFontColor(int index, std::uint32_t& native) const override;
    bool SetFontColor(int index, cRZColor color) override;
    bool GetFontColor(int index, cRZColor& color) const override;
    bool ClearFontColor(int index) override { SetFontColor(index, kBtnColorUnset); return true; }
    bool SetCaptionStyle(int style) override;
    bool GetCaptionStyle(int& style) const override { style = captionStyle_; return true; }
    bool SetGutters(std::uint8_t x, std::uint8_t y) override { return SetGutters(x, y, x, y); }
    bool GetGutters(int& x, int& y) const override { x = gutters_[0]; y = gutters_[1]; return true; }
    bool SetGutters(std::uint8_t l, std::uint8_t t, std::uint8_t r, std::uint8_t b) override;
    bool GetGutters(int& l, int& t, int& r, int& b) const override { l = gutters_[0]; t = gutters_[1]; r = gutters_[2]; b = gutters_[3]; return true; }
    bool FitSizeToContent() override;
    bool GetMinMaxInfo(cRZRect& out) override;
    bool SetTipText(const cIGZString& text) override;
    bool GetTipText(cIGZString& out) const override { out.AssignFromIGZString(tipText_); return true; }
    bool ClearTipText() override;
    bool SetTipPlacementType(int type) override { tipPlacement_ = static_cast<std::uint32_t>(type); return true; }
    bool GetTipPlacementType(int& type) const override { type = static_cast<int>(tipPlacement_); return true; }
    bool SetTipPlacementOffsets(int x, int y) override { tipOffsetX_ = static_cast<std::int16_t>(x); tipOffsetY_ = static_cast<std::int16_t>(y); return true; }
    bool GetTipPlacementOffsets(int& x, int& y) const override { x = tipOffsetX_; y = tipOffsetY_; return true; }
    bool SetTipColor(int index, std::uint8_t r, std::uint8_t g, std::uint8_t b) override { tipColors_[index] = (std::uint32_t(r) << 16) | (std::uint32_t(g) << 8) | b; return true; }
    bool GetTipColor(int index, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const override;
    void Flash(std::uint32_t count, std::uint32_t periodMs) override;
    bool SetSoundID(int which, std::uint32_t id) override { soundIds_[which] = id; return true; }
    std::uint32_t GetSoundID(int which) const override { return soundIds_[which]; }

    // internals (public for the tests)
    bool ButtonActivated();
    bool ButtonRightClicked();
    int CurrentFrameX() const { return sourceX_; }
    void TextPosition(int& x, int& y, int& w, int& h) const { x = textX_; y = textY_; w = textW_; h = textH_; }
    int FlashCount() const { return flashCount_; }
    static void FlashTimerCallback(void* button);

private:
    cIGZDrawContext* Context() const { return static_cast<cIGZDrawContext*>(drawContext_); }
    bool SetBtnFlagInternal(int flag, bool on);
    bool OnLayout(int w, int h);
    void UpdateTextSize();
    void UpdateTextPos(const cRZRect& rect);
    void PaintBitmap();
    void PaintFlat();
    void PaintLabel();
    void PlaySound(int which);
    bool ShowTip();
    void HideTip();
    void ReplaceFont(IWinButtonFont* font);

    std::uint32_t btnFlags_ = kBtnDefaultFlags;                 // +0x158
    cRZString reserved_;                                        // +0x160 (unused)
    std::int16_t textX_ = 0, textY_ = 0, textW_ = 0, textH_ = 0;   // +0x188
    int captionStyle_ = 0;                                      // +0x190
    std::uint16_t captionAlign_ = 0;                            // +0x194
    std::uint8_t gutters_[4] = {};                              // +0x196
    IWinButtonFont* font_ = nullptr;                            // +0x1a0
    std::uint32_t fontStyle_ = 0;                               // +0x1a8
    std::uint32_t fontColors_[6];                               // +0x1ac
    std::uint32_t soundIds_[3] = {};                            // +0x1c4
    int style_ = 0;                                             // +0x1d0
    int layoutMode_ = 0;                                        // +0x1d4
    cRZRect imageArea_;                                         // +0x1d8
    int sourceX_ = 0;                                           // +0x1e8
    int imageOffsetX_ = 0, imageOffsetY_ = 0;                   // +0x1ec
    cIGZBuffer* image_ = nullptr;                               // +0x1f8
    cRZString tipText_;                                         // +0x200
    int tipHandle_ = 0;                                         // +0x228
    std::uint32_t tipPlacement_ = 0;                            // +0x22c
    std::int16_t tipOffsetX_ = 0, tipOffsetY_ = 0;              // +0x230
    std::uint32_t tipColors_[3] = {0, 0x00ffffe0, 0};           // +0x234
    int flashCount_ = 0;                                        // +0x240
    cRZCallbackTimer flashTimer_;                               // +0x248
};

}  // namespace gz
