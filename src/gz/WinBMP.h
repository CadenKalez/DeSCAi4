// cGZWinBMP — a window that shows a bitmap (0x100330480..0x100330e3e). Layout: the 0x150-byte cGZWin, +0x150
// the cIGZWinBMP view (iid 0xc12cea13), +0x158 bitmap, +0x160 alpha blending on, +0x164 blend amount (0.5),
// +0x168 image area (l, t, r, b), +0x178 flags (initially 0x12).
// Flags: 1 pass mouse messages through to the parent, 2 do not fill the background, 4 invisible to hit tests,
// 8 draw as a 3x3 stretched edge (RZDrawBltEdge, not recovered), 0x10 use the image area as the source,
// 0x20 draw the image area at its own offset.
#pragma once
#include "gz/DrawContext.h"
#include "gz/Win.h"

namespace gz {

constexpr std::uint32_t kGZIID_cIGZWinBMP = 0xc12cea13;
constexpr std::uint32_t kWinBMPPassThrough = 1, kWinBMPNoFill = 2, kWinBMPNoHit = 4, kWinBMPEdge = 8, kWinBMPUseImageArea = 0x10,
                        kWinBMPAtOffset = 0x20;
// Notification messages the bitmap window posts to its notification target.
constexpr std::uint32_t kWinMsgMouseMove = 0xc89155e3, kWinMsgMouseDownL = 0x68915615, kWinMsgMouseUpL = 0x28916985;

class IWinBitmap : public IDrawTargetBuffer {
public:
    virtual int Width() const = 0;     // vslot 0x58
    virtual int Height() const = 0;    // vslot 0x60
};

// RZDrawBltEdge(context, bitmap, source, dest, true): the stretched-edge blit.
class IWinEdgeBlit {
public:
    virtual ~IWinEdgeBlit() = default;
    virtual void DrawBltEdge(cIGZDrawContext* context, IWinBitmap* bitmap, const cRZRect& source, const cRZRect& dest) = 0;
};
IWinEdgeBlit* WinEdgeBlit();
void SetWinEdgeBlit(IWinEdgeBlit* blit);

class cIGZWinBMP : public cIGZUnknown {
public:
    virtual cIGZWin* AsIGZWin() = 0;                                              // [3]
    virtual void SetBMP(IWinBitmap* bitmap) = 0;                                  // [4]
    virtual bool GetBMP(std::uint32_t iid, void** out) const = 0;                 // [5]
    virtual void SetAlphaBlending(bool on, float amount) = 0;                     // [6]
    virtual void GetAlphaBlending(bool& on, float& amount) const = 0;             // [7]
    virtual void SetImageArea(const cRZRect& area) = 0;                           // [8]
    virtual void GetImageArea(cRZRect& area) const = 0;                           // [9]
    virtual bool GetWinBMPFlag(int flag) const = 0;                               // [10]
    virtual void SetWinBMPFlag(int flag, bool on) = 0;                            // [11]
};

class cGZWinBMP : public cGZWin, public cIGZWinBMP {
public:
    explicit cGZWinBMP(cIGZWinMgr* mgr) : cGZWin(mgr) {}

    bool QueryInterface(std::uint32_t iid, void** out) override;
    std::uint32_t AddRef() override { return cGZWin::AddRef(); }
    std::uint32_t Release() override { return cGZWin::Release(); }

    bool Init() override;
    bool Shutdown() override;
    bool GZPaint() override;
    bool DoMessage(cGZMessage& message) override;
    bool GZOnKeyDown(std::uint32_t key, std::uint32_t flags) override;
    bool GZOnKeyUp(std::uint32_t key, std::uint32_t flags) override;
    bool GZOnMouseMove(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseDownL(int x, int y, std::uint32_t flags) override;
    bool GZOnMouseUpL(int x, int y, std::uint32_t flags) override;
    bool IsPointInWindowScreenCoordinates(int x, int y) const override;

    cIGZWin* AsIGZWin() override { return this; }
    void SetBMP(IWinBitmap* bitmap) override;
    bool GetBMP(std::uint32_t iid, void** out) const override { return bitmap_ && bitmap_->QueryInterface(iid, out); }
    void SetAlphaBlending(bool on, float amount) override;
    void GetAlphaBlending(bool& on, float& amount) const override { on = alphaBlending_; amount = alphaAmount_; }
    void SetImageArea(const cRZRect& area) override;
    void GetImageArea(cRZRect& area) const override { area = imageArea_; }
    bool GetWinBMPFlag(int flag) const override { return (bmpFlags_ & flag) != 0; }
    void SetWinBMPFlag(int flag, bool on) override { bmpFlags_ = on ? (bmpFlags_ | flag) : (bmpFlags_ & ~flag); }

    void SetSourceBufferAreaFromWindowArea(const cRZRect& area);

private:
    void ClampImageSize();
    cIGZDrawContext* Context() const { return static_cast<cIGZDrawContext*>(drawContext_); }
    IWinBitmap* bitmap_ = nullptr;      // +0x158
    bool alphaBlending_ = false;        // +0x160
    float alphaAmount_ = 0.5f;          // +0x164
    cRZRect imageArea_;                 // +0x168
    std::uint32_t bmpFlags_ = 0x12;     // +0x178
};

}  // namespace gz
