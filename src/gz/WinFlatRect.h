// cGZWinFlatRect — a solid filled rectangle with optional 1-pixel outline edges (0x1002942de..0x100294e90).
// It passes mouse input through to its parent. Layout: the 0x150-byte cGZWin, +0x150 the cIGZWinFlatRect view,
// +0x158 size mode, +0x159 outline flags (1 left, 2 top, 4 right, 8 bottom, 0x10 raised, 0x20 sunken, 0x40 no
// fill), +0x15c..+0x168 the four edge colours (packed RGB; left, top, right, bottom).
// Class id / interface ids: 0x22ba0121 (cIGZWin), 0xe98b2f57, 1, and 0xc2afa76f for the flat-rect view.
#pragma once
#include "gz/DrawContext.h"
#include "gz/Win.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinFlatRect = 0xc2afa76f;
constexpr std::uint32_t kWinColorShadow = 0xa8963fad, kWinColorHighlight = 0x08963faf;

// The window colour manager (RZ::WCM(), vslot 0x110) the bevel flags ask for the 3-D edge colours.
class IWinColorManager {
public:
    virtual ~IWinColorManager() = default;
    virtual void GetColor(std::uint32_t id, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) = 0;
};
IWinColorManager* WinColorManager();
void SetWinColorManager(IWinColorManager* manager);

constexpr std::uint8_t kOutlineLeft = 1, kOutlineTop = 2, kOutlineRight = 4, kOutlineBottom = 8,
                       kFlatRectRaised = 0x10, kFlatRectSunken = 0x20, kOutlineNoFill = 0x40;   // renamed: WinOutline.h owns gz::kOutlineRaised/kOutlineSunken

class cIGZWinFlatRect : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                                                        // [3]
    virtual void SetFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;                          // [4]
    virtual void GetFillColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const = 0;                 // [5]
    virtual void SetOutlineColor(int edge, std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;             // [6]
    virtual void GetOutlineColor(int edge, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const = 0;    // [7]
    virtual void SetOutlineColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) = 0;                       // [8]
    virtual bool SetOutlineFlags(int flags) = 0;                                                            // [9]
    virtual std::uint8_t GetOutlineFlags() = 0;                                                             // [10]
    virtual bool GetOutlineFlag(int flag) = 0;                                                              // [11]
    virtual void SetFillColor(std::uint32_t nativeColor) = 0;                                               // [12]
    virtual std::uint32_t GetFillColor() const = 0;                                                         // [13]
    virtual void SetOutlineColor(std::uint32_t nativeColor) = 0;                                            // [14]
    virtual void SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) = 0;   // [15]
    virtual void GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) = 0;   // [16]
    virtual bool AutoSize(const cRZRect& parentArea) = 0;                                                   // [17]
    virtual bool SetSizeMode(int mode) = 0;                                                                 // [18]
    virtual std::uint8_t GetSizeMode() = 0;                                                                 // [19]
};

class cGZWinFlatRect : public cGZWin, public cIGZWinFlatRect {
public:
    explicit cGZWinFlatRect(cIGZWinMgr* mgr) : cGZWin(mgr) {}

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool DoMessage(cGZMessage& message) override;
    bool GZPaint() override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t flags) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t flags) override;

    cIGZWin* AsIGZWin() override { return this; }
    void SetFillColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override { cGZWin::SetFillColor(r, g, b); }
    void GetFillColor(std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const override { cGZWin::GetFillColor(r, g, b); }
    void SetFillColor(std::uint32_t nativeColor) override { cGZWin::SetFillColor(nativeColor); }
    std::uint32_t GetFillColor() const override { return cGZWin::GetFillColor(); }
    void SetOutlineColor(int edge, std::uint8_t r, std::uint8_t g, std::uint8_t b) override;
    void GetOutlineColor(int edge, std::uint8_t& r, std::uint8_t& g, std::uint8_t& b) const override;
    void SetOutlineColor(std::uint8_t r, std::uint8_t g, std::uint8_t b) override;
    bool SetOutlineFlags(int flags) override;
    std::uint8_t GetOutlineFlags() override { return outlineFlags_; }
    bool GetOutlineFlag(int flag) override { return (outlineFlags_ & flag) != 0; }
    void SetOutlineColor(std::uint32_t nativeColor) override;
    void SetOutlineColors(std::uint32_t left, std::uint32_t top, std::uint32_t right, std::uint32_t bottom) override;
    void GetOutlineColors(std::uint32_t& left, std::uint32_t& top, std::uint32_t& right, std::uint32_t& bottom) override;
    bool AutoSize(const cRZRect& parentArea) override;
    bool SetSizeMode(int mode) override { sizeMode_ = static_cast<std::uint8_t>(mode); return true; }
    std::uint8_t GetSizeMode() override { return sizeMode_; }

private:
    cIGZDrawContext* Context() const { return static_cast<cIGZDrawContext*>(drawContext_); }
    std::uint8_t sizeMode_ = 0;           // +0x158
    std::uint8_t outlineFlags_ = 0;       // +0x159
    std::uint32_t edgeColors_[4] = {};    // +0x15c
};

}  // namespace gz
