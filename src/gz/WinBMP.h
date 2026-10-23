// cGZWinBMP — a window that shows (part of) a buffer (0x100330480..0x100330e3e, 41 functions). Layout: the
// 0x150-byte cGZWin, +0x150 the cIGZWinBMP view (iid 0xc12cea13), +0x158 buffer, +0x160 alpha blending on,
// +0x164 alpha (0.5), +0x168 image area (the part of the buffer shown; starts at 0,0 and is never larger than the
// buffer), +0x178 flags (default 0x12).
//
// Flags: 1 pass messages on to the parent, 2 do not paint the fill colour first, 4 transparent to the mouse,
// 8 draw the buffer as a nine-slice frame (tiles are a third of the image area), 0x10 / 0x20 use the image
// area (0x20: also draw it at its own offset instead of the window's corner).
#pragma once
#include "gz/Buffer.h"
#include "gz/RZDraw.h"
#include "gz/Win.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinBMP = 0xc12cea13;
constexpr std::uint32_t kGZMsgBMPMouseMove = 0xc89155e3;
constexpr std::uint32_t kGZMsgBMPMouseDownL = 0x68915615;
constexpr std::uint32_t kGZMsgBMPMouseUpL = 0x28916985;

constexpr int kWinBMPPassMessages = 1, kWinBMPNoFill = 2, kWinBMPMouseTransparent = 4, kWinBMPNineSlice = 8,
              kWinBMPUseImageArea = 0x10, kWinBMPImageAreaOffset = 0x20;

class cIGZWinBMP : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                           // [3]
    virtual void SetBMP(cIGZBuffer* buffer) = 0;                               // [4]
    virtual bool GetBMP(std::uint32_t iid, void** out) const = 0;              // [5]
    virtual void SetAlphaBlending(bool enable, float alpha) = 0;               // [6]
    virtual void GetAlphaBlending(bool& enable, float& alpha) const = 0;       // [7]
    virtual void SetImageArea(const cRZRect& area) = 0;                        // [8]
    virtual void GetImageArea(cRZRect& area) const = 0;                        // [9]
    virtual bool GetWinBMPFlag(int flag) const = 0;                            // [10]
    virtual void SetWinBMPFlag(int flag, bool on) = 0;                         // [11]
};

class cGZWinBMP : public cGZWin, public cIGZWinBMP {
public:
    explicit cGZWinBMP(cIGZWinMgr* mgr) : cGZWin(mgr) {}
    ~cGZWinBMP() override;

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    bool DoMessage(cGZMessage& message) override;
    bool GZPaint() override;
    void SetArea(int l, int t, int r, int b) override { cGZWin::SetArea(l, t, r, b); }
    void SetArea(const cRZRect& area) override { cGZWin::SetArea(area); }
    bool IsPointInWindowScreenCoordinates(int x, int y) const override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t flags) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;

    cIGZWin* AsIGZWin() override { return this; }
    void SetBMP(cIGZBuffer* buffer) override;
    bool GetBMP(std::uint32_t iid, void** out) const override;
    void SetAlphaBlending(bool enable, float alpha) override;
    void GetAlphaBlending(bool& enable, float& alpha) const override { enable = alphaBlend_; alpha = alpha_; }
    void SetImageArea(const cRZRect& area) override;
    void GetImageArea(cRZRect& area) const override { area = imageArea_; }
    bool GetWinBMPFlag(int flag) const override { return (flags_ & static_cast<std::uint32_t>(flag)) != 0; }
    void SetWinBMPFlag(int flag, bool on) override {
        if (on) flags_ |= static_cast<std::uint32_t>(flag);
        else flags_ &= ~static_cast<std::uint32_t>(flag);
    }

    void SetSourceBufferAreaFromWindowArea(const cRZRect& area);

protected:
    cIGZBuffer* bmp_ = nullptr;          // +0x158
    bool alphaBlend_ = false;            // +0x160
    float alpha_ = 0.5f;                 // +0x164
    cRZRect imageArea_;                  // +0x168
    std::uint32_t flags_ = 0x12;         // +0x178
};

}  // namespace gz
